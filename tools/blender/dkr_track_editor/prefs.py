"""Where the addon looks for the extracted decomp assets.

Object artwork - the sprite for a coin, the mesh for a frog - lives in an
extracted decomp asset tree, not inside the addon. So the addon has to know
where that tree is before it can draw anything, and it has to know from the
moment it is enabled: pressing *Coin* must produce a coin straight away, not
only after an object map has been imported from somewhere inside the tree.

Three places are consulted, in order:

1. the scene, set when a map or a level model was imported from inside a tree
2. this addon preference, which the author sets once and which persists
3. a search of the usual locations, which finds it with no setup at all when
   the addon is run from a checkout of this repository
"""

from __future__ import annotations

import os

import bpy
from bpy.props import StringProperty

from . import assets

#: Cached between calls so placing a hundred objects does not re-scan the disk.
_resolved = {"key": None, "tree": None}


def package_name() -> str:
    """The identifier Blender registered this addon under."""
    return __package__ or "dkr_track_editor"


class DKR_AddonPreferences(bpy.types.AddonPreferences):
    bl_idname = package_name()

    asset_root: StringProperty(
        name="Decomp Assets",
        description=(
            "Extracted decomp asset version directory, the one holding "
            "asset_objects.meta.json - for example "
            "extern/dkr-decomp/assets/.vanilla/us.v77. Point this at your "
            "decomp and every object is drawn with its real sprite or model"
        ),
        default="",
        subtype="DIR_PATH",
        update=lambda self, context: invalidate(),
    )

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "asset_root")

        tree = resolve(context)
        box = layout.box()
        if tree is None:
            box.label(text="No extracted asset tree found", icon="ERROR")
            box.label(text="Objects will be drawn as plain markers.")
            box.label(text="Run the decomp's extract.sh, then point the path above")
            box.label(text="at assets/.vanilla/<region>.<version>/")
        else:
            box.label(text="Using %s" % tree.label, icon="CHECKMARK")
            box.label(text=tree.root)


def preference_root() -> str:
    """The configured path, or empty when the addon is not registered."""
    try:
        addon = bpy.context.preferences.addons.get(package_name())
        if addon is not None and addon.preferences is not None:
            return bpy.path.abspath(addon.preferences.asset_root or "")
    except (AttributeError, KeyError):
        pass
    return ""


def search_hints() -> list:
    """Places worth looking in when nothing has been configured."""
    hints = []

    # A checkout of this repository, when the addon runs from tools/blender.
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    hints.append(repo)

    # Next to the .blend the author is working in.
    if bpy.data.filepath:
        hints.append(os.path.dirname(bpy.data.filepath))

    return hints


def resolve(context=None):
    """The asset tree to draw objects with, or ``None``.

    Cached on what it was resolved from, so the answer is recomputed when the
    author changes the preference or imports from a different tree, and not on
    every one of a few hundred placements.
    """
    context = context or bpy.context
    scene_root = ""
    try:
        scene_root = bpy.path.abspath(context.scene.dkr.asset_root or "")
    except AttributeError:
        pass

    key = (scene_root, preference_root(), bpy.data.filepath)
    if _resolved["key"] == key and _resolved["tree"] is not None:
        return _resolved["tree"]

    tree = assets.AssetTree.discover(scene_root, preference_root(), *search_hints())
    _resolved["key"] = key
    _resolved["tree"] = tree
    return tree


def invalidate():
    """Forget the cached answer; the configured path changed."""
    _resolved["key"] = None
    _resolved["tree"] = None
    from . import preview, textures

    preview.clear_cache()
    # The 3D texture catalogue is keyed by tree root, and the indices in it are
    # that extraction's, so pointing the addon at another one has to drop it.
    textures.clear_cache()


CLASSES = (DKR_AddonPreferences,)
