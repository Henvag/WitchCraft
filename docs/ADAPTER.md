# REDengine adapter contract

`host/GameBridge.h` defines the version 1 Windows x64 interface exported by
`dinput8.dll`. Resolve its three undecorated functions with `GetProcAddress`.
GameFrame is 96 bytes and GameControl is 48 bytes with default x64 alignment.
Initialize size/version on every call. Do not pass REDengine object pointers.

## Game-thread update

1. Submit real collision with WitchCraftSubmitCollision. On world transition, send
   kColClear with the new uint32 epoch, then kColRegion/kColTris using SkyCraft layouts.
   Return 0 means rejection or backpressure; retain and retry valid packets.
   Clear/regions remain ordered. Set kGameCollisionReady only when all nearby spawn
   collision has been submitted. Control also waits for the queue to drain.
2. Submit GameFrame with world identity, epoch, player feet, actual camera position,
   normalized forward, effective vertical FOV and time. Update preferably every game
   frame, at least every 250 ms. Increment relocation for loads, fast travel, doors
   and external teleports, but never for host-driven movement.
3. Read GameControl. When active, apply feet to the player anchor, suppress Witcher
   locomotion/combat input, hide Geralt's mesh/components, and keep Witcher's camera
   following the anchor. Minecraft owns physics. Mirror swimming, flight, sprint
   and crouch flags where REDengine state needs them.
4. When inactive, restore previous visibility and normal locomotion. Poll even if
   Minecraft is absent; restore on adapter unload. Publish handoff flags for menus,
   loads, cutscenes, dialogue, mounts, death and scripted sequences. Returning to
   play begins a new handshake at Witcher's actual feet.

Submit/read return 0 for size/version errors. Frames reject invalid camera vectors,
NaN/infinity and unreasonable FOV. Control becomes inactive after 250 ms without
frame or control publication. Structures are copied under a mutex. The host worker
never invokes REDengine functions.

## Coordinates and rendering

All coordinates use metres and Minecraft Y up. A candidate transform for REDengine's
Z up is `mc = (witcher.x, witcher.z, -witcher.y)`, one unit per metre. Verify scale,
orientation and origin against landmarks before enabling movement. Apply the same
transform to player, camera and collision; inverse-transform motion. Never use
SkyCraft's Skyrim factor of 70 for Witcher. Determine if engine FOV is horizontal or
vertical and convert it before submission. MC yaw/pitch are derived from real camera
forward, so aiming follows Witcher instead of an independent mouse sensitivity.

The desktop overlay still lacks game depth/lighting and roll. A native render hook is
needed for occlusion by Witcher scenery. Default framing is Witcher's third person;
Minecraft F5 is consumed and behind-the-back mode is selected automatically.

## Remaining engine work

`NativeAdapter.cpp` bootstraps a diagnostic native dispatch callback for the player's
GetWorldPosition calls. `NativeLayout.h` checks seven exact signatures and three
live vtables before reading engine fields. The external probe has verified them
against executable 5.0.0.1048522. The revised callback recorded live movement,
camera changes and pause state during the restarted outdoor-save test.
These observations are published with kGameScripted set, keeping control disabled.
Raw FOV is provisional and never used for rendering in this phase. Collision ready
is never set. See NATIVE_FINDINGS.md for the discovered native dispatch and layouts.

Teleport, visibility and static trace wrappers have been located through live RTTI
and disassembly. Teleport and visibility methods remain unused; their calling
conventions and lifecycle behavior still require validation.
The new NativeCollision implementation matches StaticTrace's six-argument PhysX
call and guards its signature/vtable/groups. TerrainPatch builds diagnostic height
triangles and submits ordered collision packets, without declaring collision ready.
Native queries and collision-ring consumption were validated in the outdoor-save
test (see terrain-test-2026-10-11.log). Minecraft walking remains untested. These patches exclude
walls, dynamic bodies, doors, water and stacked surfaces.
TerrainSchedule now retains partial samples and packets awaiting delivery during
ordinary movement. It recentres only after successful publication, or immediately
on world/relocation changes. This prevents movement from repeatedly restarting the
545-ray job. The outdoor walking test completed 24 patches across movement and
the final stop, without query-validation failures. Minecraft was closed during
the walk; after restart it consumed all 484,896 queued bytes with nothing pending.
See streaming-test-2026-10-11.log. This does not guarantee collision coverage ahead
of a moving player; tile caching, obstacle coverage and walking on colliders still
need implementation/validation.
The wall build adds WallPatch's 544 horizontal rays: 32 directions, nine height
levels and centre samples, with an eight-metre radius. The origin is the same
integer XY centre as the ground patch. Only five-hit cells with matching planes,
normals facing the origin and bounded edge lengths emit triangles. Ground and wall
triangles are combined before packetization; sampling bounds include wall height.
These are nearest static surfaces only, using the existing Terrain/Static groups.
Doors, fences in other groups, bodies, stacked surfaces and occluded obstacles
remain unsupported. Gameplay and collision-ready flags stay disabled. The wall
test covers segment validation, winding, angular wraparound, holes and changes
of surface. The outdoor wall test generated 14 combined patches, nine with wall
triangles. The final stationary location repeatedly exported 136 wall triangles.
Minecraft consumed 267,136 bytes, with nothing pending and no logged query
validation failures. Evidence: wall-test-2026-10-11.log. This validates native hits,
mesh generation and transport; geometric completeness and Minecraft movement
against those surfaces remain untested.

## Receiver indexing correction

The installed skycraft-0.1.2.jar's SkyCollision.readTris stores each triangle
packet under floorDiv(minX,8), floorDiv(minY,8), floorDiv(minZ,8) only; lookup scans
nearby eight-metre cube keys. The earlier host sent one 16-metre patch as one
packet. Transport success did not mean all those triangles were discoverable.
CollisionTiles now duplicates each triangle into every cube touched by its AABB
and sends aligned eight-metre headers. Ground and wall triangles share each cube.
Empty packets clear obsolete cube contents. Pending packets remain ordered under
backpressure, with at most four submissions per observation; accepted keys survive
an interrupted batch so the next completed patch can clear them. A world reset
discards this bookkeeping after ordering a collision clear. Six tests pass,
including receiver lookup outside the old minimum-corner cube, negative boundaries,
empty updates, interrupted batches and malformed meshes. The runtime cube test
completed 15 batches of 6–23 cube updates, ten containing wall surfaces. Minecraft
consumed all 354,896 bytes, with nothing pending and no logged validation failures.
See cube-test-2026-10-11.log. Actual Minecraft movement and collision resolution
against those surfaces still need validation.
The initial AdvanceThreads hook produced no samples during the save test and was
replaced. Native getter calls are not yet proof of a fixed frame tick or safe timing
for physics queries or movement; those require a separate update integration.

Stream real terrain, walls, stairs and dynamic obstacles ahead of the player.
Ground samples alone cannot represent caves or walls. Empty regions replace removed
geometry. Keep terrain digging disabled until Witcher collision and visuals both
change persistently. Additional bridges are needed for water, NPCs/combat, quests,
save/world association and game depth/lighting. They are not supplied by these exports.
Protocol worldId alone does not associate a Minecraft world with a Witcher save.

## In-game validation

After a verified adapter exists, check landmark transforms and camera projection;
walking, stairs, jump, crouch, sprint, swimming and flight; place/break blocks while
moving; inventory/focus loss; dialogue/cutscenes; loading/fast travel; MC disconnect
and reconnect. Confirm Geralt becomes visible/responsive on every handoff and unload.
C++ compilation and unit tests cannot replace these checks.
