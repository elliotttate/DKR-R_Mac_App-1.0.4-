# Asset and ROM policy

## Never commit or distribute

- Game ROMs in any byte order.
- Generated `dkr.o2r` files containing original resources.
- Extracted textures, palettes, models, maps, game fonts, audio or cutscenes.
- Save files or Controller Pak dumps containing user data.
- Patches that embed substantial original binary data.

## Allowed repository content

- Original port source and tests.
- Decomp source under its own applicable licence when integrated correctly.
- Extraction schemas, offsets and declarative metadata.
- Original port interface assets.
- The supplied SR.GU replacement artwork in `assets/texture-packs/srgu-remastered`,
  included at the project owner's request with attribution, source/output hashes
  and conversion records. See `packaging/licenses/SRGU-TEXTURE-PACK-NOTICE.txt`;
  the source-code license does not relicense the artwork.
- Independently supplied interface fonts only when their redistribution terms
  are documented in `THIRD_PARTY.md` and permit the intended release.
- Synthetic test geometry and data.
- Checksums identifying supported user-supplied ROM revisions.

## Safeguards

Run before every commit:

```bash
python scripts/scan_for_game_assets.py
```

CI runs the same scan. It rejects N64 ROM extensions, generated O2R archives, Nintendo 64 ROM magic
at the start of binary files and unexplained large files.

## Contributor responsibility

Do not open a pull request containing a ROM or extracted game material, even temporarily. Removing a
file in a later commit does not remove it from Git history.
