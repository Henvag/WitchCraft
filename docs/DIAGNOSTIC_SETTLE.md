# Controlled Minecraft gravity diagnostic

The revised runtime attempt passed on 2026-10-11: MC reported feet
(1068.608,2.267,-1800.696), raw tick Y=2.267, flags=5 (in-world/on-ground), ack=1
and frame counter 3415. Geralt's stationary feet were Y=2.277; target spawn Y was
2.777. The MC log confirms the teleport. See settle-retry-2026-10-11.log.
This verifies released gravity/ground contact at that location only. MC later
crashed from native-memory exhaustion (mc-memory-failure-2026-10-11.log); reliable
long-running operation remains unverified. No Witcher control was enabled.
Reducing the instance heap from 4GB to 2GB allowed the successful gravity run,
but a later retry with 1GB also failed in native allocation. The instance heap is
now 2GB; original instance/options settings are backed up in the staging
mc-memory-backup-20261011 folder. The mod restores view/simulation distance to
eight on link, so the attempted settings reduction did not persist. No Windows
paging-file settings or unrelated apps were changed. Further concurrent testing
needs sufficient available system memory commitment. After headroom recovered to
about 8.5GB with Witcher still running, MC relaunched with the 2GB heap and stayed
connected through checks about 20, 40 and 60 seconds after launch, with zero
pending collision data. This is a short recovery check, not long-run stability.

The first runtime attempt stopped before arrival: the host read ack 1 while MC
still reported its old feet (0.500,8.000,9.502); the later MC log confirms the
requested teleport to (1068.608,2.777,-1800.696). No ground-contact result can be
inferred from that attempt. Evidence: settle-first-attempt-2026-10-11.log.
The revised diagnostic waits for both the requested ack and coordinates near
the target before applying escape/contact checks, and avoids reusing the current
ack sequence. Its regression test covers a new ack paired with old coordinates.

Opt in for one Witcher process with WITCHCRAFT_DIAGNOSTIC_SETTLE=1. Default is off.
The staging installer accepts -Settle to set the environment on the newly launched
Witcher process only. This does not modify the Minecraft JAR or enable normal play.

The native adapter publishes kGameDiagnosticPatch only after accepting a complete
sampled patch near Geralt. This is not kGameCollisionReady. The overlay worker's
separate DiagnosticSettle waits for two seconds of stationary Geralt, a foreground
Witcher window, live MC without a GUI/death/flight, the host queue drained and the
MC collision-ring tail caught up. It then issues one MC-only teleport to Geralt's
feet plus half a metre and publishes kSkyInGame during the diagnostic. GameControl
is forced inactive, PollInput remains uncaptured, and rendering stays hidden.
No Witcher movement or visibility method is called.

Empty kColRegion packets mark sampled cubes as arrived for SkyCraft's spawn hold;
they have zero voxels. Triangles follow for each cube. The MC client releases its
hold after its six-second readiness timeout when no voxel solids exist. Its
teleportAck equals the requested sequence only after hold release. The diagnostic
accepts a pass only with that acknowledgement, an on-ground flag lasting half a
second, and a height within 0.6m of Geralt's original feet. It fails after 15s,
on a vertical escape (-1.5m/+2.5m), horizontal escape (0.5m), Geralt movement
(0.25m), focus loss, stale/disconnected MC, GUI, death, flight or world/relocation
change. It then publishes menu state to park MC. One attempt per process.

Logs include state 1 (running), 2 (passed) or 3 (failed), MC feet/raw tick Y,
target spawn Y, flags, acknowledgement and frame counter. Seven tests cover
normal-control exclusion, region marker indexing, delivery waits, old acks,
settling, falls, Geralt movement, timeout and focus handoff.

A successful run would validate released MC gravity/ground contact at one sampled
location. It would not validate walking, walls, jumps, full scenery coverage,
scene detection, projection, native Geralt movement or save association. Default
gameplay remains disabled.
