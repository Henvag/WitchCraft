# Bounded Minecraft walking diagnostic

Opt in with the process environment `WITCHCRAFT_DIAGNOSTIC_WALK=1`. It takes
precedence over the settle-only diagnostic. Default gameplay remains disabled;
Geralt remains under Witcher control and normal input capture stays off.

This is one attempt per Witcher process. Keep Witcher focused, stand still on
ordinary open ground in the disposable outdoor save, and leave menus closed.
The existing gravity test first waits for a delivered/consumed patch and a stable
teleport acknowledgement, then confirms ground contact. The raw MC tick position
must also agree with the settled location before input is sent.

The host sends Minecraft's W key for at most 250 ms at the starting camera yaw,
releases it, and observes another second. It requires at least 0.25 m of movement,
at least 0.2 m forward, at most 0.4 m sideways, and at most 0.35 m movement after
release. It stops on focus/menu/world changes, stale reports, Geralt movement
above 0.25 m, a 2 m horizontal escape, a 0.6 m height change, or ground contact
lost for over 250 ms. Terminal states publish menu state; desired W state is
refreshed each host update so a full input queue cannot permanently drop release.
Host failure also relies on SkyCraft's existing stale-host input release.

`host.log` records `MC walk diagnostic`, states 0 settling, 1 walking, 2 braking,
3 passed, 4 failed, raw tick position, displacement, flags, ack, frame and W state.
Eight offline test suites pass, including gravity-before-walk, release on abort,
frozen position reports, area/height guards, blocked movement, braking and yaw.
The revised in-game test passed on 2026-10-11. This validates only this short forward
walk at one sampled location; wall walking, jumps, block placement, avatar display
and native Geralt movement still need implementation or validation.

Minecraft currently uses a 2 GB heap. The prior gravity run passed, followed by
native allocation failures under high system memory commit. A subsequent restart
remained connected for one minute after commit headroom recovered. Check both game
processes and a fresh MC heartbeat before testing; this is not long-run stability
evidence. Do not launch MC until the old instance has exited after Witcher closes.

First in-game attempt (2026-10-11): gravity passed; a 500 ms W pulse moved MC
1.900 m with ground flag 5, and release changed to braking. Coasting reached
2.081 m, triggering the 2 m bound before stop validation. No full walking pass
was recorded. The revised pulse is 250 ms; the same bounds remain. Evidence:
walk-first-attempt-2026-10-11.log.

Revised in-game attempt: the 250 ms pulse started at raw feet
(1068.608, 2.267, -1800.696), flags 5, ack 1, frame 2577. W was released
at frame 2583 with displacement 0.832 m. The stop check passed at frame 2612
with displacement 1.073 m and raw feet (1068.111, 2.266, -1801.647), flags 5,
ack 1 and W=0. Coasting after release was about 0.241 m. Terminal menu state
then parked MC; later terminal log entries have flags 1 and ack 0 and are not
new ground samples. Geralt stayed at his original anchor. Both game processes
were alive with fresh heartbeats at inspection, and all 132,176 collision bytes
were consumed with none pending. Evidence: walk-pass-2026-10-11.log.
