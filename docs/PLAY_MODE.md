# Integrated Minecraft play mode

Enable WITCHCRAFT_PLAY=1 for the Witcher process (staging installer: -Play).
This prototype combines native Geralt hiding and camera-anchor follow with
Minecraft physics, world meshes, HUD/hotbar and inventory input. It starts after
a stationary player settles on delivered sampled terrain/walls. Normal Session
control remains gated; the explicit play controller owns this mode.

Controls: WASD movement, Space jump (release between jumps), E inventory,
1-9/wheel hotbar, mouse inventory cursor/clicks or Minecraft block/item actions,
Shift/Ctrl modifiers, T chat/commands, F5 first/third person, Esc Minecraft
screen/menu, F8 return to Geralt. In creative, double-tap Space to toggle flight;
hold Space to rise and Shift to descend. Inventory
keeps the avatar hidden and stops movement keys. A virtual cursor avoids Witcher
OS cursor recentering. Game callbacks queue input; only the overlay thread writes
the shared input ring. Overflow releases input and ends play. F8 ends the session
for the game run; restarting starts a fresh session.

## Verified live behavior

WASD, diagonal/camera movement and F8 passed earlier tests. Two successive standing
jumps passed. The user confirmed inventory display and item clicks, a long walk
through streaming terrain, and moving jumps after the airborne follow correction.
Evidence: jump-standing-pass-2026-10-11.log,
play-inventory-pass-walk-return-2026-10-11.log,
play-long-walk-jump-return-2026-10-11.log and
play-moving-jump-pass-elytra-return-2026-10-11.log.

The later attempted Elytra/firework use ended at a normal ground acknowledgement
with closest error 0.060 m. Flight takeoff was not established. The user then
reported choppy animation/movement. The first smoothness run measured about
26.5-29.8 FPS with 30-38 ms average host work and remained visibly choppy.
Block placement worked, but linear texture filtering blurred the blocks and
landing on a block triggered the old 0.6 m ground-height guard. Evidence:
play-smoothness-block-test-2026-10-11.log.

The next live run confirmed sharper block textures and walking onto/off blocks
without restoring Geralt. Movement and inventory still felt choppy. At 2560x1440
the host measured roughly 49-51 FPS, 20 ms average work, with peaks around 36 ms.
World/readback averaged 5.5-5.7 ms (GPU wait 3.4 ms, bitmap copy 2.1 ms), HUD
3.2-3.4 ms and upload 1.6-1.8 ms. These stages do not account for all host work.
Evidence: play-block-platform-pass-timing-2026-10-11.log. F8 was not explicitly
reported in that run.

## Smoothness revision

Avatar position uses SkyCraft v11 previous/current physics ticks and their shared
QueryPerformanceCounter clock, interpolated on the Witcher host frame. Native
horizontal camera-anchor commands use that same interpolation instead of raw
20 Hz positions. Authoritative raw ticks still drive physics checks. Interpolation
clamps at the current tick, avoids teleports and invalid/stale data, and preserves
Minecraft's published pose in menus. Exported mesh animation poses still come
from Minecraft; this does not add skeletal animation or fake new poses.

The host targets a 60 Hz deadline with a high-resolution waitable timer, waiting
only for the remaining frame budget. The old unconditional Sleep(16) after
rendering is removed. Five-second host timing logs report actual FPS and average/
peak work time. Rendering still uses GPU readback, CPU HUD composition and a
layered desktop window; 60 FPS is a target, not an established performance result
or a native Witcher render-frame integration.

Normal follow lag up to 1.25 m keeps movement continuous. Beyond that, input is
released until lag closes to 0.5 m. A gap over 2 m or a catch-up lasting over one
second ends play. Native requests stay at most 0.5 m each and require horizontal
confirmation within 0.05 m/250 ms. All play requests keep the native anchor at
its current ground height and permit at most 0.5 m of native terrain snapping per
request. This fixes the ground mismatch after landing as well as rising-ground
jumps. Minecraft may now stand up to 2.1 m above native terrain on small block
platforms, while the hidden camera anchor remains on the terrain underneath.
Jumping or walking down from those platforms can land back on sampled terrain.
Landing no longer overwrites a recorded failure reason. Older diagnostics retain
their original full 3D acknowledgement behavior.

