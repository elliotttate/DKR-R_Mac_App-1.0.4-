# Balloon material authoring inputs

Final 1774 x 887 wrap maps are in `wraps/`; non-destructive Photoshop seam composites are in `editable/`. Runtime meshes and 2K baked atlases are in `assets/models/balloons/` at the repository root. The authoring assets are not bundled in the game app.

The supplied `balloons-combined.png` reference (SHA-256 `74d31ab60f214cc48d66a8550c60aaaf3a391c90ce9cae85f4cf385396e18242`) and original Meshy USDZ remain external inputs. Neither is needed to build or run the app with the checked-in baked assets.

Used the built-in image-generation tool, not the fallback CLI. Seven source maps were generated, horizontally offset by 887 pixels in Photoshop MCP, and center-seam repaired with seven targeted image edits. Photoshop composited the repair with a 16-pixel-feathered mask spanning x=177..1597, preserving the matching outer edges. All final maps retain the original adjacent source columns along their outer 64-pixel strips. Source-wrap SHA-256 values are recorded in the runtime manifest.

These are reference-guided reconstructions, not pixel-identical copies. The model material uses periodic latitude/longitude mapping with no mirrored-front projection or seam crossfade. Gloss is baked for the unlit renderer. Knots and strings are shaded separately.

## Re-bake

From the repository root, supply the original model and reference externally:

```sh
blender -b --python runtime-recomp/tools/prepare_balloon_model.py -- \
  SOURCE.usdz assets/models/balloons REVIEW.blend balloons-combined.png art-source/balloons/wraps
python3 runtime-recomp/tools/validate_balloon_model.py assets/models/balloons
```
## Original per-color generation prompts

### red

```text
Use case: precise-object-edit. Asset type: production seamless 360-degree UV texture map for a 3D game balloon. Input image is the exact visual appearance reference. Extract and unwrap the SURFACE APPEARANCE of the FIRST balloon: saturated pure red lacquer with very large lemon-yellow five-point stars, varied sizes and rotations in a dense staggered arrangement. Output ONE flat rectangular 2:1 texture map, high resolution, edge-to-edge material only. This image will be wrapped around a complete balloon: left and right edges must join seamlessly. Preserve the reference's exact brilliant palette, crisp oversized pattern shapes, smooth glossy lacquer and broad white specular reflections; carry the same quality across the entire width, including the rear. Distribute highlights across the circumference, not just one spot. For stars use about 8-10 whole sharply defined five-point stars in three staggered rows across the width, some edge-crossing stars, occupying roughly half the surface; NOT one row of identical little stars. Crucial: flat unwrapped material map, NOT a balloon object, NOT a sphere, NOT a contact sheet, NO silhouettes, knots, strings, borders, labels, text or background, NO perspective curvature. Do not invent additional motifs.
```

### green

```text
Use case: precise-object-edit. Asset type: production seamless 360-degree UV texture map for a 3D game balloon. Input image is the exact visual appearance reference. Extract and unwrap the SURFACE APPEARANCE of the SECOND balloon: saturated emerald green lacquer with very large lemon-yellow five-point stars, varied sizes and rotations in a dense staggered arrangement. Output ONE flat rectangular 2:1 texture map, high resolution, edge-to-edge material only. This image will be wrapped around a complete balloon: left and right edges must join seamlessly. Preserve the reference's exact brilliant palette, crisp oversized pattern shapes, smooth glossy lacquer and broad white specular reflections; carry the same quality across the entire width, including the rear. Distribute highlights across the circumference, not just one spot. For stars use about 8-10 whole sharply defined five-point stars in three staggered rows across the width, some edge-crossing stars, occupying roughly half the surface; NOT one row of identical little stars. Crucial: flat unwrapped material map, NOT a balloon object, NOT a sphere, NOT a contact sheet, NO silhouettes, knots, strings, borders, labels, text or background, NO perspective curvature. Do not invent additional motifs.
```

### blue

```text
Use case: precise-object-edit. Asset type: production seamless 360-degree UV texture map for a 3D game balloon. Input image is the exact visual appearance reference. Extract and unwrap the SURFACE APPEARANCE of the THIRD balloon: saturated royal blue lacquer with very large lemon-yellow five-point stars, varied sizes and rotations in a dense staggered arrangement. Output ONE flat rectangular 2:1 texture map, high resolution, edge-to-edge material only. This image will be wrapped around a complete balloon: left and right edges must join seamlessly. Preserve the reference's exact brilliant palette, crisp oversized pattern shapes, smooth glossy lacquer and broad white specular reflections; carry the same quality across the entire width, including the rear. Distribute highlights across the circumference, not just one spot. For stars use about 8-10 whole sharply defined five-point stars in three staggered rows across the width, some edge-crossing stars, occupying roughly half the surface; NOT one row of identical little stars. Crucial: flat unwrapped material map, NOT a balloon object, NOT a sphere, NOT a contact sheet, NO silhouettes, knots, strings, borders, labels, text or background, NO perspective curvature. Do not invent additional motifs.
```

### shield

