# DKR level object map format

Reference for writing an **encoder** for the `LEVEL_OBJECT_MAPS` section - the
bytes that place every object in a track.

Everything below was verified by decoding the retail `assets.bin` and comparing
against the decomp's extracted glTFs: **136 of 136 object maps, every entry,
type and position matching exactly**. The verification recipe is at the end, and
it works in reverse: an encoder is correct when the bytes it produces from a
glTF equal the bytes already in `assets.bin`.

Companion: `docs/LEVEL_MODEL_FORMAT.md` (track geometry), `docs/CUSTOM_TRACKS.md`
(the runtime side).

## Finding the section

`assets.lut.bin` is a section table: one `u32` big endian count, then `count + 1`
big-endian offsets into `assets.bin`. Section `i` spans `offsets[i]` to
`offsets[i + 1]` - the same size-by-difference convention the rest of the format
uses.

```text
0x00        u32 BE   section count            (50 in the US build)
0x04..      u32 BE   offsets[count + 1]       (51 entries, 208 bytes total)
```

Indices come from `AssetSectionsEnum` in `include/asset_enums.h`:

| Index | Section |
|---|---|
| 20 | `ASSET_LEVEL_OBJECT_MAPS_TABLE` |
| 21 | `ASSET_LEVEL_OBJECT_MAPS` |
| 35 | `ASSET_LEVEL_OBJECT_TRANSLATION_TABLE` |

The **table** section is an array of `u32` big-endian offsets *relative to the
start of the maps section*, again with a terminator, so map `n` occupies
`table[n] .. table[n + 1]`. In the US build the table holds 140 entries; only the
ones the asset manifest names are real maps, and reading past them gives
nonsense.

## Container

Each map is compressed with the same five-byte container as a level model:

```text
bytes 0..3   decompressed size, u32 LITTLE endian
byte  4      0x09
bytes 5..    raw DEFLATE (no zlib or gzip wrapper)
```

Python: `zlib.decompress(data[5:], -15)`.

Everything past the container is N64 data, so **big endian**.

## Map header

From `fileTypes/levelObjectMap.hpp` in the asset tool:

```c
struct LevelObjectMapHeader {   // 16 bytes
    /* 0x00 */ be_uint32_t fileSize;
    /* 0x04 */ uint8_t pad4[12];
};
```

**`fileSize` counts only the entry bytes, not the header.** This is the single
easiest thing to get wrong, and it fails quietly: reading `while offset <
fileSize` instead of `while offset < 0x10 + fileSize` drops the last entry of the
map, which looks like an off-by-one in your own code rather than a
misunderstanding of the field.

So:

```text
entries occupy   [0x10, 0x10 + fileSize)
sum of entry sizes == fileSize          (holds for all 136 retail maps)
```

The decompressed blob is then **padded to a multiple of 8** with zero bytes.
Across the retail maps the tail is 0, 2, 4 or 6 bytes and is always zero-filled:

```text
blob_length = align8(16 + fileSize)
```

## Entry encoding

Every entry starts with the same eight bytes:

```c
typedef struct LevelObjectEntryCommon {
    u8 objectID;      // 9-bit object id: uses size's MSB as its top bit
    u8 size;          // 7-bit total entry length, including these 8 bytes
    s16 x, y, z;      // position, big endian
} LevelObjectEntryCommon;
```

The two fields are packed:

```python
object_id = byte0 | ((byte1 & 0x80) << 1)     # 0..511
size      = byte1 & 0x7F                       # 8..127, always even
```

and written back as:

```python
byte0 = object_id & 0xFF
byte1 = (size & 0x7F) | ((object_id >> 1) & 0x80)
```

`size` is the whole entry, common header included, so the type-specific fields
occupy `size - 8` bytes. Entry sizes observed across every retail map:

```text
8, 10, 12, 14, 16, 18, 20, 22, 26, 28, 30, 50
```

All even. Note that they are **not** all multiples of four, so an encoder must
not over-align.

**The usable limit is 63, not 127.** `track_spawn_objects` in
`src/objects.c` walks entries with

```c
gObjectMapSpawnList[index] = &gObjectMapSpawnList[index][temp_t3 = gObjectMapSpawnList[index][1] & 0x3F];
```

`& 0x3F` is six bits. An entry of 64 bytes or more would be advanced past at the
wrong stride and every entry after it misread. The asset tool writes seven bits,
so the two disagree - it never bites in shipped data because the largest retail
entry is 50 bytes, but an encoder should enforce 63.

## Object ids go through the translation table

`objectID` is **not** an index into `ASSET_OBJECTS`. It indexes
`ASSET_LEVEL_OBJECT_TRANSLATION_TABLE`, extracted as
`objects/level_object_translation_table.json`:

```json
{ "table": ["ASSET_OBJECT_RACER", "...", "ASSET_OBJECT_SETUPPOINT", "..."],
  "type": "LevelObjectTranslationTable" }
```

