# DKR-R v1.0.5 Beta 3: Online input delivery and boss introductions

8 September 2026. This is a beta for user testing, not a declaration that every
Internet connection, device or gameplay regression gate has passed.

## Beta 3: boss topology and client input freshness

All racers must use Beta 3 (gameplay protocol 47). Friend protocol 4, receipt
extension 1 and profile schema 6 are unchanged. Keep the accepted Beta 2
friend-delivery/save-status package separately for rollback.

- A boss request can legitimately load a one-racer HUBWORLD-type fly-in before
  the boss arena. The existing topology gate now verifies that exact redirect
  using ASSET_MISC_67 from the active ROM. Boss arenas still require two racers;
  normal Adventure races still require six, and challenges four. No controller
  remapping, split-screen change or barrier bypass was introduced.
- One canonical sample is retained for each local input frame. Updating a
  pending future target replaces it instead of appending a duplicate that the
  live/repair encoders would read in the wrong order. Each sample has a revision;
  older live packets and reliable repairs cannot overwrite a newer revision.
  Published frame commits remain immutable. Scene changes retire the history
  and revision state together.
- Only the newest unsent rolling input batch and cumulative acknowledgement
  are kept per destination. Disposable input/progress/orientation packets expire
  after 250 ms in the application queue. Exact input history remains available
  to the reliable repair path; commits, saves and lifecycle messages never expire
  by this rule. This cannot remove packets already buffered inside an OS/network
  transport, so route congestion still matters.
- Reaching the gameplay prediction limit yields a nonblocking Pending result
  instead of committing unlimited guessed controls. Reliable repair remains
  active. The existing menu, loading, finish, recovery and overlay paths remain
  in place. Both synchronization modes can repair an authenticated missing
  unproduced sample using the last known pad state, without polling SDL from
  the network worker or resuming a suspended scene.
- The manual-delay and rollback-slider ranges are unchanged. Automatic delay is
  offered explicitly, not silently enabled. The lobby warns when manual delay
  is below the current route estimate. Delay is frozen in the launch descriptor,
  so changing the setting is for the next lobby, not an ongoing race.

The detailed network overlay includes local sample-to-host-commit echo and
sample-to-local-consumption timings, a measured sample count, refreshed sample
count, old/duplicate revisions, and prediction-limit waits. Timings use only
one machine's monotonic clock, exclude hold-last placeholders and predicted or
nonmatching commits, and are last-sample diagnostics rather than a guarantee
of end-to-end display latency. The bounded failure recorder samples these
timings every 30 authored frames without per-frame disk writes.

The new deterministic transport-clock tests exercise the production
DirectSession input/repair/commit codecs and handlers, including encrypted
datagrams and hash-chain consumption. Cases cover 2/3/4 players, manual delay
1/window 2, automatic delay, 0/20/60/100/150/200 ms base round trips, asymmetric routes, jitter,
reordering, realtime loss, a 500 ms send blockage and a 200 ms client scheduling
pause. The expanded 120-case matrix includes both synchronization modes and
UDP/Quick Join commit-history policies. It asserts six held button edges reach each client without duplication,
bounded queues/history and recovery. They are not real WAN, GPU or Steam Deck
measurements; the existing real-thread/socket/lifecycle suites complement them.

For distant or unstable routes, try automatic delay first. With immutable
host-authoritative prediction, a one-frame deadline cannot cover a 200 ms
round trip: respecting the window must sometimes slow simulation instead of
discarding clients' controls indefinitely. Beta 3 does not add native-world
rollback, a relay, or a promise of zero latency on every connection.

## Friend delivery and lobby save-status rebuild (7 September 2026)

The Beta 2 changes retained below used the matching **friend-delivery-save-status** rebuild for
confirmed delivery and complete lobby status. Gameplay protocol 46, rollback,
input scheduling, isolated online save storage, rendering and controls are
unchanged. No dependency source was modified in this pass.

