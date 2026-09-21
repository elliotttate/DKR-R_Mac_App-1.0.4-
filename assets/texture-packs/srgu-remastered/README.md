# DKR REMASTERED (SR.GU's)

HD replacement artwork by **SR.GU (sr.gu)**, supplied for this fork's release.
The prepared pack contains **1,741 texture identities**. It is enabled by
default in Modern mode; disable or hide it in the Textures library. Accurate
mode uses original textures. The six 3D model families use their own atlases.

The release reads these files directly from its assets directory. It needs no
separate download, import step, storage authorization or external runtime.
Settings remain in the user's profile; the installed pack is read-only.

`manifest.json` records every selected source path/hash and output image hash.
Of the 1,960 supplied PNGs, 1,957 have Rice identities. `_all` takes precedence
where variants coexist, 129 RGB/alpha pairs are merged using the alpha image's
red channel, and five RGB-only textures receive opaque alpha. The stray space
before `.png` in `51F45E32#0#3_all .png` is normalized. Three unaddressed reference
images, `Thumbs.db`, and the nested Banjo ZIP are excluded; both ZIP images
are byte-identical to their loose PNGs. These exclusions are listed in the
manifest. All texture pixels selected from `_all` files remain unchanged.

RT64 aliases use the native `rice_texture_pack_policy.hpp` algorithm. Legacy
Rice coordinates use `defaultShift: none` to preserve sliced title artwork.

Regenerate into a new directory (requires Python and Pillow):

```sh
python3 scripts/prepare_srgu_texture_pack.py --source "/path/to/DKR REMASTERED (SR.GU's)" --output /tmp/srgu-prepared
```

Use double quotes around a source path containing an apostrophe. Ordinary builds
verify the committed PNGs and metadata with Python's standard library; they do
not require Pillow. The game itself requires neither.

The supplied folder contained no separate license or additional notices. Credit
remains with SR.GU and the respective rights holders; the code license does not
relicense this artwork. See `packaging/licenses/SRGU-TEXTURE-PACK-NOTICE.txt`.