Block textures now use nearest minification/magnification with mip-level blending
instead of bilinear filtering. HUD composition caches horizontal scaling indices,
skips transparent pixels and directly copies opaque pixels. Redundant CPU bitmap
clearing is removed because successful GPU readback overwrites every row.
Five-second stage logs separate world rendering/readback, HUD composition and
window upload, with additional GPU-wait/bitmap-copy measurements. These changes
are built; their visual quality and performance benefit require live measurement.

The following revision adds an SSE2 compositor for equal-width HUD frames,
retaining the scaled scalar path for other widths and existing row orientation.
Its premultiplied RGBA-to-BGRA source-over output matches integer scalar rounding.
It caches the game window and only positions/shows the overlay when its window,
position, size or visibility changes, instead of issuing SetWindowPos every frame.
Timing logs now also measure preparation and post-upload work. Performance and
overlay behavior after focus changes/resizing still need a live check.

The next run reached roughly 60 host FPS (about 10 ms average host work), but the
user still experienced dragging/choppiness in movement and inventory. A read-only
Minecraft frame-counter sample also measured about 60 FPS while play was inactive;
that does not establish active animation or HUD delivery cadence. New stage logs
count fresh Minecraft state samples and HUD frames instead of assuming that each
host draw contains new content. The separate layered window is not synchronized
with Witcher's native presentation. Smoothness remains unresolved.
Evidence: play-performance-water-test-2026-10-11.log.

The water-support run delivered only 50-60 fresh Minecraft states and HUD frames
per five-second window despite 300 host draws: about 10-12 fresh FPS during play.
This explains why inventory also looks slow; native following alone cannot cause
that symptom. Evidence: play-water-pass-cadence-2026-10-11.log. The user reported
water now staying on ground, and Minecraft logged downward flow refused because
it was resting on measured ground (tops 0.38/0.50).

A Java Flight Recording showed substantial render-thread samples in OpenGL
buffer unmapping/readback; an NVIDIA sample showed 97% GPU utilization. The
recording also includes inactive/menu time, so it does not establish a precise
per-frame GPU delay during takeover. It identifies transfers as a target, not a
proven complete explanation. Capture: tools/mc-render-profile.jfr in the project.

The latest revision caps Minecraft's HUD transfer viewport to 1280x720, preserving
aspect ratio and never upscaling small windows. At 2560x1440 this quarters its HUD
render/readback bytes. World/character meshes still render into Witcher's full
native-resolution overlay. HUD composition uses nearest-neighbour scaling; the
Minecraft UI may have different font/GUI scale. Both current cursor updates and
queued cursor-before-click events map native coordinates to transfer coordinates.
Set WITCHCRAFT_HUD_FULL_RES=1 before launching Witcher to compare the previous
full-resolution transfer. Fresh-frame counters remain enabled. Performance and
inventory appearance/click alignment need a live check; smoothness is not yet
declared fixed.

The reduced transfer run delivered approximately 23 fresh Minecraft/HUD FPS;
the user reported some improvement but remaining choppiness. During inventory,
the scalar resized compositor took about 10 ms and reduced host output to 52 FPS.
Evidence: play-reduced-transfer-test-2026-10-11.log. The next native build caches
the resized HUD when a new source frame arrives and uses SSE2 blending at every
output frame, including scaled inventory screens. Resize tests cover bottom-up
images and noninteger scales against the same coordinate mapping used by clicks.

The accompanying Minecraft patch adds persistent OpenGL readback buffers to
SkyCraft's FrameExporter, eliminating per-frame map/unmap on supported OpenGL 4.5
drivers or 3.3 contexts exposing the corresponding ARB extensions. The first
startup used the legacy fallback because Minecraft requests a 3.3 context; the
extension capability check passed the repeated isolated GPU test before reinstall.
See minecraft-patch/README.md for pinned upstream provenance, build,
fallback and the passing isolated GPU pixel check. This changes the installed
Minecraft JAR as well as the native DLL; both require a stopped game for replacement.
No live performance improvement is claimed before the combined run.

## Fluid-support revision (built, awaiting live check)

Water fell through sampled ground. The installed SkyCraft fluid mixin reads
SkyCollision.hasGeometry/groundTop, which use ColRegion voxel occupancy; exact
ColTris alone only provided local player movement. Previous ColRegion packets
were empty arrival markers. TerrainPatch now marks measured height-field surfaces
as kTriTerrain without setting diggable or material flags. TerrainVoxels samples
each triangle at 1/8-block horizontal column centres and fills two metres beneath
the measured height, bounded by the exported region. Missing triangles remain
holes. Aligned per-cube packets send exact triangles followed by actual ColBlock
occupancy, and clear stale voxel support when patches move or lose surfaces.
The local player retains exact triangle collision via SkyCraft's smooth collider.
Untyped wall/object triangles are not filled beneath.