Friend-code registrations now receive their own application heartbeat every
five seconds; the separate presence connection cannot keep those registrations
alive. A 15-second registration watchdog, bounded reconnect backoff and stale
socket/callback guards recover missing registration acknowledgements and failed
routes. Early ICE candidates are retained in a bounded, expiring queue until
their matching peer description exists. Failed sends/overflow retain durable
requests and rebuild connections rather than silently losing delivery work.

New peers negotiate receipt extension 1 on the existing social protocol 4.
Requests carry a durable transaction ID and are replayed even over an existing
authenticated connection. The receiver confirms delivery only after saving the
request. Cancel/re-add, duplicate packets, several codes for one owner and late
decisions are scoped to their transaction instead of only the permanent code.
Decision replay is batched and rotated. Older peers retain their legacy flow,
but the UI explicitly calls their delivery unconfirmed. Both racers must run
this rebuild to obtain the new receipt guarantee.

The UI distinguishes local queuing, connection, identity verification and
confirmed receipt. Retry Now is rate-limited and reuses the pending request;
Cancel remains immediate locally. Each shareable code shows registration
health. Copy Friend Connection Diagnostics supplies a bounded, redacted event
history without codes, names, identities, keys or addresses. This does not add
a backend, offline mailbox or TURN relay: both peers must eventually be online
and able to establish the existing direct route. Restrictive NAT/firewalls can
still prevent it; the UI must not report success in that case.

Friend profile schema 6 preserves the same identity and existing requests,
friends, nicknames, blocks and codes. The first write after migration retains
the old file as `friends-v1.ini.pre-receipts`, in addition to normal recovery
backups. If rolling back to an older executable, close DKR-R first and restore
that pre-migration profile deliberately; older builds cannot read schema 6.
Changes made after that backup would not be present in the restored profile.
Never share profile files: they contain private identity material.

The lobby fix addresses a separate reproducible display omission: clients
previously populated only their own save-verification flag, leaving other
cards permanently yellow. The host now repeats an authenticated advisory
bitmap tied to the exact save hash, save generation and roster generation.
Clients never accept it before local installation. Old/reordered status cannot
transfer verification to a new occupant of a reused slot; a future status waits
for its matching roster. This confirmation can also repair a lost individual
ReadyAck. Existing Ready/Ack and host launch gates remain intact. Older builds
discard the optional status message safely. The advisory send queue keeps only
the latest unsent status for each destination and is silent during gameplay.

Regression coverage includes unpersisted/lost receipts, reconnect and
cancel/re-add, stale decisions, multiple-code routing, legacy protocol and
profile migration, registration heartbeat/watchdog recovery, injected callback
loss, and three/four-player save-status agreement. Save-status tests also cover
hash/generation mismatch, reordered roster updates, slot reuse, missing local
installation, acknowledgement repair and bounded send backpressure. See the
package's playtester notes/manifest for executed tests and remaining WAN and
Steam Deck acceptance checks. The previous accepted Beta 2 package is retained
separately as the rollback build.

## Boundary recovery and four-player rebuild (7 September 2026)

Use the matching **online-boundary-recovery-four-player** rebuild on every
machine. Gameplay protocol **46** adds required-checkpoint preflight probes;
protocol 45 and older gameplay partners are incompatible. Social identities,
ROM matching, save formats and separate single-player/online saves are unchanged.

Normal races and minigames now accept **2, 3 or 4 racers**, as requested. New
lobbies still default to two. JOINTVENTURE Adventure remains two-player and
the host cannot launch it with more than two occupants. This restores access
for beta testing; it is not a claim of completed four-device/WAN qualification.

The existing paths are hardened rather than replaced:

- Retained, authenticated input commits remain repairable during finish seals
  and suspended frontend handoffs. Serving old history does not authorize new
  simulation ticks, future commits or retired epochs.
- Identical unsent handoff, launch and repair retries are coalesced. Unique
  control events remain bounded and terminal Disconnect has reserved capacity.
- Forward catch-up validates its entire immutable input chain before installing
  state and advancing the session cursor in one admission transaction. Missing
  history requests repair and leaves memory/cursors unchanged; invalid hashes
  still fail. Exact-frame live corrections retain their existing behavior.
