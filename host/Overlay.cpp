#include "Overlay.h"

#include "Input.h"
#include "Link.h"
#include "World.h"
#include "GameBridge.h"
#include "DiagnosticSettle.h"
#include "DiagnosticWalk.h"
#include "DiagnosticWall.h"
#include "DiagnosticAvatar.h"
#include "DiagnosticMovement.h"
#include "PlaySession.h"
#include "NativeCamera.h"
#include "NativePilot.h"
#include "PlayToggle.h"
#include "RenderTiming.h"
#include "McSnapshotCache.h"
#include "HudBlend.h"
#include "HudViewport.h"
#include "HudScale.h"
#include "ScriptBridgeNative.h"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace witchcraft
{
	namespace
	{
		HWND          g_window = nullptr;
		HWND          g_game = nullptr;
		HDC           g_dc = nullptr;
		HBITMAP       g_bitmap = nullptr;
		void*         g_bits = nullptr;
		int           g_width = 0;
		int           g_height = 0;
		bool          g_logged = false;
		std::vector<std::uint8_t> g_hud;
		int           g_hudW = 0;
		int           g_hudH = 0;
		bool          g_hudBottom = false;
		std::vector<std::uint8_t> g_scaledHud;
		bool g_hudChanged=true;
		int g_scaledW=0,g_scaledH=0;

        LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            return ::DefWindowProcW(hwnd, msg, wp, lp);
        }
		BOOL CALLBACK FindGame(HWND a_hwnd, LPARAM a_pid)
		{
			if(a_hwnd==g_window) return TRUE;
			DWORD pid = 0;
			::GetWindowThreadProcessId(a_hwnd, &pid);
			if (pid != static_cast<DWORD>(a_pid) || !::IsWindowVisible(a_hwnd)) {
				return TRUE;
			}
			RECT rect{};
			::GetClientRect(a_hwnd, &rect);
			const int area = (rect.right - rect.left) * (rect.bottom - rect.top);
			RECT best{};
			if (g_game) {
				::GetClientRect(g_game, &best);
			}
			const int bestArea = g_game ? (best.right - best.left) * (best.bottom - best.top) : 0;
			if (area > bestArea && area > 100 * 100) {
				g_game = a_hwnd;
			}
			return TRUE;
		}

		HWND GameWindow()
		{
			if(g_game&&::IsWindow(g_game)) {
				DWORD pid=0;::GetWindowThreadProcessId(g_game,&pid);
				if(pid==::GetCurrentProcessId()) return g_game;
			}
			g_game = nullptr;
			::EnumWindows(FindGame, static_cast<LPARAM>(::GetCurrentProcessId()));
			return g_game;
		}

		bool EnsureWindow(HWND a_game)
		{
			if (g_window && ::IsWindow(g_window)) {
				return true;
			}
			WNDCLASSW cls{};
			cls.lpfnWndProc = OverlayProc;
			cls.hInstance = ::GetModuleHandleW(nullptr);
			cls.lpszClassName = L"WitchCraftOverlay";
			::RegisterClassW(&cls);
			g_window = ::CreateWindowExW(
				WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
				cls.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, a_game, nullptr, cls.hInstance, nullptr);
			return g_window != nullptr;
		}

		bool EnsureBitmap(int a_w, int a_h)
		{
			if (g_bitmap && g_width == a_w && g_height == a_h) {
				return true;
			}
			if (g_bitmap) {
				::DeleteObject(g_bitmap);
				g_bitmap = nullptr;
			}
			if (!g_dc) {
				g_dc = ::CreateCompatibleDC(nullptr);
			}
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = a_w;
			info.bmiHeader.biHeight = -a_h;
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			g_bitmap = ::CreateDIBSection(g_dc, &info, DIB_RGB_COLORS, &g_bits, nullptr, 0);
			if (!g_bitmap) {
				return false;
			}
			::SelectObject(g_dc, g_bitmap);
			g_width = a_w;
			g_height = a_h;
			return true;
		}

		// Hotbar, hearts, and the first-person arm sit on top of the world. Both are premultiplied.
		void LogLine(const char* a_line)
		{
			wchar_t dir[MAX_PATH]{};
			if (!::GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH)) {
				return;
			}
			::lstrcatW(dir, L"\\WitchCraft\\host.log");
			HANDLE file = ::CreateFileW(dir, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				return;
			}
			DWORD wrote = 0;
			::WriteFile(file, a_line, static_cast<DWORD>(std::strlen(a_line)), &wrote, nullptr);
			::WriteFile(file, "\r\n", 2, &wrote, nullptr);
			::CloseHandle(file);
		}

		void CompositeHud()
		{
			if (g_hud.empty() || g_hudW <= 0 || g_hudH <= 0) {
				return;
			}
			auto* dst = static_cast<std::uint8_t*>(g_bits);
			static std::vector<int> spanLo,spanHi;
			if(g_hudChanged) {
				if(!HudRowSpans(g_hud,g_hudW,g_hudH,spanLo,spanHi)) return;
				g_hudChanged=false;
			}
			BlendScaledHud(dst,g_width,g_height,g_hud,g_hudW,g_hudH,g_hudBottom,spanLo,spanHi);
		}
	}

    void PresentOverlay()
    {
        LARGE_INTEGER overlayBegin{};QueryPerformanceCounter(&overlayBegin);
        MSG msg{};
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        auto& link = Link::Get();
        PumpGameCollision();
        GameFrame frame{};
        const bool haveGame = ReadGameFrame(frame);
        if (haveGame && !GameCollisionDrained(frame.collisionEpoch))
            frame.flags &= ~kGameCollisionReady;
        skycraft::proto::McState candidate{},mc{};
        const bool received=link.ReadMcState(candidate);
        static McSnapshotCache mcCache;
        const bool haveMc=mcCache.Update(link.McAlive(),link.McPid(),received,
            candidate,::GetTickCount64(),mc);
        static unsigned missedReads=0,reusedReads=0;
        static ULONGLONG snapshotLogAt=::GetTickCount64();
        if(!received) {++missedReads;if(haveMc) ++reusedReads;}
        if(::GetTickCount64()-snapshotLogAt>=5000) {
            if(missedReads) {
                char line[128]{};std::snprintf(line,sizeof(line),
                    "MC snapshots: missed=%u reusedCoherent=%u maxAgeMs=100",missedReads,reusedReads);LogLine(line);
            }
            missedReads=reusedReads=0;snapshotLogAt=::GetTickCount64();
        }
        static std::uint64_t sampledMcFrame=0;
        static unsigned freshMcSamples=0,newHudFrames=0;
        if(haveMc&&mc.frameCounter!=sampledMcFrame) {sampledMcFrame=mc.frameCounter;++freshMcSamples;}
        HWND game = GameWindow();
        RECT client{};
        if (game) ::GetClientRect(game, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        static const bool fullHud=[] {wchar_t value[2]{};return ::GetEnvironmentVariableW(L"WITCHCRAFT_HUD_FULL_RES",value,2)&&value[0]==L'1';}();
        const auto transfer=TransferViewport(width,height,fullHud);
        static Session session;
        auto sky = session.Update(haveGame ? &frame : nullptr, haveMc ? &mc : nullptr,
            haveMc, static_cast<std::uint32_t>(transfer.width), static_cast<std::uint32_t>(transfer.height));
        DWORD foregroundPid = 0;
        ::GetWindowThreadProcessId(::GetForegroundWindow(), &foregroundPid);
        const bool focused = foregroundPid == ::GetCurrentProcessId();
        auto control = session.Control();
        if (!focused || !game || ::IsIconic(game) || width < 100 || height < 100) {
            control.active = 0;
            sky.flags = skycraft::proto::kSkyMenuOpen;
        }
        static const bool settleEnabled=[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_SETTLE",value,2)&&value[0]==L'1';
        }();
        static const bool walkEnabled=[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_WALK",value,2)&&value[0]==L'1';
        }();
        static const bool wallEnabled=[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_WALL",value,2)&&value[0]==L'1';
        }();
        static const bool avatarEnabled=[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_AVATAR",value,2)&&value[0]==L'1';
        }();
        static const bool playEnabled=[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_PLAY",value,2)&&value[0]==L'1';
        }();
        static const bool jumpEnabled=[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_JUMP",value,2)&&value[0]==L'1';
        }();
        static const bool movementEnabled=playEnabled||jumpEnabled||[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_MOVEMENT",value,2)&&value[0]==L'1';
        }();
        static const bool manualEnabled=movementEnabled||[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_MANUAL",value,2)&&value[0]==L'1';
        }();
        static const bool pilotEnabled=manualEnabled||[] {
            wchar_t value[2]{};
            return ::GetEnvironmentVariableW(L"WITCHCRAFT_DIAGNOSTIC_PILOT",value,2)&&value[0]==L'1';
        }();
        if(settleEnabled||walkEnabled||wallEnabled||avatarEnabled||pilotEnabled) control.active=0;
        const bool playing = control.active != 0;
        PublishGameControl(control);
        static DiagnosticSettle settle;
        static ULONGLONG lastSettleLog=0;
        if(settleEnabled&&!walkEnabled&&!wallEnabled&&!avatarEnabled&&!pilotEnabled) {
            const auto before=settle.Status();
            settle.Update(haveGame?&frame:nullptr,haveMc?&mc:nullptr,haveMc,focused,
                haveGame&&GameCollisionDrained(frame.collisionEpoch)&&link.CollisionConsumed(),
                ::GetTickCount64(),sky);
            const auto now=::GetTickCount64();
            if(before!=settle.Status()||(settle.Status()==DiagnosticSettle::State::running&&now-lastSettleLog>=1000)) {
                char line[384]{};
                std::snprintf(line,sizeof(line),"MC settle diagnostic: state=%u reason=%s feet=(%.3f,%.3f,%.3f) targetY=%.3f rawY=%.3f flags=%u ack=%u frame=%llu",
                    static_cast<unsigned>(settle.Status()),settle.Reason(),mc.x,mc.y,mc.z,sky.posY,mc.curY,
                    mc.flags,mc.teleportAck,static_cast<unsigned long long>(mc.frameCounter));
                LogLine(line);
                lastSettleLog=now;
            }
        }
        float yaw = sky.yaw, pitch = sky.pitch;
        // The real Witcher camera owns look. Minecraft's independent mouse
        // sensitivity must not rotate blocks away from the Witcher background.
        PollInput(game, width, height, mc, haveMc, playing, yaw, pitch);
        static DiagnosticWalk walk;
        static ULONGLONG lastWalkLog=0;
        if(walkEnabled&&!wallEnabled&&!avatarEnabled&&!pilotEnabled) {
            const auto before=walk.Status();
            const auto now=::GetTickCount64();
            walk.Update(haveGame?&frame:nullptr,haveMc?&mc:nullptr,haveMc,
                focused&&game&&!::IsIconic(game)&&width>=100&&height>=100,
                haveGame&&GameCollisionDrained(frame.collisionEpoch)&&link.CollisionConsumed(),now,sky);
            // Refresh desired state so a full input ring cannot strand a held key.
            // Diagnostic mode leaves normal capture off; only MC receives this W.
            link.PushInput(skycraft::proto::kInKey,26,walk.ForwardDown()?1:0);
            if(before!=walk.Status()||now-lastWalkLog>=1000) {
                char line[384]{};
                std::snprintf(line,sizeof(line),"MC walk diagnostic: state=%u reason=%s distance=%.3f rawFeet=(%.3f,%.3f,%.3f) flags=%u ack=%u frame=%llu W=%u",
                    static_cast<unsigned>(walk.Status()),walk.Reason(),walk.Distance(),mc.curX,mc.curY,mc.curZ,
                    mc.flags,mc.teleportAck,static_cast<unsigned long long>(mc.frameCounter),walk.ForwardDown()?1u:0u);
                LogLine(line); lastWalkLog=now;
            }
        }
        static DiagnosticWall wall;
        static ULONGLONG lastWallLog=0;
        if(wallEnabled&&!avatarEnabled&&!pilotEnabled) {
            DiagnosticWalls mesh;
            const bool haveWalls=haveGame&&ReadDiagnosticWalls(frame,mesh);
            const auto before=wall.Status();
            const auto now=::GetTickCount64();
            wall.Update(haveGame?&frame:nullptr,haveMc?&mc:nullptr,haveWalls?&mesh:nullptr,haveMc,
                focused&&game&&!::IsIconic(game)&&width>=100&&height>=100,
                haveGame&&GameCollisionDrained(frame.collisionEpoch)&&link.CollisionConsumed(),now,sky);
            link.PushInput(skycraft::proto::kInKey,26,wall.ForwardDown()?1:0);
            if(before!=wall.Status()||now-lastWallLog>=1000) {
                char line[384]{};
                std::snprintf(line,sizeof(line),"MC wall diagnostic: state=%u reason=%s separation=%.3f rawFeet=(%.3f,%.3f,%.3f) flags=%u ack=%u frame=%llu W=%u wallTris=%u",
                    static_cast<unsigned>(wall.Status()),wall.Reason(),wall.Separation(),mc.curX,mc.curY,mc.curZ,
                    mc.flags,mc.teleportAck,static_cast<unsigned long long>(mc.frameCounter),wall.ForwardDown()?1u:0u,
                    static_cast<unsigned>(mesh.triangles.size()));
                LogLine(line); lastWallLog=now;
            }
        }
        static DiagnosticAvatar avatar;
        static ULONGLONG lastAvatarLog=0,nextPerspective=0;
        if(avatarEnabled&&!pilotEnabled) {
            const auto now=::GetTickCount64();
            avatar.Update(haveGame?&frame:nullptr,haveMc?&mc:nullptr,haveMc,
                focused&&game&&!::IsIconic(game)&&width>=100&&height>=100,
                haveGame&&GameCollisionDrained(frame.collisionEpoch)&&link.CollisionConsumed(),now,sky);
            if(haveMc&&focused&&(mc.flags&skycraft::proto::kMcInWorld)&&
                !(mc.flags&skycraft::proto::kMcScreenOpen)&&mc.cameraMode!=1&&now>=nextPerspective) {
                link.PushInput(skycraft::proto::kInKey,62,1);
                link.PushInput(skycraft::proto::kInKey,62,0); nextPerspective=now+750;
            }
            if(now-lastAvatarLog>=3000) {
                char line[384]{};
                std::snprintf(line,sizeof(line),"MC avatar diagnostic: visible=%u reason=%s feet=(%.3f,%.3f,%.3f) cameraMode=%u FOV=%.2f",
                    avatar.Visible()?1u:0u,avatar.Reason(),mc.x,mc.y,mc.z,mc.cameraMode,frame.verticalFov);
                LogLine(line);lastAvatarLog=now;
            }
        }
        static DiagnosticWalk pilot(true,manualEnabled);
        static DiagnosticMovement movement(jumpEnabled);
        static PlaySession play;
        static PlayToggleKey toggleKey;
        static bool lastPlayActive=false,lastPlayGui=false;
        ManualInput playInput;
        static ULONGLONG lastPilotLog=0,nextPilotPerspective=0;
        static std::uint8_t lastMovementKeys=0;
        static unsigned lastJumpState=0;
        bool pilotDisplay=false;
        if(pilotEnabled) {
            const bool toggle=playEnabled&&toggleKey.Update((::GetAsyncKeyState(VK_F8)&0x8000)!=0,
                focused&&game&&!::IsIconic(game));
            const bool toggleOff=toggle&&play.Active();
            if(toggle&&!play.Active()) {
                PublishPilotCommand({});link.PushInput(skycraft::proto::kInReleaseAll,0);
                native::RequestPilotRecovery();play.Resume();
                // A Minecraft screen left open when play ended (pause menu,
                // inventory, chat) blocks the fresh handoff, and the hidden
                // window never sees Escape itself. Close it for the player.
                if(haveMc&&(mc.flags&skycraft::proto::kMcScreenOpen)) {
                    link.PushInput(skycraft::proto::kInKey,41,1);link.PushInput(skycraft::proto::kInKey,41,0);
                    LogLine("MC play: closing the Minecraft screen left open so F8 can resume");
                }
                LogLine("MC play: F8 resume requested; rechecking native player and collision before takeover");
            }
            const auto status=ReadPilotStatus();
            const auto now=::GetTickCount64();const auto before=movementEnabled?static_cast<unsigned>(movement.Status()):static_cast<unsigned>(pilot.Status());
            const bool capture=playEnabled?play.Active():movementEnabled?movement.Active():pilot.Active();
            const auto input=manualEnabled?PollManualInput(capture&&!status.failed,playEnabled,(mc.flags&skycraft::proto::kMcScreenOpen)!=0,width,height):ManualInput{};
            playInput=input;
            if(playEnabled) {
                play.Update(haveGame&&(mc.cameraMode==1||native::CameraReady())?&frame:nullptr,haveMc?&mc:nullptr,haveMc&&!status.failed,
                    focused&&game&&!::IsIconic(game)&&width>=100&&height>=100,
                    haveGame&&GameCollisionDrained(frame.collisionEpoch)&&link.CollisionConsumed(),now,sky,
                    input.movement,input.stop||toggleOff,input.ready,status.hidden,native::CameraReady());
                if(input.overflow) {play.Stop("input queue overflow; returning Geralt");sky.flags=skycraft::proto::kSkyMenuOpen;}
                if(play.Active()&&input.ready) {
                    // Cursor positions are tracked at native resolution, while
                    // Minecraft renders its HUD into the smaller transfer image.
                    auto cursor=[&](int x,int y) {link.PushInput(skycraft::proto::kInCursor,0,
                        HudCoordinate(x,width,transfer.width),HudCoordinate(y,height,transfer.height));};
                    if(input.gui) cursor(input.cursorX,input.cursorY);
                    for(unsigned i=0;i<input.eventCount;++i) {
                        const auto& event=input.events[i];
                        // F5 cycles first person, behind and front. Front view is
                        // skipped until the camera rotation layout is verified.
                        if(event.type==skycraft::proto::kInKey&&event.code==62&&!input.gui&&native::CameraReady()) {
                            if(event.a) for(unsigned press=0;press<(mc.cameraMode==1&&!native::FrontViewReady()?2u:1u);++press) {
                                link.PushInput(skycraft::proto::kInKey,62,1);link.PushInput(skycraft::proto::kInKey,62,0);
                            }
                            continue;
                        }
                        if(event.type==skycraft::proto::kInCursor) cursor(event.a,event.b);
                        else link.PushInput(static_cast<skycraft::proto::InputType>(event.type),event.code,event.a,event.b,event.c);
                    }
                }
                if(lastPlayActive&&!play.Active()) {
                    link.PushInput(skycraft::proto::kInReleaseAll,0);
                    // Do not strand a Minecraft screen in the hidden window.
                    if(haveMc&&(mc.flags&skycraft::proto::kMcScreenOpen)) {
                        link.PushInput(skycraft::proto::kInKey,41,1);link.PushInput(skycraft::proto::kInKey,41,0);
                    }
                }
                const bool gui=(mc.flags&skycraft::proto::kMcScreenOpen)!=0;
                if(play.Active()!=lastPlayActive||gui!=lastPlayGui||play.Keys()!=lastMovementKeys||now-lastPilotLog>=1000) {
                    char line[512]{};std::snprintf(line,sizeof(line),"MC play: active=%u gui=%u hidden=%u nativeFailed=%u moves=%u keys=%u lag=%.3f MC=(%.3f,%.3f,%.3f) native=(%.3f,%.3f,%.3f) reason=%s",play.Active()?1u:0u,gui?1u:0u,status.hidden?1u:0u,status.failed?1u:0u,status.moves,play.Keys(),play.FollowLag(),mc.curX,mc.curY,mc.curZ,frame.feet[0],frame.feet[1],frame.feet[2],play.Reason());LogLine(line);
                    lastPilotLog=now;lastMovementKeys=play.Keys();
                }
                lastPlayActive=play.Active();lastPlayGui=gui;
                // Companion script mod: NPC hits only redirect Geralt's damage while
                // Minecraft owns the camera; queued hurts become Minecraft damage.
                const bool scriptPlay=play.Active()&&native::CameraReady();
                script::SetScriptPlayActive(scriptPlay);
                for(const auto& hurt:script::TakeScriptHurts())
                    if(scriptPlay) link.PushInput(skycraft::proto::kInHurt,hurt.kind,hurt.a,hurt.b,0);
            } else if(movementEnabled) movement.Update(haveGame&&mc.cameraMode==1?&frame:nullptr,haveMc?&mc:nullptr,haveMc&&!status.failed,
                focused&&game&&!::IsIconic(game)&&width>=100&&height>=100,
                haveGame&&GameCollisionDrained(frame.collisionEpoch)&&link.CollisionConsumed(),now,sky,
                input.movement,input.stop,input.ready,status.hidden);
            else pilot.Update(haveGame&&mc.cameraMode==1?&frame:nullptr,haveMc?&mc:nullptr,haveMc&&!status.failed,
                focused&&game&&!::IsIconic(game)&&width>=100&&height>=100,
                haveGame&&GameCollisionDrained(frame.collisionEpoch)&&link.CollisionConsumed(),now,sky,
                input.forward,input.stop,!manualEnabled||input.ready,status.hidden);
            const auto desired=playEnabled?play.Keys():movementEnabled?movement.Keys():static_cast<std::uint8_t>(pilot.ForwardDown()?1:0);
            constexpr std::uint16_t codes[]{26,4,22,7,44};
            for(unsigned i=0;i<5;++i) link.PushInput(skycraft::proto::kInKey,codes[i],(desired&(1u<<i))?1:0);
            if(haveMc&&focused&&(mc.flags&skycraft::proto::kMcInWorld)&&!(mc.flags&skycraft::proto::kMcScreenOpen)&&
                ((playEnabled&&play.Active()&&native::CameraReady())?mc.cameraMode==2&&!native::FrontViewReady():mc.cameraMode!=1)&&now>=nextPilotPerspective) {
                link.PushInput(skycraft::proto::kInKey,62,1);link.PushInput(skycraft::proto::kInKey,62,0);nextPilotPerspective=now+750;
            }
            const bool active=playEnabled?play.Active():movementEnabled?movement.Active():pilot.Active();
            pilotDisplay=!status.failed&&(!manualEnabled||input.ready)&&active;
            if(manualEnabled&&!active)
                PollManualInput(false);
            if(!pilotDisplay) PublishPilotCommand({});
            const auto current=movementEnabled?static_cast<unsigned>(movement.Status()):static_cast<unsigned>(pilot.Status());
            const auto jumpState=static_cast<unsigned>(movement.Jump().Status());
            if(!playEnabled&&(before!=current||desired!=lastMovementKeys||(jumpEnabled&&jumpState!=lastJumpState)||now-lastPilotLog>=1000)) {
                char line[640]{};std::snprintf(line,sizeof(line),"MC %s diagnostic: state=%u reason=%s distance=%.3f nativeHidden=%u nativeFailed=%u moves=%u keys=%u manual=%u inputReady=%u wSeen=%u wBlocked=%u wPassed=%u stopSeen=%u hookError=%u rawSeen=%u rawBlocked=%u asyncBlocked=%u path=%.3f",
                    jumpEnabled?"jump":movementEnabled?"WASD":"pilot",current,movementEnabled?movement.Reason():pilot.Reason(),movementEnabled?movement.Distance():pilot.Distance(),status.hidden?1u:0u,status.failed?1u:0u,status.moves,desired,manualEnabled?1u:0u,input.ready?1u:0u,input.wSeen,input.wBlocked,input.wPassed,input.stopSeen,input.hookError,input.rawSeen,input.rawBlocked,input.asyncBlocked,movementEnabled?movement.Path():0.0);
                LogLine(line);lastPilotLog=now;lastMovementKeys=desired;
                if(jumpEnabled) {
                    std::snprintf(line,sizeof(line),"MC jump observation: phase=%u peak=%.3f rawY=%.3f nativeY=%.3f mcFlags=%u",jumpState,movement.Jump().Peak(),mc.curY,frame.feet[1],mc.flags);
                    LogLine(line);lastJumpState=jumpState;
                }
            }
        }
        // Minecraft keys saved digging by a world identity that survives restarts.
        auto publishSky=[&] {
            auto published=sky;
            if(const auto persistent=PersistentWorld(sky.worldId)) published.worldId=persistent;
            link.WriteSkyState(published);
        };
        publishSky();
        auto abortManualRender=[&] {
            if(!manualEnabled) return;
            if(playEnabled) play.Stop("avatar display unavailable; returning Geralt");
            else if(movementEnabled) movement.Stop("avatar display unavailable; returning Geralt");
            else pilot.Stop("avatar display unavailable; returning Geralt");
            PollManualInput(false);
            link.PushInput(skycraft::proto::kInReleaseAll,0);
            sky.flags=skycraft::proto::kSkyMenuOpen;publishSky();
            PublishPilotCommand({});
        };
        static bool loggedWaiting = false;
        if (!haveGame && !loggedWaiting) {
            LogLine("waiting for a verified REDengine adapter; Geralt remains under Witcher control");
            loggedWaiting = true;
        }
        const bool display=playing||(avatarEnabled&&!pilotEnabled&&avatar.Visible())||pilotDisplay;
        if (!display || !EnsureWindow(game) || !EnsureBitmap(width, height)) {
            freshMcSamples=newHudFrames=0;
            if(pilotDisplay) abortManualRender();
            if(pilotEnabled) PublishPilotCommand({});
            if (g_window) ::ShowWindow(g_window, SW_HIDE);
            g_hud.clear();
            PumpRender();
            return;
        }
        if ((playEnabled||(!avatarEnabled&&!pilotEnabled))&&link.AcquireOverlayFrame()) {
            const auto* hdr = link.FrontHeader();
            if (hdr->width && hdr->height && hdr->width <= skycraft::proto::kMaxOverlayW &&
                hdr->height <= skycraft::proto::kMaxOverlayH) {
                const auto bytes = static_cast<std::size_t>(hdr->width) * hdr->height * 4;
                g_hud.resize(bytes);
                std::memcpy(g_hud.data(), link.FrontPixels(), bytes);
                g_hudW = static_cast<int>(hdr->width);
                g_hudH = static_cast<int>(hdr->height);
                g_hudBottom = (hdr->flags & 1) != 0;
                g_hudChanged=true;
                ++newHudFrames;
                static bool loggedHud=false;
                if(playEnabled&&!loggedHud) {LogLine("MC play: Minecraft HUD frame acquired");loggedHud=true;}
            }
        }
        // DrawWorld clears its GPU target and overwrites every bitmap row.
        // Composite the HUD only after a successful fresh readback.
        LARGE_INTEGER renderBegin{},worldDone{},hudDone{},uploadDone{},renderFrequency{};
        QueryPerformanceFrequency(&renderFrequency);QueryPerformanceCounter(&renderBegin);
        if(!DrawWorld(static_cast<std::uint8_t*>(g_bits), width, height, mc, &frame,!playEnabled&&(avatarEnabled||pilotEnabled))) {
            abortManualRender();
            if(pilotEnabled) PublishPilotCommand({});
            ::ShowWindow(g_window,SW_HIDE);return;
        }
        QueryPerformanceCounter(&worldDone);
        if(playEnabled||(!avatarEnabled&&!pilotEnabled)) CompositeHud();
        if(playEnabled&&playInput.gui) {
            // Virtual cursor remains usable when Witcher recentres its OS cursor.
            auto* pixels=static_cast<std::uint8_t*>(g_bits);
            for(int offset=-6;offset<=6;++offset) for(int axis=0;axis<2;++axis) {
                const int x=playInput.cursorX+(axis?0:offset),y=playInput.cursorY+(axis?offset:0);
                if(x>=0&&x<width&&y>=0&&y<height) {
                    auto* p=pixels+(static_cast<std::size_t>(y)*width+x)*4;
                    p[0]=p[1]=p[2]=p[3]=255;
                }
            }
        }
        POINT origin{};
        QueryPerformanceCounter(&hudDone);
        ::ClientToScreen(game, &origin);
        SIZE size{width, height};
        POINT src{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const bool uploaded=::UpdateLayeredWindow(g_window, nullptr, &origin, &size, g_dc, &src, 0, &blend, ULW_ALPHA)!=FALSE;
        QueryPerformanceCounter(&uploadDone);
        static double worldMs=0,hudMs=0,uploadMs=0,prepareMs=0,finishMs=0;
        static unsigned timingFrames=0;static ULONGLONG timingAt=::GetTickCount64();
        const double clockToMs=1000.0/renderFrequency.QuadPart;
        prepareMs+=(renderBegin.QuadPart-overlayBegin.QuadPart)*clockToMs;
        worldMs+=(worldDone.QuadPart-renderBegin.QuadPart)*clockToMs;
        hudMs+=(hudDone.QuadPart-worldDone.QuadPart)*clockToMs;
        uploadMs+=(uploadDone.QuadPart-hudDone.QuadPart)*clockToMs;++timingFrames;
        static ULONGLONG lastPixelsLog=0;
        if(avatarEnabled&&::GetTickCount64()-lastPixelsLog>=3000) {
            unsigned pixels=0;const auto* bgra=static_cast<const std::uint8_t*>(g_bits);
            for(std::size_t i=0;i<static_cast<std::size_t>(width)*height;++i) if(bgra[i*4+3]) ++pixels;
            char line[128]{};std::snprintf(line,sizeof(line),"MC avatar pixels: opaque=%u viewport=%dx%d uploaded=%u",pixels,width,height,uploaded?1u:0u);
            LogLine(line);lastPixelsLog=::GetTickCount64();
        }
        if(pilotEnabled) {
            bool pixels=false;const auto* bgra=static_cast<const std::uint8_t*>(g_bits);
            if(!playEnabled) for(std::size_t i=0;i<static_cast<std::size_t>(width)*height;++i) if(bgra[i*4+3]) { pixels=true;break; }
            PilotCommand command;
            // A fully occluded/off-screen avatar has no visible pixels. That
            // is a valid play frame, not a reason to restore Geralt.
            command.active=uploaded&&(pixels||playEnabled)&&pilotDisplay;
            command.cameraOwned=playEnabled&&native::CameraReady();
            command.publishedAt=::GetTickCount64();
            command.groundFollow=playEnabled&&play.GroundFollow();
            command.world=frame.worldId;command.epoch=frame.collisionEpoch;command.relocation=frame.relocation;
            command.feet[0]=mc.curX;command.feet[1]=mc.curY;command.feet[2]=mc.curZ;
            if(playEnabled) {
                play.PilotFeet(mc,command.feet);
                command.interpolateFollow=true;
                command.followState=mc;
            }
            else if(movementEnabled) movement.PilotFeet(mc,command.feet);
            PublishPilotCommand(command);
            if(manualEnabled&&!command.active) abortManualRender();
        }
        if(!uploaded) { ::ShowWindow(g_window,SW_HIDE);return; }
        static HWND placedWindow=nullptr;static RECT placed{};
        if(placedWindow!=g_window||placed.left!=origin.x||placed.top!=origin.y||
            placed.right!=width||placed.bottom!=height||!::IsWindowVisible(g_window)) {
            if(::SetWindowPos(g_window, HWND_TOP, origin.x, origin.y, width, height,SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
                placedWindow=g_window;placed={origin.x,origin.y,width,height};
            }
        }
        LARGE_INTEGER overlayEnd{};QueryPerformanceCounter(&overlayEnd);
        finishMs+=(overlayEnd.QuadPart-uploadDone.QuadPart)*clockToMs;
        if(::GetTickCount64()-timingAt>=5000) {
            char line[384]{};std::snprintf(line,sizeof(line),"overlay stages: viewport=%dx%d HUD=%dx%d frames=%u freshMC=%u freshHUD=%u prepare=%.2fms world/readback=%.2fms HUD=%.2fms upload=%.2fms finish=%.2fms",
                width,height,g_hudW,g_hudH,timingFrames,freshMcSamples,newHudFrames,prepareMs/timingFrames,worldMs/timingFrames,hudMs/timingFrames,uploadMs/timingFrames,finishMs/timingFrames);
            LogLine(line);prepareMs=worldMs=hudMs=uploadMs=finishMs=0;timingFrames=0;timingAt=::GetTickCount64();
            freshMcSamples=newHudFrames=0;
        }
    }
}

