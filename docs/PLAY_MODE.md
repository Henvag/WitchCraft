# Integrated Minecraft play mode

Enable WITCHCRAFT_PLAY=1 for the Witcher process (staging installer: -Play).
This prototype combines native Geralt hiding and camera-anchor follow with
Minecraft physics, world meshes, HUD/hotbar and inventory input. It starts after
a stationary player settles on delivered sampled terrain/walls. Normal Session
control remains gated; the explicit play controller owns this mode.

Controls: WASD movement, Space jump (release between jumps), E inventory,
1-9/wheel hotbar, mouse inventory cursor/clicks or Minecraft block/item actions,
Shift/Ctrl modifiers, Esc Minecraft screen/menu, F8 return to Geralt. Inventory
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
Minecraft tick timestamp and interpolated/raw Minecraft positions at about
120 samples per second. It opens the game with read/query permissions only,
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