- Catch-up defers to ordinary native ticks during recovery, handoff, finish or
  start barriers, and when finish state, player roster or actor membership
  would bypass native lifecycle work. This is a conservative guard, not a new
  whole-memory snapshot or hidden native replay loop.
- Recovery distinguishes consumed input from the explicitly reported completed
  native tick. A late Begin at the exact completed boundary can be captured by
  the outer coordinator; input admission parks before executing a later tick.
- A host parked by peer frame debt now has a worker-owned 90-second no-progress
  failure path. Repeating the same progress watermark cannot renew that timer.
  Start, finish and recovery coordinators use 45 seconds without meaningful
  progress and a 90-second total ceiling. Duplicate traffic does not count as
  progress. These bounded budgets allow route retries, not unlimited waits.
- Oversized or interrupted UDP receives and transient UDP reachability errors
  no longer immediately kill an otherwise usable socket. Invalid packets are
  still discarded and genuine socket failures remain errors; liveness handles
  sustained peer loss. Linux detects oversized datagrams using MSG_TRUNC.
- Quick Join construction/channel/negotiation exceptions retire partial routes
  and request retry. A failed replacement leaves the prior route intact. An
  unexpected transport exception is contained at the session worker boundary.
- Weighted receive service reserves opportunities for required checkpoints
  while keeping control and input history ahead of optional live replicas.
- Wait presentation skips a busy inspector lock, records unusually slow SDK
  replay durations, and keeps diagnostic disk/console writes outside the
  session mutex. SDK/GPU queue ownership is unchanged: a hung graphics driver
  is not made nonblocking by these project-owned changes.
- Current authenticated map/roster disagreements report both scene identities
  instead of looking like silent network loss. Stale scene traffic is ignored.
- Preflight now exercises all five traffic classes for every guest, suppresses
  duplicate probe echoes, assigns per-racer scores and reports the weakest
  result. It remains a short synthetic network check, not a native game-load,
  CPU/GPU or crash-freedom guarantee.

Regression coverage includes retained finish/history repair, bounded blocked
queues, transactional snapshot rejection, exact late recovery, stale progress,
UDP oversized-then-valid delivery, injected route-construction failures,
receive-lane fairness and 2/3/4-player protocol flows. See the new package's
`VALIDATION.md` for final build, suite and native-check results and remaining
acceptance work. Prior rollback packages remain separate and unmodified.

No shadow, interpolation, SDL ownership, launcher countdown architecture,
rollback-window setting, single-player save or dependency source changes are
part of this follow-up.

## Historical Beta 1 online recovery rebuild (6 September 2026)

Use the matching **online-recovery-cutscenes** rebuild on both machines.
Gameplay protocol **45** includes the separate reliable checkpoint channel
and the zero-racer cinematic handshake. Protocol 43/44 builds are not
compatible partners. The first protocol-44 recovery candidate is withheld:
native testing exposed a consecutive Adventure cutscene stall.
Friend identities, the social protocol and save formats are unchanged by this
follow-up.

This rebuild changes the existing networking paths and fixes the online
frontend simulation step. Launcher countdown, boss/regular-race controls,
single-player timing and rollback-frame settings are unchanged:

- Admitted Quick Join routes retain their logical address across replacement.
  Repeated failed sends no longer keep postponing the first reconnect attempt.
  Old SDK connections are retired on a bounded background executor, with stale
  callback checks retained.
- Completed race-start, recovery and race-finish releases remain available for
  authenticated late acknowledgements after local teardown. Completion history
  is bounded to 16 records with two-minute expiry; duplicate old transitions
  cannot continually renew that history or resurrect completed waits.
- Required checkpoints use a separate reliable, unordered channel with a
  32-KiB admission budget. Tiny control and authoritative-input lanes remain
  separate. The channels still share the physical link and SCTP congestion
  control; this is not a bandwidth or zero-latency guarantee.
- Identical unsent checkpoint/control retries are coalesced. Immutable
  checkpoint encoding is reused, while subsequent transmissions retain fresh
  authenticated packet nonces. Required state is never silently discarded.
  Full-checkpoint retry timing accounts for measured RTT and jitter.