305 entries in the US build, and it is **global**, not per level - the same table
serves every map. Confirmed against the retail bytes:

| objectID | resolves to |
|---|---|
| 18 | `ASSET_OBJECT_CAMERA_CONTROL` |
| 20 | `ASSET_OBJECT_SETUPPOINT` |
| 24 | `ASSET_OBJECT_CHECKPOINT` |
| 35 | `ASSET_OBJECT_BEACHTREE` |
| 73 | `ASSET_OBJECT_BLUEBERRYBUSH` |

An encoder placing an object type not already in the table has to extend it,
which means the table becomes a second payload the track has to ship.

## Type-specific fields

Layouts come from `include/level_object_entries.h`. Fields are written at the
offsets the `/* 0x?? */` markers give, big endian, using the declared C type.

The `Hint(...)` annotations say how the human-readable glTF value maps to the
raw bytes. The authority is `helpers/c/cStructGltfHelper.cpp`; these are the
encode directions:

### `Hint((Angle, DivideBy:N))`

```c
if (angle < 0 && type is unsigned) {
    angle += (max_int(type) + 1) / N * 360;   // wrap back to unsigned
}
raw = clamp(type, round(angle / 360 * N));
```

Decoding is the mirror, and only wraps negative when the value exceeds 360:

```c
angle = value / N * 360;
if (angle > 360 && type is unsigned) {
    angle -= (max_int(type) + 1) / N * 360;
}
```

That asymmetry matters. A `u8` with `DivideBy:64` yields 0..360 **and**
-1074..-5.625, while the same type with `DivideBy:256` never exceeds 360 and so
is never wrapped, giving 0..358.59. Assuming one signedness for all angle fields
clips legal values.

### `Hint((Scale, DivideBy:N))`

```c
raw = round(scale * N);      // encode
scale = value / N;           // decode
```

### `Hint((Enum:Name))`

The glTF stores the member name. Encoding looks up the member's **declared
value** in that enum, not its position in a sorted list. Enums with explicit
values (`VEHICLE_NO_OVERRIDE = -1`, `WARP_FLAG_NORMAL = -1`) make this a real
distinction.

### `Hint((AssetId:SECTION))`

The glTF stores the asset's build id. Encoding is the index of that id within the
section; an **empty string encodes as -1**, and decoding produces an empty string
when the value is -1 or past the section's count. That is the mechanism behind
optional fields: a field whose value has no representation is simply absent from
the glTF's `extras`, and must be written back as -1 rather than as 0.

### `Hint((Object))`

**Not** an asset section: it indexes the same level-object translation table the
entry's own `objectID` does. An Animation's `objectIdToSpawn` names the object it
spawns. An empty string encodes as -1.

### `Hint((Time, RoundToPlaces:N))`

```c
raw = round(seconds * 60);    // encode; 60 is max_tickrate
seconds = value / 60;         // decode, then rounded to N places
```

### Absence is -1, not 0

`put_struct_entry_into_gltf_node_extra` omits a field from the glTF in exactly
three cases, and all three use -1 as the sentinel:

| Hint | Omitted when |
|---|---|
| `AssetId` | the build id resolves to nothing (-1, or past the section) |
| `Object` | the object index is negative |
| `Time` | the value is already -1 |

So a field missing from `extras` has to be written back as **-1**. Writing 0
makes an Animation spawn object 0 and pause for zero frames rather than doing
neither, which is exactly the kind of wrong that looks like correct data.

### Truncate, do not saturate

`CTypes::clamp_int` is called from one place only - `get_value_from_hint_angle`.
Everywhere else the integer is written into the field and truncates. The
difference shows on the -1 sentinel: an absent `AssetId` on a `u8` must come out
`0xFF`, and saturating would make it 0, pointing an exit at the first level
instead of at nothing.

### Everything else

Written as its declared C type, big endian, at its marked offset. Arrays
(`u8 pad12[5]`, `u8 adjacent[4]`) are contiguous.

## Gotchas found while verifying

**The header has offset-marker typos.** `LevelObjectEntry_AudioReverb` marks two
consecutive fields as `0x0A`:

```c
/* 0x0A */ u8 vertexIndex;
/* 0x0A */ u8 unkB;          // actually 0x0B
```

Trusting the marker gives an 11-byte entry; the retail bytes say 12. Track a
running cursor and ignore a marker that would overlap the previous field.

**Matching a type to a struct by field names alone is not enough.**
`ASSET_OBJECT_FIREBALLATTRACT` has `unk8` as a 4-element array; matching on names
picks `LevelObjectEntry_AudioSeqLine`, whose `unk8` is `u8[0xC]`, giving a
20-byte entry where the retail bytes say 12. Compare array lengths too.

**`assets.bin` is not necessarily the revision you extracted.** The `assets.bin`
in this checkout matches `us.v80`, not `us.v77`: against v77 three maps differ,
against v80 all 136 match. Comparing against the wrong revision produces a
handful of position mismatches that look like encoder bugs and are not.

