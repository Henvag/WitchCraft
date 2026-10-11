# First manual forward trial

Opt in with WITCHCRAFT_DIAGNOSTIC_MANUAL=1. This takes priority over the older
diagnostics and enables the bounded native pilot backend. Normal Session control
remains disabled. This is one user-triggered forward pulse, not free movement.
Game-input interception and F8 restoration passed the bounded repeat below.

In the disposable outdoor save, stand still on ordinary open ground until the
Minecraft avatar replaces Geralt. Hold W for roughly half a second and release
it. Only the first 250 ms may reach Minecraft. It should take a short step and
stop. Press F8 or Esc afterward; Geralt should return. Keep Witcher focused and
the camera still during this step. No other gameplay actions are part of this trial.

The controller first runs the established gravity/arrival diagnostic. Ready
shows the avatar and takes native visibility ownership without automatic walking.
W may start only when the game-input adapter is installed and hiding is confirmed.
Capture refuses to start midway through an existing physical key press. Ready
expires after 30 seconds. After W releases or reaches 250 ms, a one-second braking
check verifies forward movement, ground contact and limited drift. Minecraft then
remains visible for the return-key check, with automatic return after 15 seconds.

The revised input adapter replaces only Witcher's executable import-table entries
for GetRawInputData and GetAsyncKeyState. It validates PE64 import bounds and
requires both original slots to match the resolved user32 exports. Pointer swaps
use a temporary writable protection and restore the original page protection.
If the second swap fails, it attempts to restore the first; either failure keeps
manual ownership disabled. Installed callbacks delegate to the original APIs
when capture is inactive. The DLL is process-lifetime, and replacing the build
requires restarting Witcher. Other modules' import tables are untouched.

For captured keyboard records, the adapter records the key, then supplies an
unmapped virtual key (255), zero scan code and a release message to the game.
Buffer lengths and RAWINPUTHEADER size/type are checked first. Size-only queries,
header-only queries, API errors and mouse/HID records preserve original behavior.
Microsoft's RAWKEYBOARD example discards VKey >=255:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawkeyboard
Mouse movement therefore continues to drive Witcher's camera. Mouse buttons and
other actions must not be used during this trial; raw mouse packets are unchanged.

Captured game key-state polling returns zero. This DLL's own user32 import stays
original, allowing physical W/Esc/F8 state observations without relying on global
keyboard-hook delivery. Raw key events preserve stop-key edges; physical polling
also supplies key transitions. State is copied under a short mutex. Only the walk
controller sends W to Minecraft. Other ordinary keys are suppressed in these two
game input paths. Alt/Windows keys and Alt-Tab pass through. Matched repeats and
release ownership persist after return to avoid a held W starting Geralt again.
Events observed when another application has focus pass through. The input lease
expires after one second without refresh; native follow still expires after 250 ms.

Focus/menu/world changes, stale reports, native failure, lost hiding after movement
starts, capture failure or unavailable avatar upload ends the trial. It releases
MC input, parks MC through menu state and disables the pilot command. The native
observer removes its owned hide reason on its next callback. Existing 2 m
horizontal, 0.6 m height, 0.75 m follow-lag and 0.75 m native-step limits remain.
After ready begins, terminal states require a Witcher restart to retry. Interrupted
initial settling can still retry with a fresh acknowledgement.

Logs include manual/inputReady, W seen/blocked/passed counts, captured stop presses,
installation error, raw records seen/filtered and filtered key-state queries.
State 5 ready, 1 walking, 2 braking, 7 stopped, 3 passed, 6 cancelled, 4 failed.
A manual pass requires a user W transition, confirmed native moves, successful
braking and restoration. The user must confirm visible movement and Geralt returning.
DirectInput or other game input paths are not intercepted in this build; whether
the two observed executable imports cover other movement/actions remains unvalidated.

Thirteen offline suites pass. They cover pulse/ground/lag bounds, no-input waiting,
capture/hide prerequisites, early release, manual stop, held-key repeat draining,
idle/return timeouts, focus/system keys, render cancellation, bounded PE import
inspection, truncated raw records and preservation of non-keyboard packets.
They do not invoke REDengine or prove game input suppression. Jumping, side/back
movement, block actions, extended play and scenery occlusion remain pending.

The first manual attempt reached ready but MC stayed stationary when W was pressed.
Geralt moved and exceeded the follow-lag bound, ending the trial and restoring
visibility. Its hooks shared the render worker. The second attempt moved the hooks
to a dedicated thread, but recorded zero W events (wSeen=0, wBlocked=0, wPassed=0)
while Geralt again moved and the trial restored visibility. That disproves the
assumption that a successful global-hook installation supplies this game's W
input. It does not establish why no callback was delivered. Offline executable
imports confirm raw input and key-state APIs exist, motivating this adapter.
Evidence: manual-first-attempt-2026-10-11.log and manual-thread-attempt-2026-10-11.log.
The revised raw-input trial achieved its first manual walk on 2026-10-11.
The user confirmed walking worked. Logs show a manual W transition, 1.073 m of
Minecraft displacement, five confirmed native follow operations, successful
braking and no native failure. Ten raw-keyboard records and 3990 key-state polls
were filtered during ownership. It returned automatically after 15 seconds at
rest and confirmed hide-bit removal. No captured stop-key press was recorded,
so F8 restoration still needs its own check. The user requested a full restart
and repeat. See manual-walk-first-success-2026-10-11.log. This validates one
bounded manual forward walk; longer movement and other controls remain pending.

The repeat on 2026-10-11 passed both manual walking and F8 restoration. The user
confirmed both worked. Logs recorded a manual W press, 1.073 m displacement,
five confirmed native follow operations, successful braking, and then a captured
stop-key press (stopSeen=1) with reason "manual stop requested; returning Geralt".
The owned hide bit was removed and hidden became false, with nativeFailed=0.
During the walk W events were blocked (wPassed=0), and five raw-keyboard records
and 855 game key-state polls were filtered before return. Both game heartbeats
were fresh and the collision queue was consumed when evidence was captured.
See manual-walk-f8-pass-2026-10-11.log. This establishes the first bounded,
player-controlled handoff with an explicit return key, not general gameplay.