- Presentation reads an immutable session view instead of waiting on the
  session lock. Waiting-notification age survives reason changes. Main-thread
  wait presentation skips busy project-owned rendering locks rather than
  blocking on them; renderer ownership and the existing first-frame safety
  gates remain intact.
- Client catch-up starts at two excess frames and aims to settle within one
  frame above the existing delivery cushion. Brief missing-commit gaps retain
  catch-up history but never accelerate through unavailable input. The host
  remains at authored 30 Hz; the existing client ceiling remains 40 Hz. Shared
  launch input delay and rollback-window controls are not changed.
- Application RTT probes retain up to four outstanding tokens, so a reply is
  not discarded merely because the next one-second probe was sent. Connection
  details identify expired probes and expose queued bytes/age, checkpoint
  buffering and maximum network-pump duration.
- An unavailable retained menu-input prefix, or a sustained no-progress repair,
  fails explicitly rather than fabricating state to clear a transition screen.
  Separate single-player/online save handling is unchanged.
- Admitted menu/post-race/cinematic ticks use the same two-VI logic step as
  online racing, instead of inheriting each device's framebuffer timing.
  Zero-racer Adventure cinematics complete the existing verified baseline and
  Arm/Go handshake, then resume on the frontend ledger. Subsequent loads get
  fresh epochs even when they request the same hub. No fake racer is created;
  incomplete regular-race/boss rosters are still rejected.
- Session completion caches reset on join/disconnect as well as hosting.

### Qualification for this rebuild

Final-source Release builds passed **62/62 project test suites on Windows**
and **62/62 on Linux (Ubuntu 24.04 under WSL)**. These include the previously
deferred social cancellation tests. Focused coverage includes 1,000 iterations
of late terminal-release handling, stale begin messages, bounded completion
history, retry deadlines, nonce-safe retry coalescing, a real local WebRTC
route replacement and actual SCTP backpressure with delivery of the control
message and all accepted checkpoint packets.

Those are protocol/transport and policy tests, not 1,000 native DKR races.
The consecutive cinematic/hub/boss/race protocol sequence and zero-racer
snapshot round trips pass, but the exact balloon-plus-key native retest is
still pending. Do not treat this build as fully gameplay-qualified yet.
The six-frame catch-up target is verified in the policy simulation, not yet a
Steam Deck or distant-Internet gameplay measurement. The proposed 100 mixed
native transitions, two-hour soak, cross-platform/Steam Deck gameplay and full
adverse-WAN matrix remain qualification work. No claim is made that every
native input entry or an internal GPU/SDK wait has been proven nonblocking.
That earlier rebuild limited public online play to two players.

The prior cancellation package and exact pre-edit source snapshot are retained
locally in `build/rollback-pre-online-recovery-beta1-20260906`. GitHub release
preparation and public 3/4-player enablement remain deferred.

## Beta 1 cancellation rebuild

The follow-up rebuild removes a cancelled outgoing request from the public
list and counts immediately, including when its recipient has never connected.
The visible result is "Friend request cancelled locally", not "Cancellation
queued". A private cancellation record remains for background delivery and
restart recovery, so removing the card does not silently lose remote cleanup.
An offline peer cannot be notified instantly without a backend inbox.

Request snapshots are published before the Cancel action returns, and worker
publication shares the state lock so an older snapshot cannot revive the card.
Explicitly re-adding a code before its cancellation reaches authentication
restores that request intentionally; it is not treated as an invisible duplicate.
No social wire format, retry schedule or gameplay logic was changed for this
follow-up. Added regression cases cover immediate known/unknown-recipient
cancellation, immutable snapshots, re-add, restart and deferred remote cleanup.
They are compile-checked only: runtime testing remains deferred at the user's
request. The original Beta 1 package is retained in its original output folder.

## Before testing

- Both racers should use v1.0.5 Beta 1. Social authentication now uses protocol
  4; older social handshakes are incompatible. Existing identities and friend
  codes are retained when a valid profile is migrated.