**One header field is little endian.** Not an object-map matter, but the same
class of trap and worth knowing if you touch `LEVEL_HEADERS`: offset 0xC4
(`unkC4`) is written in the build machine's byte order, alone in the 200-byte
header, because `levelHeader.hpp` declares it a plain `uint32_t` where every
other multi-byte member is `be_*`.

It is easy to conclude the opposite from one revision. In `us.v77` the bytes are
`00 00 07 a8`, which reads as a perfectly ordinary small big-endian value - but
the extraction records `0xA8070000` for all 65 headers, and only a little-endian
write reproduces the ROM. `us.v80` holds `10 05 d3 58` from `0x58D30510`, the
same rule. Checking the extracted JSON settles it without a second `assets.bin`,
since the extractor and the builder share one struct definition.

**One table entry is not a map.** Index 138 in the US build decompresses to
garbage - its container tag is 0x00, not 0x09. Bound the loop by the asset
manifest's `order` list rather than by the table length.

## A level has two of these

Every level header names two object maps, and the game loads them into a
two-element array:

```c
track_spawn_objects(header->unkBA,       0);   // offset 0xBA
track_spawn_objects(header->collectables, 1);  // offset 0x36
```

Both are spawned identically - the index is only which of `gObjectMap[2]` holds
it - so the split is **not semantic**. Measured across all 65 retail levels, 39
of the 85 object types appear in both maps, many near half and half: checkpoints
are 91% in the first, AI nodes 75% in the second. The conventional names
"structure" and "collectables" describe the usual contents, not a rule.

An encoder therefore cannot decide which map an object belongs to. It has to be
told.

**Neither slot is optional.** The bounds check is

```c
for (i = 0; objMapTable[i] != 0xFFFFFFFF; i++) {}
i--;
if (mapID >= i) { mapID = 0; }
```

so zero is a valid index and there is no representable "no map" in the field.
A level that supplies only one map has the other pointing at object map 0 -
some other level's layer, spawned over this track. An **empty** map is how a
track says it has none: 16 bytes, `fileSize` 0, and 16 retail maps are exactly
that.

Note also that `mapID` and `i` are both `s32`, so the comparison is signed: -1
does not trip the clamp and reads `objMapTable[-1]`, one word before the table.

## Verification recipe

This is the part that makes an encoder trustworthy: the answer already exists in
`assets.bin`, so correctness is a byte comparison rather than a play test.

```python
import json, os, struct, zlib

ASSETS = "extern/dkr-decomp/assets"
TREE   = ASSETS + "/.vanilla/us.v80"          # must match assets.bin

lut  = open(ASSETS + "/assets.lut.bin", "rb").read()
offs = [struct.unpack_from(">I", lut, 4 + i * 4)[0]
        for i in range((len(lut) - 4) // 4)]
blob = open(ASSETS + "/assets.bin", "rb").read()

# ASSET_LEVEL_OBJECT_MAPS_TABLE = 20, ASSET_LEVEL_OBJECT_MAPS = 21
start, end = offs[20], offs[21]
table = [struct.unpack_from(">I", blob, start + i * 4)[0]
         for i in range((end - start) // 4)]
base  = offs[21]

meta  = json.load(open(TREE + "/asset_level_object_maps.meta.json"))["files"]
table_of = json.load(
    open(TREE + "/objects/level_object_translation_table.json"))["table"]

for index, asset_id in enumerate(meta["order"]):
    raw = blob[base + table[index]: base + table[index + 1]]
    if len(raw) < 5 or raw[4] != 0x09:
        continue                                   # not a map
    data = zlib.decompress(raw[5:], -15)
    file_size, = struct.unpack_from(">I", data, 0)

    offset, stop = 0x10, 0x10 + file_size
    while offset < stop:
        b0, b1 = data[offset], data[offset + 1]
        object_id = b0 | ((b1 & 0x80) << 1)
        size      = b1 & 0x7F
        x, y, z   = struct.unpack_from(">3h", data, offset + 2)
        # table_of[object_id] is the ASSET_OBJECT_* name;
        # (x, y, z) is the glTF node's translation, exactly.
        offset += size
    assert offset == stop
```

Encoding is the inverse: emit each glTF node in document order, look its
`extras.id` up in the translation table, write the packed id and size plus the
position, then the type's fields; set `fileSize` to the total entry bytes and
zero-pad the blob to a multiple of 8; DEFLATE it and prepend
`[u32 LE decompressed size][0x09]`.

If the result is byte-identical to the slice you pulled out of `assets.bin`, the
encoder is right. All 136 retail maps are available as test cases.

## What is verified and what is not

Verified against retail bytes: the container, the section and table layout, the
header, `fileSize` semantics, the padding rule, the packed id/size, positions,
and the translation-table indirection - all 136 maps, every entry.

Not verified here: writing a **new** map back into `assets.bin`, which needs the
table offsets recomputed and every later section shifted; and extending the
translation table for object types a track adds. Both are encoder-side problems
this document does not cover.
