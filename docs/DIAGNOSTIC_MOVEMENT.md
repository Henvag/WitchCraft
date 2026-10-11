# Bounded WASD movement trial

Enable WITCHCRAFT_DIAGNOSTIC_MOVEMENT=1. This supersedes the single-W trial,
retains the validated game-input interception/native follow backend, and leaves
normal Session control disabled. All four directions passed the first game test
described below. Diagonals, camera rotation and explicit F8 return also passed
their subsequent test.

After stationary settling and third-person avatar readiness, the controller
waits for physical input and native hiding. W/A/S/D are sent as SDL codes
26/4/22/7, respectively. Desired states are refreshed each frame, including
releases. Multiple keys can be held together for Minecraft's diagonal movement.
Releasing keys leaves the avatar active so the user can change direction without
restarting. The current native camera yaw/pitch continue to reach Minecraft.
F8 or Esc ends ownership and restores Geralt on the next native callback.

This is a bounded outdoor trial. It releases all four desired movement keys
when distance reaches 2.3 m from the starting point, accumulated horizontal
travel reaches 7 m, or 20 seconds have elapsed since the first movement key.
These are soft stopping limits. Hard checks stop the trial if MC exceeds a
3 m radius or 0.6 m height difference, native travel exceeds 4 m, native follow
lag exceeds 0.75 m, or ground contact is lost for more than 250 ms. The native
backend retains its existing 0.75 m per-call limit and deferred acknowledgement.
Ready with no movement expires after 30 seconds. On a soft limit, the controller
checks 500 ms of position stability, stops if braking exceeds three seconds,
and waits up to ten seconds for F8 before automatic return. Held keys cannot
restart movement once a soft limit is reached.

The manual return is marked passed only after at least 0.25 m of travel and
500 ms of rest; an earlier return is cancellation, and still restores Geralt.
Focus, loading/menu, world/epoch/relocation, collision coverage, acknowledgement,
fresh-report, capture, native-hiding and avatar-upload guards remain active.
Any rendering failure releases input and disables native ownership. Terminal
states are one-shot; interrupted initial settling can retry with a new sequence.
MC position reports must remain fresh within one second. Finite position and
camera angles are required.

Use the disposable outdoor save on ordinary open ground. Once the avatar appears,
try W, S, A, D in short roughly 0.3-second presses, releasing and pausing between
each. Opposite directions keep the trial near its starting point. After the last
step has stopped, press F8. Keep the camera still initially. A later repeat can
check camera rotation and diagonals after all four directions are confirmed.
No jumping, combat, mouse buttons, inventory or block interaction belongs in this
first trial. Raw mouse packets are unchanged by the validated input adapter.

Logs use `MC WASD diagnostic` with desired `keys` bitmask W=1,A=2,S=4,D=8,
accumulated `path`, displacement, native movement/hiding/failure and game-input
counters. States: 0 settling, 1 ready, 2 controlling, 3 braking, 4 stopped,
5 passed, 6 failed, 7 cancelled. A game pass requires requested directions to
produce the expected movement, native follow, release and visible F8 restoration.
An offline or log pass alone does not establish correct direction or camera feel.

Fourteen offline suites pass. The new movement suite covers sustained input
beyond the old 250 ms pulse, all four masks, diagonals, repeated movement after
release, F8 at rest, automatic braking/return, held-key limits, radius/path/time,
native lag, world/focus, ground, frozen/nonfinite reports and rendering loss.
The key-ownership suite also covers all four held keys and their release draining.
These tests do not exercise REDengine. This build remains a small-area prototype;
extended terrain coverage, jumping, blocks and scene occlusion remain pending.

On 2026-10-11 the user confirmed all four directions worked and reported F8 worked.
The captured run contains W/S/A/D masks (1/4/2/8), repeated presses and releases,
39 confirmed native follow operations, 7.381 m accumulated horizontal travel and
no native failure. It hit the 7 m soft path limit, braked, then remained still at
0.270 m displacement from the start before automatic return. Hide-bit removal
was confirmed. During ownership W passed-count remained zero, and 76 raw-keyboard
records plus 3615 key-state queries were filtered before return.

This run recorded stopSeen=0 and an automatic-return reason. The user's reported
F8 action therefore cannot be attributed as the cause of restoration in this
capture; it may have occurred after the timeout. F8 was explicitly captured and
visually validated in the earlier manual-walk-f8-pass-2026-10-11.log trial. The
WASD milestone validates the four directions, native follow, soft path stop and
automatic restoration. It does not yet validate diagonals, camera rotation or
F8 during sustained WASD ownership. See wasd-directions-pass-2026-10-11.log.

The next test on 2026-10-11 passed in the requested order. The user confirmed
W+D diagonal movement, S+A returning toward the start, camera rotation followed
by W in the new camera direction, stable avatar alignment without flickering,
and F8 restoration. Logs confirm masks 9, 6 and 1 in that order, 14 confirmed
native follow operations, 2.837 m of total travel, and final 0.642 m displacement.
The captured stop press (stopSeen=1) ended ownership with "manual return requested"
and the hide bit was removed. Native failure remained zero. Both heartbeats were
fresh and the collision queue fully consumed at capture. Camera-relative direction
and visual alignment are user observations; the diagnostic does not numerically
compare expected camera heading against the measured trajectory. See
wasd-diagonal-camera-pass-2026-10-11.log. This completes the bounded WASD/camera
trial. Jumping, landing, extended walking, block interaction and occlusion remain.
