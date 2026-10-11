# Standing Minecraft jumps with a ground camera anchor

Enable WITCHCRAFT_DIAGNOSTIC_JUMP=1 (staging installer: -Jump). After settling,
input capture and Geralt hiding, Space triggers a real Minecraft jump over the
exported Witcher collision. The rendered avatar uses Minecraft's interpolated
world position. Witcher's hidden player retains the settled ground height as
its camera anchor; the host no longer attempts to teleport Geralt vertically.
This mode leaves native gravity unchanged and uses the original verified native
hide/ground-follow backend. Normal Session control stays disabled.

A new Space press can start another jump after confirmed landing. Holding Space
never bounces automatically. Space is sent as SDL 44 for at most 100 ms and
released early on observed takeoff. WASD stays released in this standing trial.
The avatar remains active after landing until F8/Esc or an actual interruption;
there is no automatic ten-second return after landing.

Each jump requires takeoff within 750 ms, peak height at least 0.25 m, return to
within 0.15 m of its original ground height, and 500 ms of stable contact. Height
must stay between -0.25 and 1.6 m from the initial position; horizontal travel
is limited to 0.4 m. An unfinished jump expires after three seconds. The native
anchor must stay within 0.15 m of its settled ground height. Focus/world/link,
collision coverage, render, native hiding, acknowledgement and fresh report
checks remain. Native horizontal follow retains the original 0.75 m request/lag
limits and 0.05 m acknowledgement within 250 ms.

Limit: the Witcher camera stays at ground-anchor height during this standing
jump. Native vertical follow, jumping while walking, climbing/elevation changes
and block interaction are still pending.

Live test passed on 2026-10-11: the user confirmed both jumps worked. The trace
records two complete takeoff/landing cycles, each peaking at 1.249 m and returning
to the initial MC height of 2.267 m. The hidden native anchor stayed at 2.277 m.
Minecraft remained active after landing until the captured F8 request; native
visibility was then restored with no native failure. Evidence:
jump-standing-pass-2026-10-11.log.

All fifteen suites pass. Tests include MC airborne movement with the native
anchor held on the ground, the exact published anchor target, changed-anchor
rejection, repeated jumps, held-Space rejection, remaining active more than ten
seconds after landing, F8 return and unchanged walking targets/guards.

Game check: load the disposable outdoor save on flat open ground. Wait still
until Minecraft replaces Geralt. Press/release Space, wait for landing, then
press/release Space again. Keep Witcher focused and its menus closed, do not
press movement keys. Wait several seconds after the second landing and press
F8. Report jumps, landing, early return, alignment and Geralt restoration.

## Previous native vertical-follow investigation (2026-10-11)

The six attempts are retained in jump-first-attempt through
jump-sixth-attempt-2026-10-11.log. The first reached MC takeoff but exceeded the
walking lag guard. The second reached 1.249 m but native Teleport stayed on the
ground and timed out. Loaded-player disassembly showed its controller override
hardcodes ground snapping. The third used a verified non-snapping engine queue
and confirmed one upward native move; later movement timed out. The fourth
confirmed three moves with a small downward acknowledgement tolerance, then
missed by 0.643 m. The fifth verified temporary native gravity disable/restore
but timed out with closest error 0.069 m. The sixth retained that gravity
ownership with a 0.10 m downward tolerance, then timed out at 0.379 m.

The normal controller still returned the native player toward the ground; those
changes did not complete a jump. The current runtime removes experimental native
vertical queuing, gravity writes and relaxed acknowledgement. NativeJump.h and
NativeGravity.h remain as investigation helpers used by read-only probes and
offline tests, not by the runtime pilot. No jump/landing success is claimed for
those attempts. The current change deliberately uses Minecraft for standing
vertical motion and keeps the hidden Witcher camera anchor on the ground.

