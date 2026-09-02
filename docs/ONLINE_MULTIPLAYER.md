# Online multiplayer

DKR-R supports two to four players through a native deterministic input-sync
path. Player 1 is always the host and approved guests are assigned Players 2-4.
Local multiplayer remains unchanged. Online play is player-hosted: DKR-R does
not require an account or gameplay relay. Quick Join uses a public PeerJS
rendezvous only to exchange WebRTC connection descriptions; gameplay, input
and synchronized save data remain on the authenticated peer connection.

## Quick Join

Create a lobby and share the displayed five-character
code. A guest enters that code and selects **REQUEST TO JOIN**. The rendezvous
service introduces the two copies of DKR-R, WebRTC negotiates the direct route,
and the existing encrypted admission request still waits for explicit host
approval. No account, adapter selection or port forwarding is required.

Quick Join does not use a gameplay server or relay. Both players' public IP
addresses may be visible to each other as part of direct peer routing. Some
carrier-grade, enterprise or symmetric NATs cannot establish a route using the
bundled STUN configuration. DKR-R deliberately does not expose a manual
address, adapter or port configuration fallback. Treat the code as a private
invitation. It expires when the lobby closes and no later than six hours after
creation.

## Lobby workflow

Lobby admission uses the reliable Quick Join control lane. The guest repeats
its authenticated request until the host acknowledges it, and the host repeats
the pending-approval acknowledgement until the request is accepted or declined.
If the host cannot be reached, DKR-R stops the attempt with a clear Quick Join
error instead of remaining on `Connecting` forever.

The launcher keeps the common path on **QUICK JOIN**: choose the local control
profile, create a lobby or enter a friend's code. Less common host options live
under **HOST SETTINGS**. Networking and controller-input overlay controls have
their own **OVERLAYS** tab and remain editable from the in-game settings
overlay. During a session, **LOBBY** contains approvals, the starting grid and
ready controls, while detailed telemetry is isolated under **CONNECTION**.

1. Every player selects the same supported Game Pak revision, gameplay
   settings and Magic Codes. Player 1's validated Adventure save becomes the
   session save.
   Each machine also selects the local controller/keyboard profile that will
   author its racer. That physical profile is independent from the network
   player number assigned by the host.
2. The host sets the lobby rules and selects **CREATE QUICK JOIN LOBBY**.
3. Guests enter the five-character code and select **REQUEST TO JOIN**.
4. A request receives no player slot or lobby state until the host explicitly
   selects **APPROVE**. The host may decline, block for the session, lock the
   lobby, remove a guest, or revoke and replace the invitation.
5. Every admitted player selects **READY TO RACE**. Player 1 can start only
   when at least two compatible players are present and everyone is ready.
6. Player 1 begins one host-authoritative five-second countdown. Every peer
   displays the same centred **DKR-R ONLINE STARTING IN...** notification. The
   first four ticks use the same short N64-style tone and the final tick uses
   a higher tone. Duplicate or late control packets cannot replay a tone or
   launch the game twice.
7. DKR-R launches on every machine and holds the synchronized start until all
   connected players report the same canonical cold-boot checkpoint. The game
   does not execute simulation frame 0 if any peer boots into different state.
8. By default Player 1 guides shared menus until character select. The host can
   choose a different shared-menu ownership policy before creating the lobby.
9. At character select, DKR-R creates one native game player for every occupied
   lobby slot. The host remains Player 1 and approved guests remain Players 2-4.
   Each machine's selected physical controller is routed automatically to its
   assigned online player; guests do not need to remap their controller to a
   different local port.

### Two-player Adventure

The `JOINTVENTURE` Magic Code keeps the retail game's shared-hub design: the
hub intentionally has one viewport and one visible racer, while races, bosses
and minigames use both gameplay slots. DKR-R observes which native player the
game currently owns as the shared-hub lead and routes that online racer's
authenticated input to the active local port. Online player identities never
swap. Inputs are neutralized across hub/race transition barriers, preventing a
held direction or button from leaking into the newly assigned owner.

Quick Join codes expire after six hours and become useless when the host closes the
lobby. **REKEY CODE** or **REVOKE AND REPLACE** invalidates the current admission capability immediately
without disconnecting already approved racers. Treat the code as a secret.

## Inviting friends

When a Quick Join lobby is open, Player 1 can select **INVITE FRIENDS** from
the active lobby or its dedicated host tab. Only online, unblocked friends
whose authenticated presence channel is ready are offered. The host can send
one invitation per friend, see whether it was delivered, accepted or declined,
and cancel it before it is used.