This supplies the existing Minecraft fluid rules with terrain support. The live
flow/spreading result remains unverified; sampled holes, patch boundaries and
unsampled vertical walls remain limitations. Tests cover fractional ground tops,
negative coordinates, partial footprints, clearing and packet payload sizing.

Terrain refresh starts after two metres from patch centre or one second since
publication. Up to 32 rays per callback retain a one-millisecond query budget.
Published play coverage has a two-metre edge margin. New coverage bounds become
active only after Minecraft consumes the replacement packets. Valid old coverage
keeps movement active during ordinary refresh delivery. At the edge, input pauses
for up to five seconds while fresh coverage arrives; inventory and the avatar
stay active. World, focus, native following/hiding and fresh-report failures
still restore Geralt. There are no former three-metre/20-second trial limits.

## Remaining limits

Native vertical camera following, Elytra/rocket flight, extended falls and creative
flight are not implemented. MC_FLYING in SkyCraft v11 reports creative flying,
not Elytra fall-flying. Flight needs explicit state and host-camera integration;
removing standing-jump bounds is insufficient. Jumps stay within -2.1 to +1.6 m
of takeoff and land within three seconds; airborne height is also limited to
-0.6 to +3.7 m relative to native terrain. Walking off a small platform must land
within 1.5 seconds. Supported ground height is -0.6 to +2.1 m relative to native
terrain. The camera anchor follows terrain under the airborne or raised avatar.
Higher block structures, native elevated camera following, complete scene handoff,
dynamic/NPC collision, combat and Witcher scenery depth occlusion remain pending.
Forwarded block actions do not establish destructible Witcher scenery.

Nineteen suites pass, including transfer viewport sizing/aspect ratio and cursor
mapping matched to displayed HUD pixels, SIMD HUD blending across all alpha/destination
values with mixed transparency, unaligned rows and tails, tick interpolation/invalid data/teleports/menus,
ordinary lag without repeated braking, catch-up and streaming timeout recovery,
inventory input retention, moving jumps with changing native ground, native
horizontal acknowledgement, small-platform landing/descent and failure-reason
preservation, malformed raw input and unchanged diagnostics.

Next live check: walk and turn for 30 seconds, then move inventory cursor and drag
items for 10 seconds before F8. Compare fresh Minecraft/HUD cadence and user feel,
and check that inventory clicks still align. Keep Witcher focused; focus loss
still ends play. Elytra remains unsupported.

## Latest measured rendering and streaming update

The persistent OpenGL Minecraft readback path is active. The latest live trace
shows approximately 53-60 fresh Minecraft/HUD frames per second in recent
samples, with the overlay close to 60 FPS. The user reports smoother rendering
but intermittent walking stops. The trace repeatedly records movement released
while waiting for terrain streaming, even with native follow lag around 0.2 m.

The next native build permits up to 128 rays per observation instead of 32,
retaining the one-millisecond time budget checked after every query. It permits
16 collision packets per observation instead of four, still stopping at queue
backpressure. New terrain-delivery metrics separate sampling from queue/ack time.
Live improvement from these caps is not yet verified. The same run ended with
a grounded-camera fall-range return; this update does not change that limit.

## Native follow cadence update

The faster-streaming live run felt smoother to the user. Its latest session
recorded no explicit terrain-wait or follow-lag input releases; measured patch
delivery was commonly 266-406 ms. Fresh Minecraft/HUD delivery still varied,
including low-40-FPS windows, and movement did not yet feel like native Minecraft.

The next build replaces the native observer's GetTickCount64 16 ms gate with a
60 Hz QPC schedule. It tolerates small frame jitter, rejects duplicate callbacks
and avoids catch-up bursts after a stall. This targets uneven native camera
updates; it does not change Minecraft acceleration or remove collision guards.
A new cadence log records actual observation rate and peak interval. Regression
coverage includes jittered 60 Hz, 120 Hz callbacks, 30 Hz callbacks and a stall.
All 19 test suites pass. Live cadence and perceived improvement remain unverified.

## Follow-target latency update

