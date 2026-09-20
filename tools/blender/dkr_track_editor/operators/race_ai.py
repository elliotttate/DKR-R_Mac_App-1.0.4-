"""The race AI in the viewport and the sidebar.

The line is :mod:`dkr_track_editor.race_ai`'s, drawn as an overlay rather than
built into scene objects: it is a view of the checkpoints, never something to
export, select or save, and an overlay follows a checkpoint while it is being
dragged where a generated curve would lag until someone rebuilt it.

Rebuilding is keyed on a signature of everything the line depends on - each
checkpoint's position, turn and lane bytes, and the vehicle set shown - so a
redraw with nothing changed costs one pass over the checkpoints and no spline.
"""

from __future__ import annotations

import traceback

import bpy
from bpy.props import EnumProperty

from .. import catalog as catalog_module, level_types, prefs, race_ai, scene

#: Lane 1 to 4, warm to cold, so the outside lanes read as the two ends of the
#: road rather than four unrelated colours.
LANE_COLOURS = (
    (0.96, 0.36, 0.30, 1.0),
    (1.00, 0.78, 0.22, 1.0),
    (0.36, 0.86, 0.42, 1.0),
    (0.32, 0.62, 1.00, 1.0),
)
LANE_NAMES = ("red", "yellow", "green", "blue")

LINE_WIDTH = 3.0
ALTERNATE_WIDTH = 2.0
ALTERNATE_ALPHA = 0.6
POINT_SIZE = 7.0

#: Retail races, the only headers whose difficulty is worth copying: hubs,
#: challenges and cutscenes carry the survey's zeros.
RETAIL_RACE_TYPE = "RACETYPE_DEFAULT"


# ---------------------------------------------------------------------------
# Reading the scene
# ---------------------------------------------------------------------------

def checkpoint_empties(context):
    """The scene's checkpoints in document order, which is spawn order - the
    order the game counts to its 60-gate ceiling in."""
    found = [obj for obj in scene.iter_dkr_objects(context)
             if str(obj.get(scene.PROP_ID)) == race_ai.CHECKPOINT]
    found.sort(key=lambda obj: (int(obj.get("dkr_order", 1 << 30)), obj.name))
    return found


def set_counts(context):
    """``{vehicleType: checkpoints}`` in the scene, without reading the rest."""
    counts = {}
    for obj in checkpoint_empties(context):
        key = int(obj.get("vehicleType", 0))
        counts[key] = counts.get(key, 0) + 1
    return counts


def vehicle_index(settings) -> int:
    names = [vehicle for vehicle, _label in level_types.PLAYER_VEHICLES]
    try:
        return names.index(settings.ai_line_vehicle)
    except ValueError:
        return 0


def vehicle_set(context) -> int:
    """The checkpoint set the shown vehicle's racers load (``header.unk4F``)."""
    return int(getattr(context.scene.dkr_ai,
                       "set_%d" % vehicle_index(context.scene.dkr)))


def read_route(context, catalog=None):
    """``(route, map_objects)`` for the vehicle the panel is showing."""
    catalog = catalog or catalog_module.load()
    objects = [scene.read_object(obj, catalog) for obj in checkpoint_empties(context)]
    return race_ai.build_route(objects, vehicle_set(context)), objects


def geometry(route):
    """The overlay's vertices, in Blender space, per lane."""
    to_blender = scene.to_blender

    def pairs(lines):
        coords = []
        for line in lines:
            points = [tuple(to_blender(point)) for point in line]
            for start, end in zip(points, points[1:]):
                coords += (start, end)
        return coords

    gates = list(route.main) + [route.alternate_of[k] for k in sorted(route.alternate_of)]
    return {
        "lanes": [pairs([race_ai.lane_line(route, lane)])
                  for lane in range(race_ai.LANES)],
        "detours": [pairs(race_ai.alternate_lines(route, lane))
                    for lane in range(race_ai.LANES)],
        "points": [[tuple(to_blender(node.lane_point(lane))) for node in gates]
                   for lane in range(race_ai.LANES)],
    }


def _plain(value):
    if hasattr(value, "to_list"):
        return tuple(value.to_list())
    return value


def _signature(context):
    items = []
    for obj in checkpoint_empties(context):
        items.append((
            obj.name,
            tuple(round(c, 4) for c in obj.matrix_world.translation),
            round(scene.world_yaw(obj), 6),
            tuple(_plain(obj.get(name)) for name in race_ai.LINE_FIELDS),
        ))
    return vehicle_set(context), tuple(items)


# ---------------------------------------------------------------------------
# Drawing
# ---------------------------------------------------------------------------

_HANDLE = None
_CACHE = {"signature": None, "geometry": None, "batches": None}
_LAST_ERROR = [None]


def _shader(*names):
    import gpu  # noqa: PLC0415 - absent in some background builds

    for name in names:
        try:
            return gpu.shader.from_builtin(name)
        except (ValueError, RuntimeError, SystemError):
            continue
    return None