The invited racer receives the same bottom-left notification style used for
friend-online alerts. The invitation also remains in **OPEN LOBBIES** until it
is accepted, declined, cancelled or expires, so dismissing the notification
does not lose it. **ACCEPT AND JOIN** performs the normal build, ROM, save and
settings compatibility checks before joining. An accepted friend invitation
uses a random, single-use admission capability delivered only over that
friend's authenticated encrypted channel; replaying it falls back to normal
host approval. Invitations expire after five minutes and are invalidated if
the host closes, locks or rekeys the lobby. Manual five-character Quick Join
continues to require explicit host approval and is unchanged.

The **ONLINE PROFILE** privacy controls are stored with the local friend
profile. **APPEAR OFFLINE** keeps the secure presence route available while
reporting the racer as offline and withholding any hosted-lobby advertisement;
manual Quick Join remains available. **ALLOW FRIEND LOBBY INVITES** can be
disabled independently. New one-click invitations are then declined at the
authenticated friend-service boundary and never enter the notification or
Open Lobbies queues. Disabling either privacy option does not delete friends,
Friend Codes or profile identity.

## Compatibility checks

Joining fails closed unless all peers match on:

- DKR-R release and netplay protocol;
- CPU architecture and deterministic floating-point mode;
- normalised Game Pak revision and hash;
- recompilation Patch Pipeline policy;
- simulation rate, gameplay-affecting settings and Magic Codes;
- a checksum-valid Adventure save, synchronized from Player 1 after approval.

The save comparison uses a canonical checksum-valid representation. Equivalent
retail erased slots and Save Builder empty slots therefore compare identically.
If a 512-byte EEPROM contains invalid DKR checksums, DKR-R preserves an exact
backup and rebuilds only the derived checksum bytes before it is used. DKR-R
never transfers a Game Pak. If a guest's Adventure data differs, host approval
transfers Player 1's canonical 512-byte EEPROM over the authenticated peer
channel. The guest's existing EEPROM is backed up before an atomic,
checksum-validated install; no save data is sent to any third party.

## Transport and synchronization

Initial admission uses an invitation capability plus ephemeral X25519 key
agreement. Each approved peer then has its own authenticated
XChaCha20-Poly1305 session channel; the host does not reuse one symmetric
gameplay key for every guest. Quick Join carries that protocol over a reliable
WebRTC data channel protected by DTLS. Invalid, replayed, tampered or
unapproved traffic is discarded.

Ready, load and start state is repeated until observed. Automatic input delay
derives a one-way route budget from the slowest measured guest, adds
jitter/loss headroom, and freezes that value in the launch descriptor. Hosts
may select zero-to-nine frames manually when automatic delay is unsuitable.

Player 1 is the only race-state authority in both synchronization modes. After
each authored 30 Hz gameplay update, Player 1 publishes a compact portable
state for the next simulation boundary containing the simulation RNG, race
lifecycle, roster and all racer physics/progress fields. A guest installs a
complete sample only when it matches that guest's current authored boundary.
The sample corrects local prediction without advancing the simulation clock.
Graphics, audio and other presentation-only state remain local and are not
copied across machines.

The live state stream uses a bounded latest-frame-wins lane. A slow route
cannot accumulate seconds of obsolete checkpoints; missing fragments are
requested selectively from Player 1's short retained history. A damaged or
late live sample is disposable and is superseded by a later host sample rather
than halting or parking the game. Track start, synchronized recovery and finish
remain separate reliable, exact barriers. At 30 simulation frames per second
this is a few kilobytes per frame rather than an 8 MiB RDRAM or video stream,
which keeps online play practical.

The host chooses one input synchronization mode before creating the lobby:

- **Rollback** is the default and recommended internet mode. Player 1 may
  predict missing remote input inside a bounded two-to-twenty-frame window.
  When a late input differs, Player 1 restores its local checkpoint and
  deterministically replays only the affected authored simulation frames, then
  publishes the corrected live state. Replayed frames cannot submit graphics,
  audio, rumble, telemetry or save-device writes; only the final corrected
  frame is presented.
- **Lockstep** waits for the authenticated input set selected by Player 1 before
  every simulation frame. It avoids prediction and replay but directly exposes
  latency and packet variation as input delay or stalls. It is intended for
  exceptionally stable, low-latency peer connections.

The mode, input delay and rollback window are immutable launch rules and are
validated by every peer. Gameplay packets use the existing per-peer encrypted
channels. In rollback mode, GekkoNet packets are carried inside that channel
and routed through the player-hosted lobby transport; no second socket, public
service or unauthenticated data path is opened.