- With the application closed, keep a private copy of your configuration's
  `online-profile` directory before trying the beta. The profile now uses a
  checksummed schema 5. Do not assume an older executable can read the migrated
  file. To downgrade, restore the matching pre-beta profile as well as the
  previous application package. Never share this directory: it contains keys
  and private friend codes.
- Offline requests remain queued locally. Delivery still requires both
  applications to be reachable at the same time; there is no offline-message
  backend or new relay service.
- The existing single-player and online-only save locations remain separate.
  This pass does not transfer ROMs or change race simulation/rollback policy.

## Changes by audit finding

| Finding | Project-owned implementation |
|---|---|
| 1. Friend rejection deadlock | Defer channel/connection retirement outside the friend-state lock. SDK callbacks enqueue work rather than synchronously re-entering that lock. |
| 2. Offline request retry storms | Dedicated social worker; duplicate-code suppression; fair per-destination retry/backoff; at most four outgoing negotiations and one new attempt per half-second. Requests are retained independently of the connection budget. |
| 3. Missing request decisions | Persist acceptance, rejection, blocking and cancellation decisions; retry until acknowledged after local storage succeeds. Preserve multiple incoming requests, display an outgoing queue, consume single-use codes only once. |
| 4. Alias versus identity | Resolve live authenticated routes by verified identity rather than assuming the invitation route is the identity route. |
| 5. Stale callbacks/lifetimes | Weak generation-scoped social ownership, synchronized channel publication, Quick Join callback lifetime gate and retired-peer receive guards. |
| 6. Profile write stalls/errors | Ordered background profile writes, revision-based Saving/failure status, validated IDs/records, checksum and backup recovery. Do not silently generate a replacement identity over damaged existing files. |
| 7. Signaling/rekey failures | Distinguish socket-open from confirmed registration; registration deadlines, bounded pre-admission peers and stale negotiation cleanup. Keep the old Quick Join code and keys until replacement registration succeeds. |
| 8. Authentication/privacy | Bind proofs to sender/receiver identities and nonces, reject reflected/invalid proofs, and restrict presence/lobby state to accepted, unblocked friends. No third-party crypto implementation changes. |
| 9. Lobby invitation delivery | Identity-scoped incoming IDs, monotonic terminal states, acknowledgement/retry until expiry, no silent eviction of active inbox entries. Mark acceptance after actual admission; retain valid friend admission context through compatibility retry. |
| 10. Buffered transport send | Treat a buffered send on a still-current, open, connected route as accepted instead of blindly queueing a duplicate. Retain existing higher-level reliable repair for disconnects. |
| 11. Malformed/unapproved peers | Validate message types and sizes, bound peers and per-peer receive bytes, discard retired packets, and isolate offending peers instead of failing the entire lobby. |
| 12. Stale admission packets | Restrict admission replies to admission phases; ignore late replies after joining; service known peers before the new-peer lobby lock gate. Diagnose loss of host liveness while awaiting approval. |
| 13. Pre-flight soft lock | Remember terminal test IDs, ignore late probes, acknowledge/retry results and finish missing results as Inconclusive after a deadline. Block rekey/start during incompatible lobby operations. |
| 14. UI work/layout | Immutable social snapshots; filter/sort caching; background previous-save and ROM validation; discard stale async results. Responsive request/grid rows and honest Not measured latency labels. |
| 15. Notification overflow | Protect actionable invites from presence bursts, retain inbox entries when a toast cannot fit, retry enqueueing and prune seen IDs. Preserve the existing anti-flap gate. |
| 16. Host save/replay/leave | Stage prospective host save bytes until game activation, retain previous online progress, detach replay snapshots for background finalization and report errors. Retire Quick Join transports outside the shared session lock. |

Most implementation is in `friend_service`, `quick_join_transport`,
`direct_session`, `runtime_ui`, `save_manager`, and `replay_recorder` with small
project-owned helpers. Dependencies, recompiled function/patch output, renderer,
interpolation, shadows, input backends and the single-window lifecycle were not
edited for this Online pass. Pre-existing launcher-performance and Magic Code
work was preserved, not rolled back.