def _build_batches(found):
    from gpu_extras.batch import batch_for_shader  # noqa: PLC0415

    line = _shader("POLYLINE_UNIFORM_COLOR")
    point = _shader("POINT_UNIFORM_COLOR", "UNIFORM_COLOR")
    batches = []
    for lane in range(race_ai.LANES):
        colour = LANE_COLOURS[lane]
        if line is not None and found["lanes"][lane]:
            batches.append(("line", line, batch_for_shader(
                line, "LINES", {"pos": found["lanes"][lane]}), colour, LINE_WIDTH))
        if line is not None and found["detours"][lane]:
            batches.append(("line", line, batch_for_shader(
                line, "LINES", {"pos": found["detours"][lane]}),
                colour[:3] + (ALTERNATE_ALPHA,), ALTERNATE_WIDTH))
        if point is not None and found["points"][lane]:
            batches.append(("point", point, batch_for_shader(
                point, "POINTS", {"pos": found["points"][lane]}), colour, POINT_SIZE))
    return batches


def _draw_batches(batches, region, on_top):
    import gpu  # noqa: PLC0415

    size = (float(region.width), float(region.height)) if region else (1.0, 1.0)
    gpu.state.blend_set("ALPHA")
    gpu.state.depth_test_set("NONE" if on_top else "LESS_EQUAL")
    try:
        for kind, shader, batch, colour, width in batches:
            shader.bind()
            shader.uniform_float("color", colour)
            if kind == "line":
                shader.uniform_float("viewportSize", size)
                shader.uniform_float("lineWidth", width)
            else:
                gpu.state.point_size_set(width)
            batch.draw(shader)
    finally:
        gpu.state.blend_set("NONE")
        gpu.state.depth_test_set("NONE")
        gpu.state.point_size_set(1.0)


def _draw():
    """The draw handler. It must never raise: Blender would print the
    traceback on every redraw, so a failure is printed once and then only when
    it changes."""
    context = bpy.context
    try:
        settings = getattr(context.scene, "dkr", None)
        if settings is None or not settings.show_ai_lines:
            return
        if not level_types.needs_checkpoints(level_types.current_key(settings)):
            return
        signature = _signature(context)
        if signature != _CACHE["signature"]:
            route, _objects = read_route(context)
            _CACHE.update(signature=signature, geometry=geometry(route), batches=None)
        if _CACHE["batches"] is None:
            _CACHE["batches"] = _build_batches(_CACHE["geometry"])
        _draw_batches(_CACHE["batches"], context.region, settings.ai_lines_on_top)
        _LAST_ERROR[0] = None
    except Exception:  # noqa: BLE001 - see the docstring
        message = traceback.format_exc()
        if message != _LAST_ERROR[0]:
            _LAST_ERROR[0] = message
            print("DKR track editor: could not draw the AI lines\n" + message)


def register_overlay():
    global _HANDLE
    if _HANDLE is None:
        _HANDLE = bpy.types.SpaceView3D.draw_handler_add(
            _draw, (), "WINDOW", "POST_VIEW")


def unregister_overlay():
    global _HANDLE
    if _HANDLE is not None:
        bpy.types.SpaceView3D.draw_handler_remove(_HANDLE, "WINDOW")
        _HANDLE = None
    _CACHE.update(signature=None, geometry=None, batches=None)


def is_drawing() -> bool:
    return _HANDLE is not None


# ---------------------------------------------------------------------------
# Copying a retail track's difficulty
# ---------------------------------------------------------------------------

def retail_races(context):
    tree = prefs.resolve(context)
    if tree is None:
        return []
    return sorted((level for level in tree.levels()
                   if level.race_type == RETAIL_RACE_TYPE),
                  key=lambda level: level.label)


#: Blender hands an enum callback's strings to C without taking a reference; a
#: list built fresh each call can be collected while the menu still uses it.
_ITEMS = {}


class DKR_OT_ai_copy_difficulty(bpy.types.Operator):
    """Copy a retail race's AI difficulty - the behaviour levels and every
    character's start skill - onto this track. The checkpoint sets stay: they
    name this track's own checkpoints"""

    bl_idname = "dkr.ai_copy_difficulty"
    bl_label = "Copy Difficulty From Retail"
    bl_options = {"REGISTER", "UNDO"}

    def _items(self, context):
        found = [(level.name, level.label, "") for level in retail_races(context)]
        _ITEMS["levels"] = found or [(
            "NONE", "no retail races",
            "Set the decomp assets in Preferences > Add-ons to copy from them")]
        return _ITEMS["levels"]

    level: EnumProperty(name="Track", items=_items)

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def draw(self, context):
        self.layout.label(text="Behaviour levels and start skills from:")
        self.layout.prop(self, "level", text="")

    def execute(self, context):
        from . import header as header_ops  # noqa: PLC0415
        from .level_type import _read_header  # noqa: PLC0415

        level = next((entry for entry in retail_races(context)
                      if entry.name == self.level), None)
        if level is None:
            self.report({"ERROR"}, "no retail race to copy from; set the decomp "
                                   "assets in Preferences > Add-ons")
            return {"CANCELLED"}
        found = race_ai.difficulty_from(_read_header(level.header_path) or {})
        if not found:
            self.report({"ERROR"}, "%s's header carries no AI bytes" % level.label)
            return {"CANCELLED"}
        for pointer, value in found.items():
            context.scene[header_ops.key_for(pointer)] = value
        header_ops._redraw(context)
        self.report({"INFO"}, "copied %s's difficulty (%d bytes)"
                    % (level.label, len(found)))
        return {"FINISHED"}


CLASSES = (
    DKR_OT_ai_copy_difficulty,
)