```text
Use case: precise-object-edit. Asset type: production seamless 360-degree UV texture map for a 3D game balloon. Input image is the exact visual appearance reference. Extract and unwrap the SURFACE APPEARANCE of the FOURTH balloon: a brilliant lemon-yellow field with very large vivid violet-purple five-point stars, varied sizes and rotations in a dense staggered arrangement. Output ONE flat rectangular 2:1 texture map, high resolution, edge-to-edge material only. This image will be wrapped around a complete balloon: left and right edges must join seamlessly. Preserve the reference's exact brilliant palette, crisp oversized pattern shapes, smooth glossy lacquer and broad white specular reflections; carry the same quality across the entire width, including the rear. Distribute highlights across the circumference, not just one spot. For stars use about 8-10 whole sharply defined five-point stars in three staggered rows across the width, some edge-crossing stars, occupying roughly half the surface; NOT one row of identical little stars. Crucial: flat unwrapped material map, NOT a balloon object, NOT a sphere, NOT a contact sheet, NO silhouettes, knots, strings, borders, labels, text or background, NO perspective curvature. Do not invent additional motifs.
```

### rainbow

```text
Use case: precise-object-edit. Asset type: production seamless 360-degree UV texture map for a 3D game balloon. Input image is the exact visual appearance reference. Extract and unwrap the SURFACE APPEARANCE of the FIFTH balloon: sharply edged glossy wavy horizontal color bands, TOP TO BOTTOM green then royal blue then red then yellow then green. Wave amplitude about 8 percent of image height, four smoothly sinusoidal waves across the width. No pink, purple, gradients, stars, or additional motifs. Output ONE flat rectangular 2:1 texture map, high resolution, edge-to-edge material only. This image will be wrapped around a complete balloon: left and right edges must join seamlessly. Preserve the reference's exact brilliant palette, crisp oversized pattern shapes, smooth glossy lacquer and broad white specular reflections; carry the same quality across the entire width, including the rear. Distribute highlights across the circumference, not just one spot. For stars use about 8-10 whole sharply defined five-point stars in three staggered rows across the width, some edge-crossing stars, occupying roughly half the surface; NOT one row of identical little stars. Crucial: flat unwrapped material map, NOT a balloon object, NOT a sphere, NOT a contact sheet, NO silhouettes, knots, strings, borders, labels, text or background, NO perspective curvature. Do not invent additional motifs.
```

### silver

```text
Use case: precise-object-edit. Asset type: production seamless 360-degree UV texture map for a 3D game balloon. Input image is the exact visual appearance reference. Extract and unwrap the SURFACE APPEARANCE of the SIXTH balloon: bright icy silver mirrored square tiles, thin cobalt-blue grid lines, intense white and sky-blue window-shaped reflections with a few deeper navy reflected patches. The grid is straight and evenly spaced, 28 columns and 13 rows. These are reflective mirror tiles, not opaque blue checkerboard squares. Output ONE flat rectangular 2:1 texture map, high resolution, edge-to-edge material only. This image will be wrapped around a complete balloon: left and right edges must join seamlessly. Preserve the reference's exact brilliant palette, crisp oversized pattern shapes, smooth glossy lacquer and broad white specular reflections; carry the same quality across the entire width, including the rear. Distribute highlights across the circumference, not just one spot. For stars use about 8-10 whole sharply defined five-point stars in three staggered rows across the width, some edge-crossing stars, occupying roughly half the surface; NOT one row of identical little stars. Crucial: flat unwrapped material map, NOT a balloon object, NOT a sphere, NOT a contact sheet, NO silhouettes, knots, strings, borders, labels, text or background, NO perspective curvature. Do not invent additional motifs.
```

### gold

```text
Use case: precise-object-edit. Asset type: production seamless 360-degree UV texture map for a 3D game balloon. Input image is the exact visual appearance reference. Extract and unwrap the SURFACE APPEARANCE of the SEVENTH balloon: highly glossy metallic gold, pale yellow above and orange gold below, a thick pure royal-blue W zigzag stripe centered horizontally at 55 percent height. Four full W chevron cycles across the width. A separate row of small downward-pointing blue triangles ABOVE the stripe, and separate row of upward-pointing blue triangles BELOW it, with visible gold gaps separating the little triangles from the main stripe. No diamonds, no crossing lines, no purple, no stars. Output ONE flat rectangular 2:1 texture map, high resolution, edge-to-edge material only. This image will be wrapped around a complete balloon: left and right edges must join seamlessly. Preserve the reference's exact brilliant palette, crisp oversized pattern shapes, smooth glossy lacquer and broad white specular reflections; carry the same quality across the entire width, including the rear. Distribute highlights across the circumference, not just one spot. For stars use about 8-10 whole sharply defined five-point stars in three staggered rows across the width, some edge-crossing stars, occupying roughly half the surface; NOT one row of identical little stars. Crucial: flat unwrapped material map, NOT a balloon object, NOT a sphere, NOT a contact sheet, NO silhouettes, knots, strings, borders, labels, text or background, NO perspective curvature. Do not invent additional motifs.
```

## Center-seam edit prompt (all seven offset maps)

```text
Use case: precise-object-edit. Edit target: the attached flat 2:1 balloon texture map. Repair ONLY the vertical join at the EXACT CENTER of the image (x=50 percent). The image was offset horizontally by half its width for seamless texture repair. Retouch the central 12 percent wide strip so there is no vertical lighting discontinuity, no jagged or clipped motif edges and no mismatched color at the join. Reconstruct any stars crossing the join into complete clean FIVE-point stars; for the wavy bands and zigzag stripe, join each edge smoothly and accurately; for silver grid, preserve continuous straight grid lines. Keep all existing colors, glossy white reflections, motif sizes, sharp boundaries, flat rectangle layout, and every pixel outside that central strip unchanged. Keep the extreme left and right image edges EXACTLY unchanged; those edges already match perfectly and will wrap together. Do not redraw the whole texture, do not add objects, borders, text, backgrounds, shading changes, or new motifs. Output the same full rectangular 2:1 texture with the center seam repaired.
```