The QPC-cadence live test produced near-60-Hz native observations. The user
reported even camera motion and good walking cadence, but a remaining dragging
feeling. Moving samples still commonly showed 0.2-0.3 m of raw tick-to-anchor lag.

The next build sends Minecraft's tick snapshot with the internal pilot command
and evaluates its horizontal interpolation on the game thread when consumed.
Previously the target was interpolated after the overlay upload and frozen until
a later native callback. The new path retains grounded native Y, acknowledged
movement, finite-distance checks, command expiry and interpolation clamping;
it does not extrapolate or change Minecraft physics. Tests cover advancement
between callbacks, endpoint clamping and unchanged diagnostic targets.
All 19 suites pass; live reduction in perceived dragging remains unverified.

## Read-only camera trajectory capture

The follow-target update reduced many observed moving raw-position gaps to
roughly 0.02-0.13 m, and the user reported slight improvement, but dragging
remains. Recent samples showed close to 60 fresh Minecraft frames per second.
These results do not establish whether the remaining delay is camera easing,
render presentation or input/physics latency.

The native probe can now capture the native actor, camera, view direction,
Minecraft tick timestamp and interpolated/raw Minecraft positions. It requests
an 8 ms sampling interval; observed Windows scheduling gives about 60 samples
per second. It opens the game with read/query permissions only,
checks its executable fingerprint/signatures and leaves the shared mapping
read-only. Capture duration is bounded to 120 seconds and can wait through the
main menu until a valid player appears. Paused and Minecraft-state flags are
included for filtering. No capture data is uploaded to GitHub.

Example (choose a local output path):

```powershell
.\build\witchcraft_native_probe.exe --sample-follow 60 C:\Temp\witchcraft-follow.csv
```

The probe was built and its main-menu capture path verified. The live movement
capture and camera-delay analysis are pending.

## Captured follow and camera delay

The read-only probe completed 6,040 samples over 120 seconds. Filtering to
unpaused active play gave 3,142 samples across about 50 seconds, with repeated
starts/stops and sideways movement. During movement above 1 m/s, horizontal
interpolated Minecraft-to-native feet error had a median of 0.097 m and a 95th
percentile of 0.200 m. A velocity comparison on a 20 ms grid estimated about
20 ms of Minecraft-to-actor delay and about 20 ms of actor-to-camera delay.
These estimates use 100 ms central differences and are approximate; they do
not measure Windows compositor or display latency. Camera offset also varied
with movement, confirming that native framing is not a rigid Minecraft camera.

The next build reads newer Minecraft snapshots directly on the game callback,
while retaining the pilot command's validity/expiry and teleport acknowledgement.
It advances the grounded anchor's horizontal interpolation by one 60 Hz frame
within the already resolved previous/current tick segment. It clamps at the
known endpoint and never leads native height. This targets the measured follow
and camera delay. All 19 suites pass, including stale snapshot/teleport/flying
rejection, newer-tick use and lead endpoint clamping. Perceived improvement and
camera stability are not yet verified in-game. The captured positions remain
local and are excluded from the repository.

The user clarified that the trajectory capture began with jumps over placed
blocks. Rechecking only the later portion (after 70 seconds), with central-difference
windows entirely on ground, gave 279 moving grid samples: median horizontal feet
error 0.110 m, 95th percentile 0.206 m. The estimated velocity delays remained
20 ms for both stages on the 20 ms grid. These approximate grounded estimates
support testing the follow update independently of the early jumping sequence.
The fresh-snapshot/bounded-lead build's repeat capture gave 2,961 active samples
over 46.2 seconds, including 444 grounded moving grid samples. Median horizontal
feet error fell to 0.0242 m and the 95th percentile to 0.0724 m. The fitted
Minecraft-to-native delay was zero at the analysis's 20 ms resolution; the
native-to-camera estimate remained 20 ms. The user reported slightly less drag,
but it remains perceptible. Zero fitted delay does not establish zero real latency.

## Remaining camera integration investigation

SkyCraft's `skse/src/Game.cpp` pins Minecraft's camera after Skyrim's native camera
update and propagates the transform to its render camera. WitchCraft currently
uses Witcher's native camera framing and presents Minecraft through a separate
layered window. Camera-stage lag and presentation latency remain distinct possible
contributors; the trajectory estimates do not prove either is the whole cause.

