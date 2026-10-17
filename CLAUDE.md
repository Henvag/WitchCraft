# WitchCraft — project handoff for Claude

Handoff written 2026-10-17. This is a development snapshot, not a claim that the
requested features are finished. Recheck Git, source, installed binaries and logs
before making changes. Some older README entries describe historical prototypes.

## Goal and user priorities

Make The Witcher 3 playable through Minecraft, similar to
[SkyCraft](https://github.com/chasmlol/SkyCraft), which integrates Minecraft with
Skyrim. Minecraft should own normal player movement, physics, animation, inventory
and block interactions while Witcher supplies the surrounding world.

The user wants a coherent playable build: smooth movement, reliable third/first
person, creative flight, block placement/breaking, water and swimming, mining into
Witcher terrain, and explosions that visibly destroy that terrain. Frequent
unexpected returns to Geralt and falling through the ground are major defects.
The user has already endured many small install/test/restart cycles. Prefer
complete, evidence-based fixes and combined tests over another sequence of tiny
tolerance changes. Do not describe passing offline tests as proof of smooth or
glitch-free gameplay.

## GitHub and local checkout

- Repository: **https://github.com/Henvag/WitchCraft** (private at handoff).
- Remote: `https://github.com/Henvag/WitchCraft.git`.
- Branch: `main`.
- Checkout: `C:\Users\henva\Projects\WitchCraft`.
- Last implementation commit at handoff: `7951ff5`,
  `Tolerate small controller deviations in play follow acknowledgements`.
- Prior commits include `8d1c450` (F8 re-entry) and `a7ca5f3` (sampled occlusion).
- GitHub is already configured. Use the existing authenticated environment; do
  not ask the user to paste passwords or tokens into chat or this file.

**There is substantial uncommitted local implementation work.** A GitHub clone at
the commit above does not contain it. Start with `git status --short` and
`git diff --stat`, then inspect the actual diff. Preserve this work; do not reset
or overwrite it. This handoff describes both that local work and the older
installed build, explicitly distinguished below. If working on another machine,
obtain the local changes before assuming they are available.

## What has actually worked in game

The user has confirmed these in earlier builds, though integration remains fragile:

- Minecraft avatar replaces/hides Geralt, with stable third-person alignment.
- Camera-relative WASD, diagonals, standing and moving jumps.
- HUD/hotbar, number/wheel selection, inventory display and item clicks/drags.
- T chat and `/gamemode creative` / `/gamemode survival`.
- Minecraft block placement, sharper textures, walking onto and off small block
  platforms without immediately restoring Geralt.
- Bucket water staying on sampled ground and spreading instead of falling through.
- Creative flight and Witcher camera height following flight.
- F5 first-person view and returning to third person.
- F8 returning to Geralt and F8 entering Minecraft again without restarting.

Controls: WASD, Space jump; creative double-tap Space toggles flight, Space rises,
Shift descends; E inventory; T chat; F5 perspective; F8 handoff toggle. Re-entry
starts at Geralt's position and clears saved flight while preserving creative mode.

Reported problems: spontaneous handoff cancellation around water, native animation
or controller changes; missing terrain support/fall-through; residual movement
drag; placed water visible through native scenery. Sampled occlusion was added,
but full-scene occlusion is not implemented or verified. Earlier rendering work
raised frame delivery substantially; the user still noticed some drag.

**Not finished:** native Witcher lake/river swimming; real Witcher terrain mining
and explosion excavation; robust Elytra flight; native NPC/combat integration;
complete scenery collision/occlusion. Minecraft-placed water support is not native
lake integration. An offline excavation helper is not working terrain destruction.

## Architecture and useful files

| Area | Files / responsibility |
| --- | --- |
| Loading | `host/proxy/dinput8.cpp`: proxy DLL loaded by Witcher DX12. |
| Native observation | `host/NativeAdapter.cpp`, `host/NativeLayout.h`: validated game objects and game-thread callbacks. |
| Geralt/anchor control | `host/NativePilot.cpp`: owned visibility bit and native movement requests. |
| Camera | `host/NativeCamera.cpp`: verified custom-camera data hook, first/third person and altitude translation. |
| Play lifecycle | `host/PlaySession.cpp`: handoff, Minecraft inputs, terrain waits and recovery boundaries. Legacy diagnostics are separate. |
| Input | `host/ManualGameInput.cpp`, `host/ManualInput.cpp`: game raw-input/key-state interception, virtual GUI cursor, text and toggles. |
| Collision | `host/NativeCollision.cpp`, `host/Terrain.cpp`, `host/Walls.cpp`, `host/CollisionTiles.cpp`, `host/TerrainVoxels.cpp`. |
| Shared memory | `host/Link.cpp`, `host/GameBridge.cpp`, `protocol/skycraft_protocol.h`. |
| Rendering | `host/World.cpp`, `host/Overlay.cpp`: D3D11 rendering/readback and transparent desktop overlay over Witcher. |
| Smoothing | `host/MotionTimeline.cpp`: buffered Minecraft tick history on the shared clock. |
| Minecraft patch | `minecraft-patch/src/dev/skycraft/client/`: patches selected SkyCraft client classes. |
| Native research | `docs/NATIVE_FINDINGS.md`, `docs/ADAPTER.md`, `tools/`. |

Witcher remains a DX12 game. The current compositor is an external D3D11 layered
window, **not** a native Witcher render/depth hook. Sampling nearby surfaces into
the overlay depth buffer can hide Minecraft geometry behind those surfaces. It
cannot cut a visible hole in the Witcher image or supply full-scene depth.

Minecraft runs vanilla simulation in a hidden SkyCraft client. Local Witcher
geometry is sampled through verified PhysX queries and streamed as triangles and
voxel support markers. The hidden Geralt remains a native camera anchor subject
to Witcher's controllers and animations; exact follow acknowledgement failures
have repeatedly ended play. Avoid making Minecraft movement depend on Geralt
performing the same jump/swim/fall animation.

The bridge uses SkyCraft protocol v11 and `Local\SkyCraft_v1`. Do not run Skyrim,
the stand-in host and WitchCraft simultaneously. Local WIP adds flag meanings
without changing the binary layout; matching host and patched JAR are required.
Review both ends before changing protocol fields, coordinate conversions or clocks.

## Uncommitted implementation at handoff

This work builds and passed offline tests, but **has not been installed or verified
in game as a combined build**. Review it before continuing:

1. **Continuous play follow:** `PlaySession`, `NativePilot`, `GameBridge` and camera
   changes remove several old short-trial jump/fall/time limits from camera-owned
   play. Native follow retargets bounded steps instead of waiting for stale exact
   positions. Larger lag holds input for catch-up rather than immediately returning
   Geralt. Rejected engine requests retry with backoff; owned hiding is reapplied.
   Identity, world, connection, focus and other validity checks still matter.
2. **Terrain continuity:** centre-based triangulation preserves valid partial
   surfaces, permits measured slopes and queries ground farther down. Sampling
   follows Minecraft's horizontal position; nearby replacement collision is sent
   before stale removals. This is still bounded sampled geometry with real gaps.
3. **Transient camera state:** briefly reuse a verified camera snapshot only for
   matching player/world/camera identities. A completely occluded or offscreen
   Minecraft mesh no longer by itself ends play.
4. **Minecraft recovery:** new `GroundRecovery.java`, `TerrainGuard.java` and
   `GroundRecoveryTest.java`. Host flags enable managed terrain and physics holds.
   Recovery detects crossing sampled support or a missing-ground fall, and can
   restore a recent supported position. Flight, water, real block support and
   known lower ground need distinct treatment. Queued server corrections validate
   generation/player/dimension. This is a fallback, not complete world collision.
5. **Excavation foundation only:** new `host/Excavation.h` validates `RenDug`
   packets and stores bounded dug sections; `host/ExcavationClip.h` subtracts unit
   cells from triangles with bounded output. `tests/ExcavationTests.cpp` tests the
   helpers. They are **not wired to a native renderer/physics excavation backend**.
   `tools/excavation_inventory.py` provides read-only executable inspection.

Affected tracked files include `CMakeLists.txt`, the host modules above,
`protocol/skycraft_protocol.h`, Minecraft `SkyClient.java`/`build.ps1`, and related
tests. New untracked files must be included deliberately when committing; a plain
tracked-files-only commit would omit essential recovery classes and tests.

## Swimming: concrete next integration

SkyCraft already has `SkyWater` and a protocol `WaterGrid`: a 16x16 grid of native
water heights, origin, world identity and a no-water sentinel. `Link::WriteWaterGrid`
exists. WitchCraft does not yet provide a real native-water sampling adapter.
Upstream uses this data for entity water physics while leaving native water
rendering to the host game. Vanilla Minecraft can then provide swimming pose,
buoyancy and drowning; do not fake a swimming pose independently of fluid state.

The installed Witcher scripts declare:

```text
content/content0/scripts/engine/world.ws
GetWaterLevel(point: Vector, optional dontUseApproximation: bool): float
```

This is a research lead, not a verified callable native address. Locate and verify
the actual implementation/ABI before calling it. Sample actual surfaces, distinguish
shore/land from water, and reset grid data on world changes. Inspect the upstream
water reader's world validation; its seqlock reader alone does not establish that
the grid belongs to the current world. A guessed global water plane is inadequate.

## Terrain mining and explosions: missing backend

Terrain currently lacks diggable permission because WitchCraft cannot yet remove
the native visible surface and native collision together. Upstream Minecraft
`SkyDig`/`SkyDigBlast` and dug-cell packets are useful, but Skyrim's native mesh and
physics integration cannot simply be copied into Witcher.

Implement and verify all three effects coherently: native visible holes/cuts,
native collision removal, and updated Minecraft collision/dug state. Enabling a
diggable flag or removing only exported triangles leaves intact visible terrain
and can cause fall-through. Overlay depth occlusion cannot provide excavation.

Read-only executable research found RTTI leads `CTerrainTile`,
`CRenderProxy_Terrain`, `CRenderTerrainUpdateData` and terrain-hole strings. Their
methods and update lifetimes remain unverified. Heightfield holes may also be
insufficient for arbitrary caves/tunnels. Preserve world identity, persistence,
capacity limits and recovery when designing mining/explosion updates.

Upstream source is locally available at:
`C:\Users\henva\Documents\New project\WitchCraft-work\tools\SkyCraft-source`
(reference commit `bfcaf178524b92c2cdeb88e4ce0f13ef9ded6f32`, MIT).
Compare upstream `skse/src/Game.cpp`, digging mesh/physics code, and Fabric
`SkyWater`, `SkyDig`, `SkyDigBlast`, `SkyCollision` and `TriCollider`.

## Build and validation

Native: Windows x64, C++20, CMake 3.25+, MinGW or Windows SDK/MSVC. The local
`build` directory is already configured. From the repository:

```powershell
cmake --build build -j 6
ctest --test-dir build --output-on-failure
```

For a fresh MinGW checkout, configure first with
`cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`.
For Visual Studio use its generator and `--config Release` as appropriate.

The Minecraft patch targets the installed SkyCraft 0.1.2 / Java 25 environment.
Build against the **original** JAR, not an already patched installed JAR:

```powershell
& ./minecraft-patch/build.ps1 -BaseMod 'C:\Users\henva\Documents\New project\WitchCraft-work\minecraft-backup-20261011-184302\skycraft-0.1.2.jar'
& ./minecraft-patch/test-ground-recovery.ps1
```

The script checks the original SHA-256 and preserves unrelated upstream entries.
Outputs: `build\dinput8.dll` and `dist\minecraft\skycraft-0.1.2.jar`.
Last completed development validation: **24/24 native CTest suites passed**, Java
recovery tests passed, patched JAR compiled. These results precede this handoff;
rerun after code changes. They do not establish live compatibility or smoothness.

## Local runtime, installation and evidence

- Witcher DX12 directory:
  `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12`.
- Target executable: `5.0.0.1048522`; PE timestamp `0x6ac555a3`, image size
  `0x06402000`. Native addresses/signatures are build-specific. Validate before use.
- Minecraft instance:
  `%LOCALAPPDATA%\SkyCraft\Prism\instances\SkyCraft\.minecraft`.
- Prism: `%LOCALAPPDATA%\SkyCraft\Prism\prismlauncher.exe`.
- Local staging/backups/scripts:
  `C:\Users\henva\Documents\New project\WitchCraft-work`.
- Staging installer: `install-diagnostic.ps1 -Play`; enables `WITCHCRAFT_PLAY=1`.
  Inspect its manifest/hash checks first. A rebuilt DLL makes an old manifest stale.
- Correction (2026-10-17): the installed DLL and JAR already match the combined
  build (DLL hash equal, all client classes byte-identical), installed 13 Oct and
  played ~50 min that night. Staging manifests/rollback JSON predate it, so
  `install-diagnostic.ps1` refuses until they are refreshed. Check actual hashes
  before installation; preserve existing rollback files and create fresh backups.
- No PowerShell 7 here: `Start-Process -Environment` fails on 5.1. Launch with
  `$env:WITCHCRAFT_PLAY='1'; Start-Process <witcher3.exe>` instead.
- `game-rollback.json` and `minecraft-rollback.json` in staging record backups.
  Windows must release the loaded DLL before replacement. Close Witcher normally;
  allow the Minecraft client time to exit. Do not force-kill a running save session.
- Replace the instance's patched JAR; do not install a second SkyCraft JAR beside it.
- Prism has needed `-Xms512m`, `-Xmx2048m`, `-XX:HeapBaseMinAddress=32g` and
  `-Dskycraft.startHidden=true`. Recheck its effective configuration if JVM/native
  memory errors recur; avoid exposing account tokens from process command lines.
- Host log: `%LOCALAPPDATA%\WitchCraft\host.log` (append-only; find the last
  `WitchCraft host loaded` when analyzing a run).
- Minecraft log: instance `logs\latest.log`.
- Read-only probe: `build\witchcraft_native_probe.exe --link`, `--diagnose`, or
  `--sample-follow <seconds> <output.csv>`.
- Earlier failure evidence: staging `unstable-play-20261012.log`. Repeated native
  follow errors and a roughly 2.2 m lag stop motivated the continuous-follow work.

Only source/documentation belong in GitHub. Game assets, saves, binaries/JARs,
local diagnostic logs and credentials are excluded. Some paths referenced here
exist only on this computer and are not included in a remote clone.

## How to continue

Review and finish the local stability work, validate the combined native/JAR build,
then test a coherent route in a disposable outdoor save: walking/slopes, jumps,
block platforms, water edges, creative flight, F5, GUI/chat and F8 re-entry. Measure
fresh Minecraft frames, host presentation, follow lag and terrain delivery
separately; a 60 Hz loop does not prove 60 fresh frames or low latency.

Then connect native-water sampling and swimming, and implement the actual native
excavation backend. The user wants both; neither should be presented as delivered
by the existing offline helpers. Keep explicit restoration for F8 and genuinely
invalid sessions, while fixing ordinary transitions that unnecessarily cancel play.
Report what was implemented, what was built/tested, what was installed, and what
still needs live validation as separate facts.
