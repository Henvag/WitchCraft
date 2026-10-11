# WitchCraft

Goal: play The Witcher 3 as a Minecraft character in third person, with Minecraft
movement, inventory, block placement/breaking, and eventually SkyCraft's combat,
fluids, destruction, NPC collision, and multiplayer features.

Minecraft runs SkyCraft's Fabric mod. This host uses SkyCraft protocol version 11,
unchanged. Check protocol compatibility before upgrading the Fabric build.

## Current status

The first integrated Minecraft play build is ready for its live game check.
It restores the Minecraft HUD/hotbar and inventory, combines WASD with jumping,
forwards inventory keys/mouse input, and stays active until F8 or an actual
interruption. The old diagnostic movement time and area limits are removed.
The smoothness revision uses shared-clock tick interpolation for the avatar and
native camera anchor, targets a 60 Hz host deadline, avoids ordinary follow/stream
input pauses and respects native ground snapping. All seventeen suites pass;
the first live run still felt choppy at roughly 27 FPS. Block placement is confirmed.
Sharper textures and small block-platform traversal are now confirmed; the next
run measured roughly 50 FPS but still felt choppy. The latest build speeds up HUD
blending, caches overlay window placement and measures the remaining host work.
The last run reached 60 host FPS but still felt choppy, so smoothness remains
unresolved. The following test measured only 10-12 fresh Minecraft/HUD FPS during
play. Water now appears supported, with matching ground-support messages in the
Minecraft log. The latest build quarters HUD transfer pixels at 1440p and remaps
inventory coordinates, preserving native-resolution world meshes. All nineteen
suites pass. Reduced transfers reached about 23 fresh FPS, with some perceived
improvement. The latest update caches inventory scaling and includes a Minecraft
patch using persistent OpenGL readback; its isolated GPU pixel check passed on
the GTX 1080 Ti. Combined in-game performance remains to be verified.
See [play mode](docs/PLAY_MODE.md) for controls,
architecture, current limits and the combined game check.

WASD/diagonal/camera movement and F8 restoration previously passed live tests.
Two successive standing jumps also passed. The first integrated run acquired the HUD and confirmed 49 follow updates before
the immediate follow-lag cutoff ended play. The revised build releases movement
briefly while the native anchor catches up, using small acknowledged steps.
Inventory display and clicks are now confirmed by the user. The trace also records
walking jumps and successful catch-up. The longer walk hit the old terrain coverage
boundary; the streaming revision prefetches earlier and holds Minecraft active
during brief coverage waits. The user confirmed a long walk stayed active; the trace recorded 180 follow updates.
A moving jump then hit a native height-confirmation mismatch on rising ground.
The revised airborne follow confirms horizontal movement and lets Witcher snap
its hidden anchor to the ground. The user now confirms moving jumps work. A later Elytra/firework attempt returned
Geralt after a ground acknowledgement timeout; flight was not established.
Elytra flight and airborne Witcher camera integration remain unimplemented. The hidden native camera
anchor stays at ground height during jumps. Sampled collision, native scene
handoff, scenery occlusion, NPC/combat and elevated block traversal remain partial.

The native adapter targets DX12 executable 5.0.0.1048522 with verified signatures
and uses the player's native GetWorldPosition dispatch callback. The stand-ins
remain available for separate Minecraft testing. This is a prototype, not a
complete SkyCraft-equivalent conversion.

## Development history

The following entries record earlier builds and their validation at the time.
Current behavior is described above and in docs/PLAY_MODE.md.

The next build adds real PhysX ground queries and diagnostic terrain packets.
A 16-metre patch uses corner and centre samples, with hole/cliff rejection and
up to eight rays per observation (a one-millisecond budget checked between rays).
It streams non-diggable triangles through the existing collision bridge. This
height patch does not represent walls, caves or dynamic obstacles and does not
set collision ready. An outdoor-save test verified native hits and patch refresh;
after restarting Minecraft, it consumed the exported packets, including a patch
at Geralt's new location (99,256 bytes total, none pending). This validates querying and transport, not Minecraft walking
or full scenery collision. Evidence is in `docs/terrain-test-2026-10-11.log`.

The diagnostic build finishes each pending terrain patch before following
ordinary movement, including when the collision queue is full. Teleports and world
changes still invalidate partial work immediately. Tests cover this scheduling.
The outdoor walking test completed 24 patches across movement and the final stop;
after restart, Minecraft consumed all 484,896 queued bytes. This verifies sampling
and delivery while moving; walking on the exported colliders remains untested.
Evidence is in `docs/streaming-test-2026-10-11.log`.