The race countdown, frame-debt/rollback algorithms, boss/regular-race controller
routing, two-player limit and imported-ROM matching rules were not rewritten.
Changes to transport lifecycle can still affect online gameplay; that is why
real racing qualification remains necessary.

## Evidence already obtained

These checks ran before the user requested that testing stop while they race:

- Windows friend-service, expanded friend-delivery, local social integration,
  DirectSession, replay and notification suites passed. The combined six-suite
  run took 90.32 seconds; the save-manager suite also passed separately.
- Actual private localhost WebSocket rendezvous plus WebRTC connections tested
  three synthetic profiles, two distinct requests, acceptance while a sender
  was unavailable, and restart/reconciliation. No real users were contacted.
- Windows and Linux Quick Join lifecycle integration passed: failed replacement
  registration retained the old code; successful rekey retained live channels;
  128 unique 8 KiB packets were delivered; malformed traffic was ignored and
  concurrent close/send completed. This run recorded **zero backpressure
  attempts**, so it is not evidence of forced SCTP saturation. The buffered-send
  branch has policy coverage, not a measured congested-Internet qualification.
- A 30-minute earlier-source social soak retained 32 unreachable requests across
  57,700 frontend service passes. Maximum measured service call was 560
  microseconds and peak peers was five, with no assertion failure or rejected
  executor job. This measures service-call cost, **not rendered launcher FPS**.
- All 62 project-prefixed Linux suites passed. The parent CTest invocation was
  then stopped during the unrelated dependency `fullbench` benchmark. This is
  not a claim that all 66 registered project/dependency tests completed.

These are source-development results, not a complete test pass over the final
packaged bytes. Later retry-map pruning, registration deadline refinements,
cancellation-token/crossed-decision handling and version metadata were compiled
but not executed after the user's stop-testing instruction. A regression case
for crossed cancellation/acceptance was added but deliberately not run.

## Deferred qualification and remaining limits

The user elected to perform the remaining testing. No further game instances,
unit/integration tests or packaged runtime self-tests were started after that
instruction. Packaging explicitly disables those runtime checks for this beta;
normal packaging still enables them by default. Static package scans and build
checks do not substitute for runtime testing.

Still required before treating this as a fully qualified release:

1. Run the final-source Windows/Linux suites and the exact staged-package
   Controller Pak, private SDL3 host and live input-switch checks.
2. Inspect every Online subpage at 1280x800 and desktop sizes, in Launcher and
   Overlay, using keyboard/controller. The attempted visual check was stopped;
   no final visual or 60 FPS target-device result is claimed.
3. Submit several different offline friend codes, accept/reject/cancel from
   both sides, close/restart both apps, and verify all intended results converge.
   Specifically cover simultaneous accept/cancel, revocation and reconnection.
4. Exercise Windows-to-Linux, Windows-to-Windows and Linux-to-Linux Quick Join
   over real networks, including loss/congestion, host approval, rekey failure,
   pre-flight, compatibility sync, invitations and repeated leave/rejoin.
5. Play repeated regular and boss races, minigames, Adventure initials/cutscenes,
   race-end/menu transitions and return to single player. Compare single-player
   save hashes before/after testing using private local copies.
6. Repeat the soak on the final source and physical Steam Deck; record frame
   percentiles, memory and connection counts, not just an average FPS.

Not every scalability enhancement proposed by the audit is finished: variable-
height card lists are not virtualized; large profiles still incur copying and
serialization costs; failed disk writes can retry on the worker cadence; older
profiles with more than eight active friend codes retain those codes instead of
silently revoking them. Durable request decisions are implemented, but friendship
removal is still a one-shot remote notification and lobby invitations remain
session-only, expiring records. Further load measurements should guide changes
to these limits, rather than silently dropping users' data.

Pre-flight measures synthetic transport traffic, not a complete CPU/GPU race
benchmark. Restrictive NAT/firewalls or unavailable rendezvous infrastructure
can still prevent peer-to-peer connections. No universal connectivity guarantee
or security certification is implied by these changes.
