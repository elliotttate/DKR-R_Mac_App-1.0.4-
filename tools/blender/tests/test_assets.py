"""Resolve every object type to its artwork, and decode every object model.

The preview system is only as good as the name chain behind it, so this walks
that chain for all 85 object types that appear in retail tracks and reports how
many resolve to a sprite, a mesh, or nothing. It also decodes all 390 extracted
object models and checks each is internally consistent.

Runs on any Python 3.8+; it does not need Blender.

    python tools/blender/tests/test_assets.py
"""

from __future__ import annotations

import collections
import glob
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(_HERE))

from dkr_track_editor import (  # noqa: E402
    assets, catalog as catalog_module, object_model,
)

from test_roundtrip import VANILLA  # noqa: E402

FAILURES = []


def check(condition, message):
    if not condition:
        FAILURES.append(message)
    return condition


def find_tree():
    for version in sorted(glob.glob(os.path.join(VANILLA, "*"))):
        tree = assets.AssetTree.find(version)
        if tree is not None:
            return tree
    return None


def test_object_models(tree):
    paths = sorted(glob.glob(os.path.join(tree.root, "objects", "models", "*.bin")))
    if not paths:
        print("  skip: no extracted object models")
        return
    decoded = vertices = triangles = 0
    for path in paths:
        try:
            model = object_model.load(path)
        except Exception as error:  # noqa: BLE001
            FAILURES.append("%s: %s" % (os.path.basename(path), error))
            continue
        decoded += 1
        vertices += len(model.vertices)
        triangles += len(model.triangles)
        for index, batch in enumerate(model.batches):
            if batch.vertex_count < 0 or batch.face_count < 0:
                FAILURES.append("%s batch %d has a negative span"
                                % (os.path.basename(path), index))
            if batch.vertex_count > 256:
                FAILURES.append(
                    "%s batch %d spans %d vertices; the index is a u8"
                    % (os.path.basename(path), index, batch.vertex_count)
                )
        # Every face must land inside the vertex array once resolved.
        for indices, _batch in model.faces():
            if max(indices) >= len(model.vertices):
                FAILURES.append("%s has a face past its vertices"
                                % os.path.basename(path))
                break
    print("  object models : %d of %d decoded, %d vertices, %d triangles"
          % (decoded, len(paths), vertices, triangles))
    check(decoded == len(paths), "every object model decodes")


def test_texture_resolution(tree):
    """Every textured batch of every object model must find its PNG.

    A model's texture table stores an index into the global 3D texture list, and
    an animated texture has no file at its own stem - only numbered frames. So a
    naive lookup silently leaves things like the zipper untextured.
    """
    paths = sorted(glob.glob(os.path.join(tree.root, "objects", "models", "*.bin")))
    if not paths:
        return
    total = resolved = untextured = 0
    unresolved = collections.Counter()
    for path in paths:
        try:
            model = object_model.load(path)
        except Exception:  # noqa: BLE001
            continue
        for batch in model.batches:
            total += 1
            texture = model.texture_for(batch)
            if texture is None:
                untextured += 1
                continue
            if tree.texture_3d_png(texture.texture_id):
                resolved += 1
            else:
                unresolved[texture.texture_id] += 1

    print("  batches       : %d total, %d textured and resolved, %d untextured"
          % (total, resolved, untextured))
    check(not unresolved,
          "every textured batch resolves to a PNG (%d ids did not: %s)"
          % (len(unresolved), list(unresolved)[:6]))


