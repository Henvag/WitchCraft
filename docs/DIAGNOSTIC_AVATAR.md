# Stationary Minecraft avatar display prototype

Opt in with process environment `WITCHCRAFT_DIAGNOSTIC_AVATAR=1`. It takes
precedence over the other diagnostics and keeps GameControl inactive. Geralt
remains visible and under Witcher control. This build tests rendering alignment
before the native movement/hiding integration.

Stand still on ordinary open ground in the disposable outdoor save. The existing
settle diagnostic waits for delivered terrain, teleports MC to the Witcher anchor,
and verifies stable ground contact. The host requests MC's behind-the-back view
using F5 until cameraMode=1. It then parks MC and displays the received avatar mesh
at MC's reported position through Witcher's observed camera. Only the avatar is
drawn in this diagnostic: other Minecraft scene meshes and HUD are excluded.

Rotate Witcher's camera while keeping Geralt stationary. The display hides on
pause/loading, loss of focus, stale game/MC data, invalid anchor/world, MC GUI,
death/flight, Geralt movement over 0.25 m, or MC displacement outside the settle
bounds. Menu/focus return may show the same stationary anchor again; movement to
a different location requires restarting the one-shot diagnostic.

The transparent desktop overlay has no Witcher depth buffer. Geralt may show
through or overlap parts of the Minecraft character, and Witcher scenery will
not occlude it correctly. Camera roll, exact FOV interpretation and screen
alignment still require visual validation. The raw native FOV is provisionally
treated as vertical, as in the existing renderer. Do not claim replacement or
playability from this diagnostic alone.

Logs record `MC avatar diagnostic`, visibility eligibility, MC position,
perspective and raw FOV. `MC avatar pixels` records nontransparent pixel count,
viewport size and layered-window upload success. Pixels/upload confirm output
was produced; user visual inspection must confirm it appears and aligns in game.

Ten offline test suites include settled-arrival/perspective gating, stationary
camera rotation, menu/focus hiding and return, stale link, anchor movement and
world/MC state changes. In-game validation is pending. Normal movement input is
not captured; the only synthetic input in this mode is MC's perspective toggle.

First launch reached MC cameraMode=1, but the settle diagnostic aborted on
a handoff/movement/stale-link/world-change guard before producing a visible
avatar. The specific guard cause is not isolated by that combined reason.
Display mode now retries failed initial settling when focus, game state and
delivered patch return, with a fresh stationary wait and a new teleport sequence.
The avatar gate preserves its own sequence across inactive Session frames to
avoid reusing an interrupted teleport. A regression test covers focus interruption
and successful retry. All ten suites pass; revised visual validation is pending.
Evidence: avatar-first-attempt-2026-10-11.log.

Revised visual test passed on 2026-10-11. The user reported that the character
stays aligned with Geralt during camera rotation, with no flickering. Host logs
confirm visibility eligibility at MC feet (1068.608,2.267,-1800.696), cameraMode
1 and raw FOV 67.00. At 2560x1440, sampled frames contained 364,005 to 433,077
nontransparent avatar pixels and layered-window uploads succeeded. Both games
remained alive with fresh heartbeats; 131,536 collision bytes were consumed
with none pending. These logs corroborate rendered output; alignment and absence
of flicker are user observations, not an automated pixel-comparison result.
Geralt remains visible and under Witcher control. This validates the stationary
avatar display and the tested camera rotations, not walking display, occlusion,
all FOV settings or a complete character replacement.
Evidence: avatar-display-pass-2026-10-11.log.