The diagnostic build adds nearest static wall sampling: 544 horizontal rays
around the patch centre, within eight metres and from one metre below the sampling
height to three metres above it. Five hits must agree on a local plane and facing
normal before emitting a cell; missing hits and sharp changes leave gaps. Ground
and wall triangles are combined before delivery. The outdoor wall test produced 14 combined patches, nine
with wall triangles; the final stationary location repeatedly produced 136 wall
triangles. Minecraft consumed all 267,136 bytes with nothing pending, and no query
validation failures were logged. Evidence is in `docs/wall-test-2026-10-11.log`.
Actual movement against those surfaces and geometric completeness remain untested.
This approximation misses narrow gaps, hidden surfaces and dynamic obstacles.

Receiver review found a packet-indexing mismatch: SkyCraft stores each triangle
packet only under the eight-metre cube containing its minimum corner. Earlier
16-metre packets arrived but were not indexed throughout their coverage. The next
build sends aligned cube packets, copying crossing triangles into every touched
cube and emptying obsolete cubes. Six tests pass, including a regression using
Geralt's observed coordinates. The in-game cube test completed 15 batches of
6–23 cube updates; Minecraft consumed all 354,896 bytes with nothing pending and
no logged validation failures. Evidence is in `docs/cube-test-2026-10-11.log`.
Minecraft movement against those surfaces remains untested.

The next build adds empty voxel-region arrival markers alongside triangles,
because SkyCraft's spawn hold checks those markers before releasing the player.
They contain no solid voxels. An opt-in `WITCHCRAFT_DIAGNOSTIC_SETTLE=1` runs one
gravity-only check: wait for a stationary Geralt and delivered patch, spawn the
hidden MC player half a metre above his feet, then check for a released player
settling nearby. It never enables Geralt control, input capture or rendering.
Timeout, focus loss, movement or scene/world changes stop the diagnostic. Seven
tests pass. The first runtime attempt stopped on a new acknowledgement paired
with old coordinates, before the requested teleport completed. The revised build
waits for position and acknowledgement to agree. The repeat passed: released MC
settled at Y=2.267 beside Geralt's Y=2.277, with on-ground set and ack 1. This
validates gravity/ground contact at one location, not walking or walls. Evidence:
`docs/settle-retry-2026-10-11.log`. MC later exhausted system memory commitment;
memory stability remains under investigation before enabling movement.

Rendering uses a transparent D3D11 desktop overlay, without Witcher's depth, lighting,
shadows or camera roll. NPC combat, water exchange, destruction, and save binding still
need implementation. See [ADAPTER.md](docs/ADAPTER.md) for the next integration step.

## Build and test

Windows x64, CMake 3.25+, C++20, Windows SDK or MinGW:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

For Visual Studio, omit `-G` and build/test with `--config Release`.
MinGW binaries statically link C++ and threading runtimes to avoid DLL conflicts.
Tests run without either game and do not prove in-game compatibility.

## Independent Minecraft test

Run `build/witchcraft_standin.exe`, then start SkyCraft's Minecraft without Skyrim.
The Python equivalent is `tools/standin.py`, with `--self-test` for its checks.
Do not run multiple hosts together: Skyrim, WitchCraft and the stand-ins share
`Local\SkyCraft_v1`.

The build produces `build/dinput8.dll`; it does not install it into the game.
Preserve the previous DLL when testing a replacement.

`WITCHCRAFT_DISABLE_NATIVE=1` skips the native callback. The DLL has process lifetime;
restart the game to replace or disable it. Diagnostics append to
`%LOCALAPPDATA%\WitchCraft\host.log`. Build with tests enabled to also produce
`witchcraft_native_probe.exe`, which only reads an already running game.
`WITCHCRAFT_DISABLE_TERRAIN=1` keeps the position observer but skips physics queries.

MinHook 1.3.4 sources and license remain under `third_party/minhook-1.3.4` from the
initial investigation. The revised callback does not link MinHook or patch executable
instructions. See `docs/NATIVE_FINDINGS.md` for addresses and limitations.

The bounded Minecraft walking diagnostic passed on 2026-10-11: a 250 ms forward
pulse moved MC 1.073 m on the exported ground, then released and stopped with
on-ground set. Geralt stayed under Witcher control. This validates one short
open-ground walk; walls, jumps, block interaction and native Geralt movement
remain unvalidated. See docs/DIAGNOSTIC_WALK.md and docs/walk-pass-2026-10-11.log.

