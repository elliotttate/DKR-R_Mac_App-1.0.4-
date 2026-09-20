"""Retrato de tudo que decide qual imagem cada face da pista mostra.

No Blender: Text Editor > Open > este arquivo, mude ETAPA abaixo, Run Script.
Headless:   blender -b pista.blend --python dkr_texture_dump.py -- <etapa>

Grava <nome-do-blend>-texdump-<etapa>.json ao lado do .blend e imprime um
resumo no console (Window > Toggle System Console).
Só lê a cena; não altera nada.
"""

import hashlib
import json
import os
import sys
import time
from collections import Counter

import bpy

ETAPA = "1-antes-da-conversao"

CUSTOM_ID_BASE = 0x7000
CUSTOM_ID_COUNT = 255
NO_TEXTURE = 0xFF


def _label():
    if "--" in sys.argv:
        rest = sys.argv[sys.argv.index("--") + 1:]
        if rest:
            return rest[0]
    return ETAPA


def _blend_dir():
    return os.path.dirname(bpy.data.filepath) if bpy.data.filepath else ""


def _resolve(stored):
    if not stored:
        return ""
    if os.path.isabs(stored) or not _blend_dir():
        return stored
    return os.path.normpath(os.path.join(_blend_dir(), stored))


def _file(path):
    if not path or not os.path.isfile(path):
        return {"path": path, "exists": False}
    with open(path, "rb") as handle:
        digest = hashlib.md5(handle.read()).hexdigest()[:12]
    stat = os.stat(path)
    return {"path": path, "exists": True, "bytes": stat.st_size,
            "mtime": time.strftime("%Y-%m-%d %H:%M:%S",
                                   time.localtime(stat.st_mtime)),
            "md5": digest}


def _ordinal(texture_id):
    try:
        value = int(texture_id)
    except (TypeError, ValueError):
        return None
    if CUSTOM_ID_BASE <= value < CUSTOM_ID_BASE + CUSTOM_ID_COUNT:
        return value - CUSTOM_ID_BASE
    return None


def _records(obj, key):
    raw = obj.get(key)
    try:
        return json.loads(raw) if raw else []
    except (TypeError, ValueError):
        return ["<ilegivel>"]


def _image(image):
    if image is None:
        return None
    try:
        absolute = bpy.path.abspath(image.filepath_from_user())
    except Exception:  # noqa: BLE001
        absolute = ""
    return {
        "name": image.name,
        "filepath": image.filepath,
        "file": _file(absolute),
        "packed": bool(image.packed_file),
        "size": list(image.size),
        "users": image.users,
        "source": image.source,
    }


def _material(material):
    if material is None:
        return None
    tree = getattr(material, "node_tree", None)
    images = []
    if tree is not None:
        for node in tree.nodes:
            if node.type == "TEX_IMAGE":
                images.append({"node": node.name,
                               "linked": any(s.is_linked for s in node.outputs),
                               "image": node.image.name if node.image else None})
    return {
        "name": material.name,
        "users": material.users,
        "dkr_category": material.get("dkr_category"),
        "dkr_texture_index": material.get("dkr_texture_index"),
        "dkr_surface": material.get("dkr_surface"),
        "dkr_look": material.get("dkr_look"),
        "image_nodes": images,
    }


def _custom_textures(scene):
    settings = getattr(scene, "dkr", None)
    if settings is not None and hasattr(settings, "custom_textures"):
        rows = [{k: getattr(r, k) for k in (
            "name", "source", "png", "original", "width", "height", "format",
            "render_mode", "transparency", "nudge")}
            for r in settings.custom_textures]
    else:  # addon desligado: lê as propriedades cruas
        raw = scene.get("dkr")
        raw = raw.to_dict() if hasattr(raw, "to_dict") else (raw or {})
        rows = list(raw.get("custom_textures", []))
    out = []
    for ordinal, row in enumerate(rows):
        row = dict(row)
        row["ordinal"] = ordinal
        row["id"] = hex(CUSTOM_ID_BASE + ordinal)
        row["png_file"] = _file(_resolve(row.get("png", "")))
        row["original_file"] = _file(_resolve(row.get("original", "")))
        out.append(row)
    return out


