# Minecraft wall-contact diagnostic

Opt in with `WITCHCRAFT_DIAGNOSTIC_WALL=1` in Witcher's process environment.
This takes precedence over walking/settle diagnostics and forces GameControl
inactive. Geralt stays visible and under Witcher control; normal input capture
and rendering remain disabled. Only the hidden MC player receives automatic W.

After installation, use the disposable outdoor save. Find a solid, nearly
vertical building wall and stand about one metre from it on ordinary ground.
Keep Witcher focused, stand still and leave menus closed for 40 seconds. The
diagnostic waits for exported, consumed triangles covering nine points across
MC's body: heights 0.2, 0.8 and 1.4 m and lateral offsets -0.35, 0, +0.35 m.
The wall plane must be 0.6 to 1.8 m away. Gaps, missing triangles, slanted walls
and walls outside that range leave the test waiting without pressing W.

The existing gravity test runs first. On settled arrival, the host freezes the
selected wall plane and MC yaw, then presses W toward it. A contact is confirmed
only within 0.15 to 0.45 m of the plane, after at least 0.15 m forward approach
and 500 ms with at most 0.08 m drift while W is held. It releases W and observes
another 500 ms before passing. This establishes obstruction at the sampled wall
plane; it cannot prove the identity of the blocking collider in MC.

The attempt fails and releases W on focus/menu/world/ack changes, lost ground
contact, stale reports, Geralt moving over 0.25 m, MC moving sideways over 0.3 m,
horizontal displacement over 2.5 m, height change over 0.6 m, crossing within
0.15 m of the wall plane, or no contact within two seconds. One attempt per
Witcher process. Terminal state parks MC in menu state. Desired W state is sent
each host update, including release, to recover from input queue backpressure.

The native adapter publishes a copied wall-only mesh after enqueueing its entire
collision batch. The host checks world/epoch/relocation and current sampled-patch
status and waits for transport consumption before starting. No REDengine pointer
crosses threads; GameFrame and protocol ABI remain unchanged.

Host log states: 0 waiting, 1 settling, 2 approaching, 3 braking, 4 passed,
5 failed. Entries show separation from the wall plane, raw MC tick feet, flags,
ack, frame counter, W state and sampled wall triangle count.

Nine offline test suites cover mesh ownership/identity, body coverage, geometry
gaps, successful obstruction/release, crossing, timeout, loss of focus/ground,
stale reports, world changes and Geralt movement. In-game validation is pending.
This does not enable general gameplay, jumps, block placement, avatar projection
or native Geralt movement. Minecraft still uses the 2 GB heap from the previous
memory investigation; check a fresh heartbeat before starting an attempt.

First in-game attempt (2026-10-11) remained waiting with W=0. All 259,296
collision bytes were consumed. At Witcher feet (1002.494,9.852,-1799.606)
the exported planes were 1.163 and 1.362 m away, but the best target covered
only six of nine body points: mask 0x1f8, missing all three ankle-height
points. Range was correct; incomplete lower-body geometry prevented activation.
No wall collision pass is claimed. Evidence: wall-contact-wait-2026-10-11.log.

Second location remained waiting, W=0, with 282 wall triangles published and
499,696 collision bytes consumed. At feet (999.693,10.596,-1804.604), the
nearest vertical plane was 0.715 m away. Best body coverage was five of nine
points, mask 0x0d9. The existing geometry therefore cannot start this strict
wall-contact check at either tested location. This is a coverage/selection
limitation, not a successful collision test or a transport failure. Further
manual position retries are deferred. Evidence: wall-contact-second-wait-2026-10-11.log.

Sampling refinement: coarse half-metre wall cells remain conservative. After
the coarse pass, up to two nearby wall orientations receive body-centred parallel
rays on a 0.2 m lateral / 0.125 m vertical grid covering +/-0.8 m sideways
and 0 to 2 m above the frozen patch origin. Each orientation uses 153 corners
and 128 cell centres (281 rays), with a 3 m ray length. All five samples per
cell must hit, face the ray origin, agree in normal and lie within 4 cm of the
centre plane. Geometry is never extrapolated across missing samples.
At most 562 additional rays are sampled per patch (1,651 including ground and
coarse walls). The existing eight-query / one-millisecond observation budget
is retained; patch completion can take longer. Coarse and refined triangles
share the same collision tiling and diagnostic wall snapshot.
The regression test demonstrates coarse rejection near a thin projecting base,
refined body coverage above it, and preservation of a missing fine-cell hole.
All nine offline test suites pass. This is a proposed improvement for the two
observed coverage failures; revised in-game validation remains pending.

Refined in-game test passed on 2026-10-11. State 2 began at wall separation
0.956 m, raw feet (1000.460,10.681,-1810.062), flags 5, ack 1, frame 2329,
W=1 and 394 wall triangles. State 3 released W at separation 0.300 m, raw
feet (1000.634,10.716,-1810.695), flags 5, ack 1, frame 2352. State 4 passed
at the same position and separation, flags 5, ack 1, frame 2372, W=0.
MC approached about 0.656 m and stopped at its body half-width from the
selected plane, remained blocked during held input, and stayed still after
release. Geralt remained at the original anchor under Witcher control.
Later terminal logs have menu/hold flags and ack values; they are not new
contact tests. Both games were alive with fresh heartbeats at inspection;
347,296 collision bytes were consumed with none pending. Refined patches
used 1,370 rays at this location (one nearby orientation). This validates
one bounded wall-contact test, not arbitrary scenery or general gameplay.
Evidence: wall-contact-pass-2026-10-11.log.
