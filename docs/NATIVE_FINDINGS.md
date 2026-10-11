# Native integration findings

Evidence collected from the user's running disposable outdoor save on 2026-10-11.
This is an exact-build investigation, not a general Witcher SDK. The installed
`bin/x64_dx12/witcher3map.json` identifies symbols. Add 0x1000 to its `0001:` offsets
to obtain executable RVAs. ASLR requires adding the current module base at runtime.

## Build and reads verified externally

Executable version 5.0.0.1048522, timestamp 0x6ac555a3, SizeOfImage 0x06402000.
NativeLayout.ValidateCode checks seven code signatures. NativeLayout.Snapshot
checks CR4Game, CR4Player and CR4CameraDirector vtables and all required memory
reads, then rejects nonfinite positions and unreasonable direction/FOV values.
The read-only probe verified this same implementation against the running game.
Unit tests check missing memory, altered signatures, wrong vtables, null worlds,
pause state, nonfinite camera data and the provisional coordinate transform.

| Read | RVA or field |
| --- | --- |
| CScriptingSystem.AdvanceThreads | 0x270b150, void(system, float delta) |
| Script globals pointer | 0x5a7da70 |
| Global object i | *(*(globals + 0x20 + 8*i) + 8) |
| CR4Game / CR4Player / CR4CameraDirector globals | 0 / 1 / 2 |
| Corresponding vtable RVAs | 0x37b41d0 / 0x381e630 / 0x3810308 |
| Player world position | player + 0xa0, three floats |
| Camera position | camera + 0x70, three floats |
| Camera forward | camera + 0x790, three floats |
| Raw camera FOV | camera + 0xa0, float |
| Pause count | game + 0x160, uint32 |
| World | *(*(game + 0xf0) + 8) |

Example live readings: player (1070.611,1795.544,1.621), camera
(1069.553,1793.316,3.555), direction (0.6321,0.7553,-0.1733), raw FOV 67.
Witcher is Z up. Provisional mapping to Minecraft: (x,z,-y), scale 1.
Projection convention, scale against landmarks, roll, scene flags and camera
timing must be validated before this frame can drive rendering or gameplay.

## Runtime RTTI and dispatch

Object +0x18 holds CClass; parent +0x10; name index +0x2c; native/script function
array +0x48 with count +0x50. CFunction name index +0x10, flags +0x18, native index
+0xe0. Names pool singleton is at RVA 0x5857dd8; table at pool+0x11830; each name
index addresses an eight-byte holder pointer whose first pointer is ANSI text.

Native flag bit 0 selects native dispatch. Native flag bit 1 selects the opcode
table at RVA 0x5a75a70, stride eight. Otherwise native records at 0x5a5da70 have
stride 24; function pointer first, this adjustment at +8. Index +0xe0 is an index,
not a pointer. CScriptStackFrame code pointer is +0x30. Avoid manufacturing script
frames until all parameter opcodes, out references and ownership are verified.

| Wrapper discovered through live RTTI | RVA |
| --- | --- |
| CNode.GetWorldPosition | 0x2267e50 |
| CCameraDirector.GetCameraPosition | 0x2382890 |
| CCameraDirector.GetCameraDirection / GetFov | 0x23826f0 / 0x23826d0 |
| CGame.IsPaused / GetWorld | 0x227a2d0 / 0x2277c80 |
| CEntity.Teleport / SetHideInGame | 0x225b290 / 0x22558f0 |
| CWorld.StaticTrace / StaticTraceWithAdditionalInfo | 0x22a3640 / 0x22a3100 |

Teleport's wrapper ultimately calls entity vtable+0x460. StaticTrace's wrapper
calls the physics object at world+0x1f0, vtable+8, with point vectors, a group
mask, a zero optional argument and an output structure. Default groups are
looked up through RVA 0x5a58ec0 and two name constants. Result code 1 is a hit;
position and normal occupy the first two 16-byte vectors of the output.
Full output layout, masks, lifetimes and error semantics need verification.
The initial observer called no physics methods. The terrain build now calls the
verified underlying ray entry, after checking its code, physics vtable and group
names. Teleport and visibility methods remain unused.

## Terrain query implementation and runtime test

The subsequent wall diagnostic build generalizes this same ray ABI to horizontal
segments; it calls no new native entry point. Segment validation checks finite
positions/normals, a near-unit normal and a hit on the supplied segment. WallPatch
uses 32 angular directions at nine half-metre height levels plus centre rays
(544 total), within eight metres. Normals, plane agreement and edge lengths reject
cells that span changes of surface. This remains an approximation. The outdoor
wall test generated 14 combined patches, nine containing wall triangles; the final
stationary location repeatedly exported 136 wall triangles. The live link reported
267,136 bytes written and consumed, zero pending, with fresh heartbeats. No query
validation failures were logged. See wall-test-2026-10-11.log. These checks verify
querying, mesh generation and transport, not complete geometry or MC movement.
The combined ground/wall job has 1,089 rays, retains the eight
ray/one-millisecond between-call budget and publishes both meshes together.
The subsequent receiver review found that the earlier single-patch packets were
indexed only under their minimum-corner cube. CollisionTiles now publishes the
combined mesh across aligned eight-metre cubes instead. See RECEIVER_FINDINGS.md;
the earlier runtime byte counters alone did not validate local collision lookup.

