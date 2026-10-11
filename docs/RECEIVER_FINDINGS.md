# Minecraft collision receiver review

Reviewed on 2026-10-11 before enabling movement. Public source reference pinned to
[SkyCraft bfcaf178](https://github.com/chasmlol/SkyCraft/blob/bfcaf178524b92c2cdeb88e4ce0f13ef9ded6f32/fabric/src/main/java/dev/skycraft/world/SkyCollision.java).
Installed skycraft-0.1.2.jar SHA256:
EA429C73DCEC49D39B6A0BB9B17AD08A6B0290A39A96CCF6F6C07D581CF992D9.
The bundled Java 25 javap -c -p confirms the same readTris indexing in the installed
binary; its output is retained in the staging tools/skycraft-reference directory.

readTris indexes a complete packet under one key, calculated by floorDiv of each
minimum coordinate by eight. Maximum bounds do not make triangles discoverable
under other keys. trianglesNear searches only cube keys overlapping the query
box and then checks individual triangle bounds. Empty packets overwrite a cube's
triangle array with an empty array (the Java map retains that empty key).

The host's initial 16-metre packets therefore validated querying, mesh generation
and byte transport, but could not supply reliable collision lookup around Geralt.
CollisionTiles corrects this without changing the shared protocol or Minecraft JAR:
each triangle is copied to all cubes touched by its bounds, including negative
coordinates and exact boundaries. Each header uses an aligned minimum and
inclusive maximum minimum+7. Ground and wall surfaces share a cube packet.
Previously accepted cubes absent from a new patch receive empty packets. During
backpressure/interruption, accepted keys are tracked independently of pending
work so old collision can still be cleared. A world change uses the ordered clear.

The sixth test mirrors the installed receiver's key lookup and covers cross-cube
triangles, negative boundaries, stale removal, interruption, invalid mesh rejection
and a regression at the observed player coordinates. This verifies packet indexing
in a receiver model. It does not prove live Minecraft collision resolution,
complete scenery coverage or a safe Witcher movement update point.

The corrected build's outdoor test completed 15 batches, with 6–23 cube packets
per batch including empty replacements; ten batches contained wall surfaces.
The live mapping reported 354,896 bytes written and consumed, zero pending and
fresh heartbeats. No ray/tile validation failures were logged. Evidence is saved
in cube-test-2026-10-11.log. This validates batch completion and transport in the
running games; actual Minecraft movement/triangle resolution remains untested.

Further client review found that SkyClient.holdUntilReady requires KNOWN_REGIONS
at the spawn block, the block below, and eight metres below. readTris does not set
these; only readRegion does. The next build sends empty voxel-region packets for
every cube in the sampled bounds before its triangles. These mark arrival without
inventing solids. With no solid voxels, holdUntilReady waits its six-second timeout
before releasing the player; liftOutOfGeometry uses the exact triangle ground.
Therefore diagnostic settling requires the released teleport acknowledgement,
not just an on-ground flag at the held position. Arrival markers are independent
of WitchCraft's normal collision-ready flag, which remains unset.