Before controller initialization, DKR-R installs the same four-port virtual
controller topology on every peer. The host is always virtual Player 1 and each
approved guest is routed to its assigned Player 2-4 slot. Physical input is
sampled privately, sent only for the peer's assigned slot, and is exposed to
the game only after the synchronized input set for that simulation frame has
been confirmed. Later device-discovery or latency polls cannot replace that
committed frame. A keyboard or controller cannot accidentally author a second
online player, and the lobby cannot be readied without a usable local input
source.

Peers validate a canonical cold-boot checkpoint before simulation frame 0,
then validate frontend and level-transition checkpoints. At the start of each
gameplay scene, every peer parks at a shared ready barrier. The barrier is an
incremental state machine serviced by the normal host loop: it never sleeps on
the authored game thread, so the renderer, audio device, overlay and network
worker remain responsive while another racer is loading. The host publishes
one portable frame-zero gameplay state, waits for every occupied peer to
install and acknowledge the exact bytes, and only then broadcasts the shared
resume command. Race-finish and recovery barriers use the same non-blocking
model. Rollback gameplay uses local checkpoints at the repeatable authored
`main_game_loop` boundary. Host pointers and renderer/audio sidecars never
enter the portable network state. Confirmed-state hashes remain diagnostic:
an ordinary racer/RNG difference is repaired by the next live Player 1 frame,
while a guest that cannot install that stream requests the existing explicit
shared recovery transaction.

Immediately before post-race code may delete or compact racer objects, both
modes leave speculative gameplay, publish one final portable Player 1 state,
wait for every occupied peer to install and acknowledge it, complete a shared
transition barrier, and retire gameplay recovery. This explicit finish seal
prevents late rollback from crossing into a menu whose object topology has
already changed. Rendering, audio output, the launcher and settings overlay
remain local presentation consumers; they are not streamed.

A prolonged live-state outage parks every peer at a future shared boundary and
performs one host-authored repair. Missing confirmed input, a failed repair, or
an in-race disconnect stops the session rather than allowing a divergent race
to continue. The Online MP page remains responsive so a terminal failure can
be read and acknowledged. When recording is enabled, confirmed four-player
inputs are written to `netplay/replays` below the local DKR-R configuration
directory.

Protocol 40 negotiates rollback or lockstep explicitly, carries the live
Player 1 replica stream, losslessly compresses portable authoritative state,
supports Quick Join transport bootstrap and the synchronized launch countdown,
and scopes gameplay packets
to the active scene epoch. Delayed packets from a retired scene cannot enter
the next one. Rollback is driven at
the generated thread-3 call boundary before the ordinary `main_game_loop`
call, so a replay returns normally through the same recompiled C++ stack; it
does not jump out of an active native frame. Historical replay advances only
deterministic simulation: physical input polling, renderer and audio task
submission, live audio-buffer feedback, rumble, save writes, and gameplay
lifecycle barriers remain suppressed until the final presented frame. The
Online MP page reports the current rollback frame, correction count, replayed
frames, largest correction and prediction lead for testing.

Encrypted traffic is scheduled in independent lanes. Lifecycle and recovery
control is reliable and bounded; authored input and rollback packets have a
dedicated real-time lane; recovery checkpoints have a separate non-evicting
lane; and live replicas use a latest-frame-wins lane. State hashes and lobby
telemetry cannot displace live racer input. Receive replay protection tracks
these lanes independently, so a state burst cannot make a delayed but valid
input packet appear stale.

## Quick Join troubleshooting

- Quick Join does not require an inbound firewall rule, adapter selection or
  manual port configuration.
- Verify that both players have internet access and can reach the signaling
  service, then re-enter the current five-character code.
- Verify that both players use the same current DKR-R build, supported Game Pak
  revision and gameplay settings.
- Carrier-grade, enterprise, symmetric and some mobile NATs may be unable to
  establish the direct WebRTC peer route. The current release deliberately has
  no gameplay relay.

## Current boundaries

- No players may join after loading begins.
- The host ending the session ends it for every guest.
- There is no spectator, voice-chat, text-chat, matchmaking or public browser.
- There is no bundled relay and no promise that Quick Join can traverse every
  NAT or firewall.
- Quick Join depends on the public PeerJS rendezvous being reachable for the
  initial connection. An established peer connection does not route gameplay
  through that service.
- Cross-architecture sessions remain rejected until deterministic behaviour
  has passed release certification on those architectures.
