# Third-party software and assets

DKR-R project code is distributed under `LICENSE.md`. A runtime linked with
N64ModernRuntime must also satisfy GPL-3.0; see
`runtime-recomp/COPYING-NOTICE.md`.

## Build and runtime components

| Project | Purpose | Licence |
|---|---|---|
| DavidSM64/Diddy-Kong-Racing | Matching source, ELF symbols and structures | See upstream `LICENSE.md` |
| N64Recomp | Static CPU and RSP translation | MIT |
| N64ModernRuntime | libultra-compatible host services | GPL-3.0 |
| RT64 | RDP rendering and presentation | MIT |
| GekkoNet | Pinned rollback research foundation; DKR-R currently uses its certified deterministic input-delay path while native host-stack rewind remains fail-closed | BSD-2-Clause |
| Monocypher | Authenticated encryption for online multiplayer datagrams | BSD-2-Clause or CC0 (DKR-R uses BSD-2-Clause) |
| libdatachannel | Optional WebRTC data channels and WebSocket signaling for five-character Quick Join | MPL-2.0 |
| Mbed TLS | Static TLS/DTLS provider used by Quick Join | Apache-2.0 or GPL-2.0-or-later (DKR-R uses Apache-2.0) |
| libjuice | ICE/STUN peer-route establishment used by libdatachannel | MPL-2.0 |
| usrsctp | SCTP data-channel transport used by libdatachannel | BSD-3-Clause |
| nlohmann/json | Signaling message encoding used by Quick Join | MIT |
| plog | Logging dependency used internally by libdatachannel | MPL-2.0 |
| SDL2 | Window, audio, input and controllers | Zlib |
| SDL3 | Private headless controller and Steam Deck sensor helper | Zlib |
| SDL_GameControllerDB | Community controller mappings loaded before user mappings | Zlib |
| Dear ImGui | Launcher and in-game settings UI | MIT |
| DirectX Shader Compiler | Windows shader compilation | University of Illinois/NCSA and bundled notices |

Exact dependency commits are recorded in `dependencies.lock.json`. Windows
packages include applicable notices in `ThirdPartyLicenses`. Linux AppImages
include project notices plus the copyright records and common licences for each
deployed system library under `usr/share/doc/dkr-port`.

The complete GekkoNet licence is distributed as `GEKKONET-LICENSE.txt`.
The complete Monocypher licence is distributed as `MONOCYPHER-LICENSE.txt`.
Quick Join packages also distribute the complete libdatachannel, Mbed TLS,
libjuice, usrsctp, nlohmann/json and plog licence texts. The public PeerJS
rendezvous carries signaling metadata only; DKR-R gameplay, controller input
and synchronized save data remain on the authenticated peer connection.

The controller mapping snapshot is documented in `assets/controllers/README.md`.
Its Zlib licence is distributed as `SDL-GAMECONTROLLERDB-LICENSE.txt`. Locally
created or imported mappings are stored in the user's DKR-R configuration
directory and are never written into the installed database.

## Fonts

Racing Banana is the launcher body font supplied by the project owner. It is not
extracted from Diddy Kong Racing. Its supplied source SHA-256 is
`AEADA6E5FF1388D27CC5D29DE9C67B74C0F6D851D6C772EE35376AF4E288C896`.
Public distributors are responsible for confirming redistribution permission.

Jumpman by Neale Davidson / Pixel Sagas is used for headings and the performance
overlay. Its supplied licence is packaged as `Jumpman-LICENSE.txt`; source
SHA-256 is
`36D473DF3E85F93AEECB619EF06432F4F6DCC572773AAB668D09C8AD9A04C334`.

## CRT masks

Six optional PNG display masks are included for Modern presentation. They are
not game assets. Provenance, hashes and redistribution notes are in
`assets/filters/README.md` and `packaging/licenses/CRT-FILTERS-NOTICE.md`.

Nintendo, Rare, Diddy Kong Racing and related names and assets belong to their
respective owners. DKR-R includes no game ROM or extracted game assets.