Read-only inspection of this exact executable found the active `CCustomCamera`
interface at object + 0x170, vtable RVA 0x3810198. Slot 0x08 (RVA 0x1e0f710)
adjusts `this` by -0x170 and delegates to the native update with its third argument
zero. Slot 0x10 (RVA 0x1e0f290) produces camera data; its position override is the
sum of object + 0x1d0 and object + 0x230. That sum differs from the camera director's
final position at +0x70 in the paused live sample, so changing the script-facing
camera position alone is not yet a verified final-camera solution.

`--sample-follow` now also records these intermediate pivot/offset positions,
guarded by exact interface and method identities. `tools/analyze_camera_stages.py`
compares their horizontal velocity delays without requiring active Minecraft
takeover. Use fixed view direction, clear ground and repeated starts/stops; combat,
turning and jumps can confound the fit. No camera pointer is replaced, no engine
method is invoked, and no camera memory is written by this probe. The first stage
recording was interrupted by a drowner attack and is excluded from conclusions.

The clean repeat captured valid intermediate camera fields. Its fixed-view
velocity fits selected zero delay at 10 ms resolution across the camera stages
during normal Geralt walking. Residual velocity error was appreciable, so this
does not establish perfect camera timing or directly measure Minecraft takeover.
There is not enough evidence to justify writing to the camera fields yet.

## Buffered physics-tick rendering

The earlier active Minecraft comparison contained 680 of 2,961 samples (23.0%)
with tick age above 50 ms. The previous renderer clamps interpolation to the
latest known tick endpoint, so those late arrivals can hold a moving pose even
when frame delivery is near 60 Hz. This is a separate issue from native feet lag.

The new shared motion timeline keeps eight known tick segments, filters small
timestamp jitter, and adapts a 4-30 ms rendering buffer from recent arrival timing
(initially 20 ms), following SkyCraft's approach. Avatar rendering and native
horizontal follow consume the same history. Native follow retains the bounded
one-frame lead; neither path extrapolates beyond known endpoints. Old snapshots
from the other thread cannot rewind the history. GUI transitions, relocation and
invalid data reset it. Ground-height ownership and F8 restoration are unchanged.

The buffer trades a small amount of latency for continuous interpolation; it is
not evidence that overall input-to-screen latency is solved. Logs now report
rendered and held-moving frames plus buffer duration. All 20 test suites pass,
including late-tick continuity, out-of-order snapshots, stopped motion, teleport,
GUI/invalid input and skipped-tick bounds. Live smoothness remains unverified.

The installed test was reported smoother, with slight remaining drag. Recent
five-second log windows showed 0-4 held-moving frames per roughly 300 rendered
frames, with buffers around 23-29 ms. These counts have a different denominator
from the earlier tick-age probe, so they are not a direct percentage comparison.
The session later returned on `fall outside grounded camera range`, after 3,469
confirmed native follow moves; this limitation still applies outside water.

## Chat, commands and shallow Minecraft water

The next build forwards T to open Minecraft chat, GUI keys for editing/submitting
commands, and Unicode text using the foreground keyboard layout. Shift is captured
at keypress, text precedes subsequent key events, and held typing/editing keys
repeat. GUI key releases remain matched if the screen closes while a key is held.
Event queues stay bounded and focus loss/F8 retain release-all behavior.

Use T, type `/gamemode creative` or `/gamemode survival`, then Enter. The upstream
mirror-world creation settings enable commands; an existing save may have different
permissions, which must be checked in-game. Creative flight is still unsupported:
switching game mode does not implement flying. First-person switching remains
disabled until Witcher's native view and Minecraft's perspective can switch together.

The patched SkyClient now reports an additional v11 flag (bit 8, `kMcInWater`),
without changing the packet layout. It distinguishes floating in water from falling;
the existing `kMcSwimming` flag reports sprint-swimming. Within the supported shallow
height range, PlaySession permits sustained Space for ascent and does not run its
ordinary fall timer. Leaving water restores fall checks. Minecraft's existing normal
avatar exporter supplies the swimming pose; its separate ragdoll capture is the only
path that forces a standing pose.

This supports testing player-placed Minecraft water. Witcher's lakes/rivers are not
yet published through WaterGrid and therefore do not trigger Minecraft swimming.
Deep-water camera following, flight, first-person view and native combat integration
remain separate features. All 20 native suites pass, and the patched Java build
preserves unrelated JAR entries byte-for-byte. Live chat/command and swimming checks
are pending.

