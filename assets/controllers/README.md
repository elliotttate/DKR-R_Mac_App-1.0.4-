# Controller mapping database

`gamecontrollerdb.txt` is a pinned snapshot of the community-maintained
[SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB). DKR-R
loads this file before scanning connected controllers, then loads mappings
created by the in-app N64 controller setup wizard from the user's local data
directory. User mappings therefore override the bundled snapshot without
modifying the installation.

- Snapshot date: 2026-08-10
- Entries: 2,256
- SHA-256: `F40282F6B73D55700866D3E21720FF4D52590390E8548B719A3CCDDEBC2867DF`
- License: zlib; see `packaging/licenses/SDL-GAMECONTROLLERDB-LICENSE.txt`

Update this snapshot only from the upstream repository, preserve its platform
fields, update the metadata above, and run the controller mapping policy plus
Windows/Linux packaging tests before release.
