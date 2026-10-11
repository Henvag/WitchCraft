#include "Input.h"

#include "Link.h"

#include <array>
#include <cstdint>

namespace witchcraft
{
	namespace
	{
		constexpr auto kScanToSdl = [] {
			std::array<std::uint16_t, 256> t{};
			t[0x01] = 41;
			for (int i = 0; i < 9; ++i) {
				t[0x02 + i] = static_cast<std::uint16_t>(30 + i);
			}
			t[0x0B] = 39;
			t[0x0C] = 45;
			t[0x0D] = 46;
			t[0x0E] = 42;
			t[0x0F] = 43;
			t[0x10] = 20;
			t[0x11] = 26;
			t[0x12] = 8;
			t[0x13] = 21;
			t[0x14] = 23;
			t[0x15] = 28;
			t[0x16] = 24;
			t[0x17] = 12;
			t[0x18] = 18;
			t[0x19] = 19;
			t[0x1A] = 47;
			t[0x1B] = 48;
			t[0x1C] = 40;
			t[0x1D] = 224;
			t[0x1E] = 4;
			t[0x1F] = 22;
			t[0x20] = 7;
			t[0x21] = 9;
			t[0x22] = 10;
			t[0x23] = 11;
			t[0x24] = 13;
			t[0x25] = 14;
			t[0x26] = 15;
			t[0x27] = 51;
			t[0x28] = 52;
			t[0x29] = 53;
			t[0x2A] = 225;
			t[0x2B] = 49;
			t[0x2C] = 29;
			t[0x2D] = 27;
			t[0x2E] = 6;
			t[0x2F] = 25;
			t[0x30] = 5;
			t[0x31] = 17;
			t[0x32] = 16;
			t[0x33] = 54;
			t[0x34] = 55;
			t[0x35] = 56;
			t[0x36] = 229;
			t[0x37] = 85;
			t[0x38] = 226;
			t[0x39] = 44;
			t[0x3A] = 57;
			for (int i = 0; i < 10; ++i) {
				t[0x3B + i] = static_cast<std::uint16_t>(58 + i);
			}
			t[0x45] = 83;
			t[0x46] = 71;
			t[0x57] = 68;
			t[0x58] = 69;
			return t;
		}();

		bool                  g_capture = false;
		volatile bool         g_gui = false;

		bool                  g_hidCursor = false;



		ULONGLONG             g_nextPerspective = 0;
		std::array<bool, 256> g_keyDown{};
		bool                  g_mouseDown[3]{};
		HHOOK                 g_mouseHook = nullptr;
		HHOOK                 g_keyHook = nullptr;
		volatile LONG         g_wheel = 0;
		volatile LONG         g_rawDx = 0;
		volatile LONG         g_rawDy = 0;

		std::uint16_t ToSdl(int a_vk)
		{
			switch (a_vk) {
			case VK_UP: return 82;
			case VK_DOWN: return 81;
			case VK_LEFT: return 80;
			case VK_RIGHT: return 79;
			case VK_INSERT: return 73;
			case VK_DELETE: return 76;
			case VK_HOME: return 74;
			case VK_END: return 77;
			case VK_PRIOR: return 75;
			case VK_NEXT: return 78;
			case VK_RCONTROL: return 228;
			case VK_RMENU: return 230;
			default: break;
			}
			const UINT scan = ::MapVirtualKeyW(static_cast<UINT>(a_vk), MAPVK_VK_TO_VSC);
			if (scan == 0 || scan >= 256) {
				return 0;
			}
			return kScanToSdl[scan];
		}

		// Minecraft owns locomotion. The adapter moves Geralt's anchor from McState.
		bool PassThroughKey(DWORD a_vk)
		{
			if (a_vk == VK_LWIN || a_vk == VK_RWIN) {
				return true;
			}
			if (a_vk == VK_TAB && ((::GetAsyncKeyState(VK_MENU) & 0x8000) || (::GetAsyncKeyState(VK_LMENU) & 0x8000))) {
				return true;
			}
			if (!g_gui && a_vk == 'G') {
				return true;
			}
			return false;
		}

		LRESULT CALLBACK MouseHook(int a_code, WPARAM a_msg, LPARAM a_param)
		{
			if (a_code == HC_ACTION && g_capture) {
				if (a_msg == WM_LBUTTONDOWN || a_msg == WM_LBUTTONUP)
					NoteMouseButton(0, a_msg == WM_LBUTTONDOWN);
				if (a_msg == WM_MBUTTONDOWN || a_msg == WM_MBUTTONUP)
					NoteMouseButton(1, a_msg == WM_MBUTTONDOWN);
				if (a_msg == WM_RBUTTONDOWN || a_msg == WM_RBUTTONUP)
					NoteMouseButton(2, a_msg == WM_RBUTTONDOWN);
				if (a_msg == WM_MOUSEWHEEL) {
					const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(a_param);
					::InterlockedExchangeAdd(&g_wheel, static_cast<LONG>(static_cast<short>(HIWORD(info->mouseData))));
					return 1;
				}
				// Movement is left alone so the cursor can operate menus and Witcher can look.
				if (!g_gui && (a_msg == WM_LBUTTONDOWN || a_msg == WM_LBUTTONUP || a_msg == WM_RBUTTONDOWN || a_msg == WM_RBUTTONUP ||
					a_msg == WM_MBUTTONDOWN || a_msg == WM_MBUTTONUP)) {
					return 1;
				}
				if (g_gui && (a_msg == WM_LBUTTONDOWN || a_msg == WM_LBUTTONUP || a_msg == WM_RBUTTONDOWN || a_msg == WM_RBUTTONUP ||
					a_msg == WM_MBUTTONDOWN || a_msg == WM_MBUTTONUP)) {
					return 1;
				}
			}
			return ::CallNextHookEx(g_mouseHook, a_code, a_msg, a_param);
		}

