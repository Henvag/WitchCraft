# WitchCraft

Play The Witcher 3 as a Minecraft player. Same split as SkyCraft: Minecraft runs hidden and
owns movement, inventory, blocks and combat. The host owns the world.

## Link

`protocol/skycraft_protocol.h` is SkyCraft's shared-memory layout, version 11, mapping
`Local\SkyCraft_v1`. WitchCraft does not invent a second protocol. The Fabric mod already
speaks this one.

The host creates the mapping. Each frame it writes:

- where the player should be after a load or a door
- look direction
- collision near the player
- nearby characters, as invisible Minecraft hitboxes
- keys and mouse

Minecraft writes back the real feet position, what was hit, explosions, and the block meshes
to draw.

## Stand-in

`tools/standin.py` is the host until the Witcher plugin exists. It publishes one stone floor
and forwards the keyboard while its window is focused. That is enough to prove Minecraft
connects, walks, places blocks and detonates TNT on something that is not Skyrim.

## Witcher plugin

Current status: GameBridge and Session implement the host boundary; see ADAPTER.md.
The executable-specific observer reads real player/camera state through a guarded
native getter callback, validated in an outdoor save. A new ground sampler streams
diagnostic height triangles; its native queries and Minecraft packet consumption
have been validated in the outdoor save.
Movement remains disabled pending full collision and scene integration. Its desktop overlay
is not a native depth-tested render hook. The milestones below are intended work.


Once The Witcher 3 is installed, the stand-in becomes a DLL next to `witcher3.exe`.

1. **Link.** The DLL creates the mapping and heartbeats. Minecraft connects. Geralt does not
   move yet.
2. **Walk.** Export collision around Geralt (ray casts first, real physics shapes later) and
   move his actor to Minecraft's position every frame. One block is one metre until a measurement
   in the installed game says otherwise. REDengine is Z-up; the axis swap matches the protocol's
   Skyrim mapping and gets checked against a known landmark.
3. **Draw.** Composite Minecraft's block meshes and HUD into the frame, depth-tested against
   the game. The installed exe decides whether that hook is D3D11 or D3D12.
4. **Fight.** Mirror nearby NPCs into the actor table. Minecraft damage becomes a Witcher hit.
   A Witcher hit becomes Minecraft damage.
5. **Dig.** Mark Witcher ground as diggable triangles so TNT and the pick leave a real hole.

Novigrad is the first place worth filming: elytra over the market, then TNT in the square.