Live physics object: CPhysicsWorldPhysXImpl, vtable RVA 0x3828068. Slot +8 points
to RVA 0x1e965e0. Disassembly confirms six arguments: physics, start Vector4,
end Vector4, include-group mask, zero exclude mask, output. Status 0 is a miss,
1 is a hit and 2 means queries are temporarily unavailable. The output contains
position at +0, normal at +16, fraction at +32, metadata and a borrowed component
pointer at +48, and optional output pointers at +64/+72. The adapter initializes
these pointers to null and allocates 96 aligned bytes, covering the wrapper's
88-byte output initialization. It does not retain component pointers.

The live group table contains 30 entries. Terrain is index 5 and Static index 1,
but the implementation resolves names rather than relying on those indices.
Dynamic bodies, doors, platforms and water are not included in this ground patch.
The query rejects out-of-ray hits, invalid normals, nonfinite data and unexpected
status values. Invalid query results disable sampling for the process.

TerrainPatch uses 289 corners and 256 centre samples over a 16-by-16 metre area.
Rays run from feet height +2 to feet height -8 in Witcher's Z-up space. It emits
upward triangles in Minecraft coordinates only where all five cell samples hit,
normals face up, height range is at most one metre and the centre matches the
chosen diagonal within 0.15 metres. No synthetic plane fills missing samples.
This is a sampled approximation, not full engine collision. Narrow gaps between
samples and stacked surfaces remain unsupported, so gameplay stays inactive.

Per-observation work is capped at eight rays and checks a one-millisecond budget
between calls; an individual engine query can exceed that budget. Completed
patches refresh after five seconds or movement outside their inner coverage.
World changes clear the collision epoch, and queue backpressure retains packets.
Four automated tests pass, including terrain winding, slopes, holes, cliffs and
invalid-data rejection. WITCHCRAFT_DISABLE_TERRAIN=1 skips queries for diagnosis.

The outdoor-save test verified the native query call and stationary patch refresh.
Geralt's movement was recorded afterward. Exported patches included 505/545 ray hits with
434 triangles, then 545/545 hits with 510 triangles. No result-validation failure
was logged. The user completed the test without reporting a crash or stutter;
this is not a frame-time benchmark. The early patch contained 40 misses; missing
cells were omitted rather than replaced with synthesized ground. A follow-up
stationary check after movement exported a patch at the new location:
545 hits, 510 triangles, MC region (1053,-8,-1852) to (1069,3,-1836).
Sustained fast movement can
restart an unfinished patch and requires improved streaming before gameplay.

Minecraft had automatically closed when Witcher was restarted earlier, so it was
relaunched through the existing Prism SkyCraft instance. Its log confirmed link
attachment and collision epoch 1. A read-only shared-memory check found current
host/client heartbeats and collision counters written=58296, consumed=58296,
pending=0. SkyFlags stayed menu/open handoff (2). This verifies transport
consumption; no Minecraft walking, collider application, wall coverage or avatar
replacement was tested. The follow-up check found written=99256, consumed=99256,
pending=0 after the new-location patch. Evidence is in terrain-test-2026-10-11.log.

The read-only native probe now supports --link for header, heartbeat, epoch and
collision-ring counters, without accessing or calling engine objects.

## Observation callback and first runtime test

The initial AdvanceThreads hook installed but produced no samples during the
user's walk/orbit/pause test. An external read-only probe then confirmed valid
player/camera objects and changed position/direction. The hook's existence did
not prove that this entry ran; the original observer also selected its first
thread before a valid sample, which could exclude a later game thread.

The revised observer replaces the first pointer of native record 0x8a3 at
RVA 0x5a5da70 + 24*0x8a3. Installation waits until registration matches the exact
GetWorldPosition wrapper, with zero this adjustment and writable memory. Atomic
compare/exchange preserves other hooks. The callback delegates all three original
arguments and preserves the result, then observes only calls on CR4Player. It
serializes observations without locking itself to a loading thread. No executable
code is patched and MinHook is no longer linked.

The restarted outdoor-save test succeeded. A captured set of 25 periodic records
contains seven distinct feet positions, eleven camera directions, both pause
values 0 and 1, and a single calling thread (26896). World sequence stayed 1.
Raw FOV settled from 60 through 63.517 to 67 after load. Evidence is preserved in
runtime-observation-2026-10-11.log. The final records were paused while the user
returned to the chat, so pause-menu resume and focus-induced pause were not
isolated from each other. No movement/visibility/collision operation was tested.

Memory reads use
ReadProcessMemory on the current process so inaccessible pointers fail. It
publishes an explicit scripted handoff, with no collision ready flag. No input
capture or Geralt hiding is enabled. The DLL is pinned for process lifetime.

The external probe's --diagnose mode permits only the AdvanceThreads code mismatch
left by the first installed hook, while retaining the fingerprint and other code
checks. It never writes memory or calls engine functions. Default mode requires
all original signatures. The revised dispatch callback leaves code unchanged.

Next validation: isolate pause-menu resume from focus loss, then verify collision
query ABI, projection and a frame update point suitable for control; implement
terrain and obstacle coverage and scene handoffs before
enabling Minecraft physics. Ground ray samples alone cannot cover walls/caves.

MinHook upstream: https://github.com/TsudaKageyu/minhook/releases/tag/v1.3.4.
Downloaded tag ZIP SHA256:
172708123DAA0C98D20D3A980B16A50BE14AF243DC95DEE6F79C24193AD010E4.