The live run confirmed T opens chat. It returned Geralt before command testing;
the host trace shows a single unavailable/zero Minecraft snapshot between valid
in-world GUI snapshots, with no native pilot failure. The next host build retains
the last coherent snapshot for at most 100 ms only while the same Minecraft PID
has a healthy heartbeat. Cache fallback never renews its age. Coherent new world,
GUI or death flags are applied immediately, and disconnect/PID change/expiry
invalidate the cache. Logs count missed reads and coherent reuse. All 21 suites
pass, including the recorded chat/read-race sequence and expiry/peer-change checks.
The Minecraft JAR is unchanged for this correction; live verification is pending.

## Creative flight and first-person camera build (2026-10-12)

The user confirmed chat transitions and both game-mode commands after the bounded
state-read cache fix. This build adds the mayfly ability as v11 flag bit 9 without
changing packet sizes. Creative Space input remains held and preserves double-tap
edges, allowing vanilla Minecraft to own flight. Survival retains its jump/fall
guards. Flight requires the verified native camera hook; the altitude bound is
256 metres above the hidden ground anchor. Existing horizontal follow/streaming
bounds still apply. Elytra is not included.

The CCustomCamera ICamera GetData slot is checked against its exact vtable and
method prologue before installation. The wrapper calls the original first, then
changes only returned translation (and first-person near clip). Witcher's camera
director consumes that data and rebuilds its transform normally. First person
uses Minecraft feet plus eye height; creative third person preserves Witcher's
camera offset and adds Minecraft's elevation. Rotation remains under Witcher
mouse control. F5 skips the front-facing perspective. First person permits an
empty avatar export; Minecraft's transparent hand/HUD pass remains visible.

Ownership requires active play, fresh commands, a healthy link, the current
world/epoch/relocation, a hidden player, foreground focus, an unpaused game and
the matching topmost camera. Returning with F8 stops the override, so subsequent
camera reads use the original pose; no persistent camera position is written.
Twenty-two test suites pass, including flight gating, held Space, double-tap
edges, GUI release, F8 and finite camera conversion. Live camera/flight behavior
still needs verification after installation.

### Camera-stage validation correction

The first live flight test confirmed vanilla takeoff, but the camera stayed on
Geralt's ground anchor. The director clears its output lens to zero before it
calls GetData. The hook incorrectly required a complete camera snapshot inside
that construction stage, so it skipped every override. The correction validates
actor/world identities, finite feet and pause state independently of camera
output. Ordinary observers retain full pose/lens validation. A regression test
uses the cleared lens and still rejects mismatched player identity.

The trace also ended flight when native ground rose more than 0.6 metres above
Minecraft. Creative flight now uses a symmetric 256-metre altitude bound; survival
guards are unchanged. Camera logs record applied overrides, perspective, height
difference and requested position for the next live verification.

### Saved-flight startup correction

After relaunch, the link and collision queue were healthy but the player still
reported flying from the creative mirror save. DiagnosticSettle excluded that
state, preventing an arrival teleport and leaving the avatar hidden. Play mode
now permits a saved mayfly state to begin arrival when the camera hook is ready.
The Minecraft arrival teleport clears flying on both client and integrated
server and updates abilities, preserving creative mode and its flight permission.
A regression covers saved flight, teleport issuance, grounding and active play.
Camera-stage validation remains installed; live camera-follow verification is
still pending because this startup issue prevented takeover.

### Live camera/flight pass and water-edge acknowledgement

The user confirmed first person and camera following during creative flight,
with some glitchiness. Camera logs confirmed overrides in modes 1 and 0 at
roughly 180 GetData calls/second and flight heights up to about 16 metres in the
sampled entries. Flying above water later restored Geralt. The native trace
identified a ground-height acknowledgement rejection: the hidden anchor snapped
from requested Z 65.292 to Z 66.013 while X/Y matched the requested target.

During camera-owned creative flight, ground-follow acknowledgements now accept
native vertical changes up to four metres. The default remains 0.5 metres for
other grounded follow. Horizontal confirmation remains within 0.05 metres with
a 250 ms deadline, and larger/non-finite changes still stop. The recorded water
coordinates are covered by a regression, alongside horizontal timeout and the
upper height bound. This fixes the observed rejection in tests; crossing the
water edge still requires live verification. Reported camera glitchiness is
being clarified separately rather than assumed resolved by this change.
