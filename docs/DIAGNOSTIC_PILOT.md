# Bounded native follow and visibility trial

Opt in with `WITCHCRAFT_DIAGNOSTIC_PILOT=1`. This supersedes the earlier
diagnostics but does not enable normal Session control or free movement input.
Use the disposable outdoor save, ordinary open ground, no game actions, and keep
Witcher focused while standing still. After settling and MC third-person mode,
MC automatically holds W for 250 ms, releases it and brakes for one second.
The avatar is displayed during that trial. Afterward the overlay hides and the
native adapter requests removal of its Geralt hide reason. This trial is brief;
it is not the interactive replacement yet. Interrupted initial settling may
retry with a new acknowledgement sequence; movement itself remains one-shot.

Native calls run inside the existing guarded player GetWorldPosition observer,
never on the overlay worker. This is an observed game-thread callback, still not
a verified fixed movement-update point. This experiment must validate both
teleport timing and continued callbacks while Geralt is hidden.

The mapped CR4Player vtable+0x430 is RVA 0x1fd57c0. Its arguments are
`(player, hide, propagate, uint16 reason)`; it adds/removes reasons at player+0x30c
and updates rendering via the engine. The script wrapper uses reason 0x10. The
trial refuses an initially hidden player, uses 0x10 with propagate=false, checks
that the bit was added, and later removes only that bit and checks its removal.
Other hide reasons are preserved. Concurrent game/script ownership of that same
reason bit has not been established; avoid cutscenes or other game actions in
this short trial. Successful bit removal is not itself a visual restoration test.

The mapped vtable+0x460 is RVA 0x1e2fc80, which forwards position and rotation
to the entity helper at RVA 0x1fd3820. The Teleport script wrapper supplies a
16-byte position vector and the existing rotation at *(player+0xb0)+0x20.
The trial copies the rotation, supplies MC coordinates converted to Witcher
(x,-z,y,w=1), rejects nonfinite values and any single move over 0.75 m, and checks
the boolean return. Only one request may be pending. A later observer callback
must confirm player feet within 0.05 m of the request within 250 ms; otherwise
the trial stops and removes its hide reason. No new teleport is issued while
confirmation is pending. Targets already within 0.05 m need no teleport.
Method RVAs, player vtable and all three entry prefixes are checked before calls.

A separate copied command carries world/epoch/relocation and expires after
250 ms. It becomes active only after nontransparent avatar output and a successful
layered-window upload. Pause/focus/world changes, native failure, trial completion
or missing render output disable it. The native observer removes the owned hide
reason on its next callback. If callbacks stop entirely, restoration cannot run
until they resume; this is one reason the experiment is not general gameplay.
No unloaded player is dereferenced to force restoration.

Native follow uses raw MC tick feet. The MC walking diagnostic permits Geralt to
follow only within 0.75 m of those feet, with a total 2.5 m Geralt anchor bound.
The MC walk retains its own 2 m horizontal and 0.6 m height bounds. A native
failure is sticky for this process and stops the MC diagnostic as well.

Logs: `MC pilot diagnostic` records state (as DiagnosticWalk), displacement,
native hidden/failure state, successful native move count and W state.
`native pilot` records hide, initial follow operations, method failures and
hide-reason restoration confirmation. A pass requires native moves, MC walking
success and restoration evidence plus visual confirmation, not MC success alone.

Eleven offline suites pass, including follow-lag rejection, stationary retry,
command expiry, coordinate conversion, finite/step bounds, deferred movement
confirmation, outstanding-request rejection, timeout and cancellation. They
cannot invoke or verify REDengine behavior.

The first trial started but stopped at the immediate post-call position check.
The owned hide bit was added and its removal was confirmed. Two old log entries
said "follow applied", but those used a loose immediate-position tolerance and
are not proof of two completed moves. Later observer feet changed approximately
0.43 m, consistent with delayed engine movement. This suggests deferred position
application; it does not yet prove the revised callback timing works. The user
reported that nothing obvious happened during the 40-second check. The revised
build logs return value, immediate coordinates and later confirmed coordinates
separately. See pilot-first-attempt-2026-10-11.log for captured evidence.

The revised open-ground handoff trial passed on 2026-10-11. The engine accepted
the teleport requests and five follow operations were confirmed on later
callbacks. Minecraft moved 1.073 m, released input and stopped. Native failure
remained false. The owned hide reason was removed afterward, and the user
visually confirmed the Minecraft character took over, walked, and Geralt
returned. This also confirms that immediate reads can retain the previous feet
while the subsequent callback observes the requested position (with roughly
0.01 m of ground adjustment). Both game heartbeats remained live and the
collision queue was fully consumed when evidence was captured.

The trial began after settling, approximately 5–10 seconds into the user's
observation. The requested 40 seconds was an observation window, not a delayed
start. See pilot-handoff-2026-10-11.log. This proves one brief automatic handoff
on ordinary ground; manual input ownership, free walking, camera behavior over
longer movement, jumping, block interaction and scenery occlusion remain
unvalidated. Next: a bounded manual movement trial with a reliable stop/restore
control and suppression of Witcher's own movement input during ownership.