The refined wall-contact diagnostic also passed on 2026-10-11. MC walked
from 0.956 m wall separation to 0.300 m, was blocked while forward input
remained held, then released and stayed still with on-ground set. Geralt
remains under Witcher control. See docs/DIAGNOSTIC_WALL.md and
docs/wall-contact-pass-2026-10-11.log. This validates one sampled wall contact;
full scenery coverage, avatar projection and native Geralt movement remain pending.

The stationary avatar display prototype passed its first visual test on
2026-10-11. The user confirmed that Minecraft's character stays aligned with
Geralt during camera rotation without flickering. Logs confirm avatar output
at 2560x1440 and successful transparent-window uploads. Geralt is still visible
and under Witcher control; this is the first visible integration prototype,
not a playable replacement. See docs/DIAGNOSTIC_AVATAR.md and
docs/avatar-display-pass-2026-10-11.log.

The first visible native handoff passed on 2026-10-11. Minecraft walked 1.073 m
while Geralt was hidden and followed through five confirmed engine movement
requests. Input released, Minecraft stopped, and the user confirmed Geralt
returned. Engine movement is confirmed on later callbacks, rather than by an
immediate read after Teleport. See docs/DIAGNOSTIC_PILOT.md and
docs/pilot-handoff-2026-10-11.log. This is a brief automatic trial; manual
movement input, jumping, block interaction and longer gameplay remain pending.

The next prototype is a bounded manual W trial with Esc/F8 restoration and
automatic time limits. Its first game attempt failed to capture W: Geralt moved
while Minecraft remained stationary, and the trial restored visibility. The
dedicated-thread retry also received no W events. The revised build intercepts
Witcher's executable raw-input and key-state imports and records filtered reads.
Thirteen offline suites pass. Manual walking and captured F8 restoration are
validated below. See
docs/DIAGNOSTIC_MANUAL.md. Enable it with WITCHCRAFT_DIAGNOSTIC_MANUAL=1.

The raw-input build subsequently passed its first manual forward walk: the user
confirmed walking, and logs recorded 1.073 m displacement, five confirmed native
follow operations, braking and automatic visibility restoration with no native
failure. See
docs/manual-walk-first-success-2026-10-11.log.

The repeat also passed F8 restoration: the user confirmed walking and returning
Geralt worked, and the log recorded a captured stop-key press, removal of the
owned hide bit, and no native failure. See docs/manual-walk-f8-pass-2026-10-11.log.
This is a bounded one-pulse forward trial. Longer movement, side/back controls,
jumping, blocks and scenery occlusion remain pending.

The next build adds a bounded WASD trial with repeated presses, diagonals,
camera-derived heading and F8 restoration. It uses soft time/path/radius stops
and the existing native follow guards. Fourteen offline suites pass. See
docs/DIAGNOSTIC_MOVEMENT.md and enable
WITCHCRAFT_DIAGNOSTIC_MOVEMENT=1.

All four WASD directions passed their first visual check on 2026-10-11. Logs
recorded 39 confirmed native follow operations and 7.381 m of total movement,
then a soft path-limit stop and automatic visibility restoration without native
failure. The user also reported F8 worked, but this capture recorded automatic
return and no captured stop key; explicit F8 restoration remains proven by the
earlier single-W trial. Diagonals and camera rotation still need game checks.
See docs/wasd-directions-pass-2026-10-11.log.

The subsequent diagonal/camera trial also passed: W+D, S+A, camera rotation then
forward movement, stable alignment without flickering, and captured F8 restoration
were confirmed. Logs recorded 14 native follow operations and 2.837 m total travel
without native failure. See docs/wasd-diagonal-camera-pass-2026-10-11.log.
The bounded movement prototype is validated; jumping while walking, extended walking,
block interaction and scenery occlusion remain pending.

Repeatable standing jumps passed the live test on 2026-10-11. Two Space presses
produced complete 1.249 m jumps and stable landings; Minecraft stayed active until
F8 restored Geralt, with no native failure. All fifteen offline suites pass.
The hidden Witcher camera anchor stays on the ground; native vertical follow and
jumping while walking remain pending. See docs/DIAGNOSTIC_JUMP.md and
docs/jump-standing-pass-2026-10-11.log.