def _mesh_object(obj, custom):
    mesh = obj.data
    slots = [_material(m) for m in mesh.materials]
    per_slot = Counter(p.material_index for p in mesh.polygons)
    entry = {
        "name": obj.name,
        "kind": ("geometria_convertida" if "dkr_geometry" in obj
                 else "mesh_original" if "dkr_converted_to" in obj
                 else "mesh_nao_convertida"),
        "hidden": obj.hide_get() if obj.name in bpy.context.view_layer.objects else None,
        "props": {k: obj.get(k) for k in obj.keys()
                  if k.startswith("dkr_") and k not in (
                      "dkr_base_textures", "dkr_extra_textures", "dkr_water")},
        "faces": len(mesh.polygons),
        "uv_layers": [layer.name for layer in mesh.uv_layers],
        "active_uv": mesh.uv_layers.active.name if mesh.uv_layers.active else None,
        "slots": [{"slot": i, "faces": per_slot.get(i, 0), "material": s}
                  for i, s in enumerate(slots)],
    }
    if "dkr_geometry" not in obj:
        return entry

    table = _records(obj, "dkr_base_textures") + _records(obj, "dkr_extra_textures")
    entry["texture_table"] = []
    for index, record in enumerate(table):
        row = dict(record) if isinstance(record, dict) else {"raw": record}
        ordinal = _ordinal(row.get("id"))
        row["index"] = index
        if ordinal is not None:
            row["own_ordinal"] = ordinal
            own = custom[ordinal] if ordinal < len(custom) else None
            row["own_name"] = own["name"] if own else "<ORDINAL INEXISTENTE>"
            row["own_png"] = own["png_file"]["path"] if own else None
        entry["texture_table"].append(row)

    # O que a face diz (atributo) contra o que o material dela mostra.
    attribute = mesh.attributes.get("dkr_texture")
    values = [0] * len(mesh.polygons)
    if attribute is not None and attribute.domain == "FACE":
        attribute.data.foreach_get("value", values)
    pairs = Counter()
    for polygon, value in zip(mesh.polygons, values):
        slot = polygon.material_index
        material = mesh.materials[slot] if slot < len(mesh.materials) else None
        pairs[(value, material.name if material else None)] += 1
    entry["face_texture_vs_material"] = [
        {"face_dkr_texture": v, "material": m, "faces": n}
        for (v, m), n in sorted(pairs.items(), key=lambda kv: (kv[0][0], str(kv[0][1])))
    ]

    problems = []
    for (value, name), count in pairs.items():
        material = bpy.data.materials.get(name) if name else None
        index = material.get("dkr_texture_index") if material else None
        expected = -1 if value == NO_TEXTURE else value
        if index is not None and int(index) != expected:
            problems.append("%d face(s) com dkr_texture=%d usam %s (dkr_texture_index=%s)"
                            % (count, value, name, index))
    for slot in entry["slots"]:
        material = slot["material"]
        if not material or material["dkr_texture_index"] in (None, -1):
            continue
        index = int(material["dkr_texture_index"])
        if index >= len(table):
            problems.append("%s aponta para a entrada %d, mas a tabela tem %d"
                            % (material["name"], index, len(table)))
            continue
        want = entry["texture_table"][index].get("own_png")
        shown = [n["image"] for n in material["image_nodes"] if n["image"]]
        if want:
            files = [_image(bpy.data.images.get(n))["file"]["path"] for n in shown]
            if not any(f and os.path.normcase(f) == os.path.normcase(want) for f in files):
                problems.append("%s (entrada %d) deveria mostrar %s, mas mostra %s"
                                % (material["name"], index, os.path.basename(want),
                                   files or "nada"))
    entry["problems"] = problems
    return entry


def main():
    scene = bpy.context.scene
    custom = _custom_textures(scene)
    folder = os.path.join(_blend_dir(), "dkr_textures")
    files = []
    if os.path.isdir(folder):
        for root, _dirs, names in os.walk(folder):
            for name in sorted(names):
                info = _file(os.path.join(root, name))
                info["path"] = os.path.relpath(info["path"], folder)
                files.append(info)

    report = {
        "etapa": _label(),
        "quando": time.strftime("%Y-%m-%d %H:%M:%S"),
        "blend": bpy.data.filepath,
        "blender": bpy.app.version_string,
        "geometry_path": getattr(getattr(scene, "dkr", None), "geometry_path", None),
        "custom_textures": custom,
        "dkr_textures_folder": files,
        "objects": [_mesh_object(o, custom) for o in scene.objects
                    if o.type == "MESH" and o.data is not None
                    and ("dkr_geometry" in o or "dkr_converted_to" in o
                         or not any(k.startswith("dkr_") for k in o.keys()))],
        "dkr_materials": [_material(m) for m in bpy.data.materials
                          if m.name.startswith("dkr ")],
        "images": [_image(i) for i in bpy.data.images
                   if i.source in {"FILE", "GENERATED"}],
    }

    target = os.path.join(
        _blend_dir() or bpy.app.tempdir,
        "%s-texdump-%s.json" % (
            os.path.splitext(os.path.basename(bpy.data.filepath))[0] or "unsaved",
            _label()))
    with open(target, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, ensure_ascii=False, default=str)

    print("\n=== dkr texture dump: %s ===" % _label())
    print("texturas proprias: %d" % len(custom))
    for obj in report["objects"]:
        print("- %s [%s] %d faces, %d slots"
              % (obj["name"], obj["kind"], obj["faces"], len(obj["slots"])))
        for problem in obj.get("problems", []):
            print("    PROBLEMA: " + problem)
    print("gravado em " + target)


main()