def test_uvs(tree):
    """UVs must come out at the right scale once taken out of fixed point.

    Coordinates outside 0..1 are legitimate - N64 textures tile, and a face can
    span many repeats - so an outlier proves nothing. What does prove the
    divisor is right is the bulk of them: with the wrong scale every value would
    be off by a factor of 32 or of the texture size, and the median would move
    with it.
    """
    paths = sorted(glob.glob(os.path.join(tree.root, "objects", "models", "*.bin")))
    if not paths:
        print("  skip: no object models")
        return

    values = []
    faces = 0
    for path in paths[:80]:
        try:
            model = object_model.load(path)
        except Exception:  # noqa: BLE001
            continue
        check(len(model.uvs) == len(model.triangles) or not model.triangles,
              "%s pairs UVs with triangles" % os.path.basename(path))
        for batch in model.batches:
            texture = model.texture_for(batch)
            if texture is None:
                continue
            for face in range(batch.face_offset, batch.face_offset + batch.face_count):
                coordinates = model.face_uvs(face, texture)
                if coordinates is None:
                    continue
                faces += 1
                for u, v in coordinates:
                    values.append(abs(u))
                    values.append(abs(v))

    if not values:
        FAILURES.append("no textured faces had UVs at all")
        return
    values.sort()
    median = values[len(values) // 2]
    inside = sum(1 for value in values if value <= 2.0) / float(len(values))
    print("  UVs           : %d faces, median |uv| %.3f, %.0f%% within two tiles"
          % (faces, median, inside * 100))
    check(median <= 2.0,
          "the median UV is within a couple of tiles (got %.3f); a wrong divisor "
          "would move this by a factor of 32" % median)
    check(inside >= 0.8,
          "most UVs sit within two tiles (got %.0f%%)" % (inside * 100))


def test_resolution(tree):
    """Every catalogued object type should resolve to something, or nothing on purpose."""
    catalog = catalog_module.load()
    kinds = collections.Counter()
    missing = []

    for object_id in sorted(catalog.types):
        header = tree.object_header(object_id)
        if header is None:
            missing.append(object_id)
            kinds["no header"] += 1
            continue
        kind, path, _header = tree.preview_for(object_id)
        kinds[kind] += 1
        if kind != "none":
            check(path and os.path.isfile(path),
                  "%s resolves to a file that exists (%s)" % (object_id, path))

    print("  object types  : %s"
          % ", ".join("%s %d" % (k, n) for k, n in sorted(kinds.items())))
    if missing:
        print("  no header     : %s" % ", ".join(m.replace("ASSET_OBJECT_", "")
                                                  for m in missing[:8]))

    drawable = kinds["sprite"] + kinds["mesh"]
    check(drawable >= 60,
          "most object types resolve to artwork (%d of %d)" % (drawable, len(catalog.types)))


def test_levels(tree):
    """Every level header must resolve to a model and both of its object maps.

    A track's objects live in two maps - ``map-2`` for the track and
    ``map-collectables`` for the pickups - and all 65 retail headers carry both.
    Loading only the first leaves a track with no coins and no balloons.
    """
    levels = tree.levels()
    check(len(levels) >= 60, "the level index found the headers (%d)" % len(levels))
    if not levels:
        return

    incomplete = [entry.name for entry in levels if not entry.is_complete]
    check(not incomplete,
          "every level resolves to a model and an object map (%d did not: %s)"
          % (len(incomplete), incomplete[:6]))

    without_pickups = [entry.name for entry in levels if not entry.collectables_path]
    check(not without_pickups,
          "every level resolves its collectables map (%d did not: %s)"
          % (len(without_pickups), without_pickups[:6]))

    for entry in levels:
        for path in [entry.model_path] + entry.object_maps:
            if path and not os.path.isfile(path):
                FAILURES.append("%s points at a missing file: %s" % (entry.name, path))
                break

    lake = next((e for e in levels if e.label == "Ancient Lake"), None)
    check(lake is not None, "Ancient Lake is in the index")
    if lake:
        check(lake.world_label == "Dino Domain",
              "Ancient Lake is in Dino Domain (got %s)" % lake.world_label)
        check(os.path.basename(lake.model_path) == "ancient_lake.bin",
              "Ancient Lake resolves its model (got %s)"
              % os.path.basename(lake.model_path or ""))
        check(lake.objects_path != lake.collectables_path,
              "its two object maps are different files")
        print("  Ancient Lake  : %s + %s + %s"
              % (os.path.basename(lake.model_path),
                 os.path.basename(lake.objects_path),
                 os.path.basename(lake.collectables_path)))


def test_known_objects(tree):
    """Spot-check the chain against types whose look is known."""
    expectations = [
        ("ASSET_OBJECT_PALMTREETOP", "sprite", "palm_tree_top"),
        ("ASSET_OBJECT_BEACHTREE", "sprite", "beach_tree"),
        ("ASSET_OBJECT_WEAPONBALLOON", "sprite", "balloon_boost"),
        ("ASSET_OBJECT_AIRZIPPERS", "mesh", "AirZippers"),
    ]
    for object_id, expected_kind, expected_stem in expectations:
        kind, path, _header = tree.preview_for(object_id)
        name = os.path.basename(path) if path else "<none>"
        check(kind == expected_kind,
              "%s is a %s (got %s)" % (object_id, expected_kind, kind))
        check(expected_stem.lower() in name.lower(),
              "%s resolves to %s (got %s)" % (object_id, expected_stem, name))
        print("  %-32s %-7s %s" % (object_id.replace("ASSET_OBJECT_", ""), kind, name))


def test_balloon_variants(tree):
    """A weapon balloon's five sprites are selected by its balloonType field."""
    header = tree.object_header("ASSET_OBJECT_WEAPONBALLOON")
    if header is None:
        print("  skip: no weapon balloon header")
        return
    check(len(header.models) == 5,
          "the balloon header lists five sprites (got %d)" % len(header.models))
    seen = set()
    for variant in range(len(header.models)):
        _kind, path, _h = tree.preview_for("ASSET_OBJECT_WEAPONBALLOON", variant)
        if path:
            seen.add(os.path.basename(path))
    check(len(seen) == len(header.models),
          "each balloon type gets its own sprite (%d distinct)" % len(seen))
    print("  balloon sprites: %s" % ", ".join(sorted(seen)))


def main():
    tree = find_tree()
    if tree is None:
        print("SKIP: no extracted asset tree found under %s" % VANILLA)
        return 0
    print("asset tree: %s" % tree.root)
    print()

    test_object_models(tree)
    test_texture_resolution(tree)
    test_uvs(tree)
    test_resolution(tree)
    test_levels(tree)
    print()
    test_known_objects(tree)
    test_balloon_variants(tree)

    print()
    if FAILURES:
        print("FAIL: %d" % len(FAILURES))
        for line in FAILURES[:15]:
            print("  " + line)
        return 1
    print("PASS: object artwork resolves and every model decodes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