		LRESULT CALLBACK KeyHook(int a_code, WPARAM a_msg, LPARAM a_param)
		{
			if (a_code == HC_ACTION && g_capture && (a_msg == WM_KEYDOWN || a_msg == WM_KEYUP || a_msg == WM_SYSKEYDOWN || a_msg == WM_SYSKEYUP)) {
				const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(a_param);
				const bool down = a_msg == WM_KEYDOWN || a_msg == WM_SYSKEYDOWN;
				if (info->vkCode < g_keyDown.size() && info->vkCode != VK_F5 && !PassThroughKey(info->vkCode)) {
					if (g_keyDown[info->vkCode] != down) {
						g_keyDown[info->vkCode] = down;
						if (const auto sdl = ToSdl(static_cast<int>(info->vkCode)))
							Link::Get().PushInput(skycraft::proto::kInKey, sdl, down ? 1 : 0);
					}
				}
				if (!PassThroughKey(info->vkCode)) {
					return 1;
				}
			}
			return ::CallNextHookEx(g_keyHook, a_code, a_msg, a_param);
		}

		void InstallHooks()
		{
            HMODULE module=nullptr;
            if(!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&KeyHook),&module)) return;
			if (!g_mouseHook) {
				g_mouseHook = ::SetWindowsHookExW(WH_MOUSE_LL, MouseHook, module, 0);
			}
			if (!g_keyHook) {
				g_keyHook = ::SetWindowsHookExW(WH_KEYBOARD_LL, KeyHook, module, 0);
			}
		}

	}


	void NoteMouseDelta(int a_dx, int a_dy)
	{
		if (a_dx != 0) {
			::InterlockedExchangeAdd(&g_rawDx, a_dx);
		}
		if (a_dy != 0) {
			::InterlockedExchangeAdd(&g_rawDy, a_dy);
		}
	}

	void NoteMouseButton(int a_index, bool a_down)
	{
		if (!g_capture || a_index < 0 || a_index > 2) {
			return;
		}
		if (g_mouseDown[a_index] == a_down) {
			return;
		}
		g_mouseDown[a_index] = a_down;
		static const std::uint16_t kSdl[3] = { 1, 2, 3 };
		Link::Get().PushInput(skycraft::proto::kInMouseButton, kSdl[a_index], a_down ? 1 : 0);
	}

    void PollInput(HWND a_game, int a_width, int a_height, const skycraft::proto::McState& a_mc,
        bool a_haveMc, bool a_playing, float& a_yaw, float& a_pitch)
    {
        (void)a_yaw;
        (void)a_pitch; // Witcher's camera supplies authoritative look to SkyState.
        auto& link = Link::Get();
        DWORD foregroundPid = 0;
        ::GetWindowThreadProcessId(::GetForegroundWindow(), &foregroundPid);
        const bool focused = foregroundPid == ::GetCurrentProcessId();
        const bool previous = g_capture;
        g_capture = a_playing && a_haveMc && focused &&
            (a_mc.flags & skycraft::proto::kMcInWorld) != 0;
        g_gui = g_capture && (a_mc.flags & skycraft::proto::kMcScreenOpen) != 0;
        if (!g_capture) {
            if (previous) link.PushInput(skycraft::proto::kInReleaseAll, 0);
            g_keyDown.fill(false);
            for (auto& down : g_mouseDown) down = false;
            ::InterlockedExchange(&g_rawDx, 0);
            ::InterlockedExchange(&g_rawDy, 0);
            ::InterlockedExchange(&g_wheel, 0);
            if (g_hidCursor) { ::ShowCursor(TRUE); g_hidCursor = false; }
            return;
        }
        InstallHooks();
        if (!previous) {
            // Seed held movement keys on entry; blocked keys thereafter come
            // exclusively from hook events, including short presses.
            for (int vk = 8; vk < 256; ++vk) {
                if (vk == VK_F5 || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
                    PassThroughKey(static_cast<DWORD>(vk))) continue;
                if ((::GetAsyncKeyState(vk) & 0x8000) && ToSdl(vk)) {
                    g_keyDown[vk] = true;
                    link.PushInput(skycraft::proto::kInKey, ToSdl(vk), 1);
                }
            }
        }
        ::InterlockedExchange(&g_rawDx, 0);
        ::InterlockedExchange(&g_rawDy, 0);
        if (!g_gui) {
            if (!g_hidCursor) { ::ShowCursor(FALSE); g_hidCursor = true; }
            if (a_mc.cameraMode != 1 && ::GetTickCount64() >= g_nextPerspective) {
                link.PushInput(skycraft::proto::kInKey, 62, 1);
                link.PushInput(skycraft::proto::kInKey, 62, 0);
                g_nextPerspective = ::GetTickCount64() + 600;
            }
        } else {
            if (g_hidCursor) { ::ShowCursor(TRUE); g_hidCursor = false; }
            POINT origin{}, cursor{};
            ::ClientToScreen(a_game, &origin);
            if (a_width > 0 && a_height > 0 && ::GetCursorPos(&cursor))
                link.PushInput(skycraft::proto::kInCursor, 0, cursor.x - origin.x, cursor.y - origin.y);
        }
        const LONG wheel = ::InterlockedExchange(&g_wheel, 0);
        if (wheel) link.PushInput(skycraft::proto::kInScroll, 0, wheel);
    }
}
