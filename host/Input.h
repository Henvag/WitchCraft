#pragma once

#include "skycraft_protocol.h"

#include <Windows.h>
#include <array>

namespace witchcraft
{
    struct ManualInput {
        bool ready=false,forward=false,stop=false;
        std::uint8_t movement=0;
        std::uint32_t wSeen=0,wBlocked=0,wPassed=0,stopSeen=0;
        std::uint32_t hookError=0;
        std::uint32_t rawSeen=0,rawBlocked=0,asyncBlocked=0;
        std::array<skycraft::proto::InputEvent,192> events{};
        unsigned eventCount=0;
        bool overflow=false,gui=false;
        int cursorX=0,cursorY=0;
    };
    // Game-import capture records keys; bounded diagnostic controllers send
    // movement to Minecraft. Raw mouse movement remains with Witcher's camera.
    ManualInput PollManualInput(bool enabled,bool play=false,bool gui=false,int width=0,int height=0);
	// Keys, mouse buttons, the wheel, and look. Same events SkyCraft sends.
	// Capture starts only after Witcher is in a loaded world. Until then every key still reaches Witcher.
	void PollInput(HWND a_game, int a_width, int a_height, const skycraft::proto::McState& a_mc, bool a_haveMc, bool a_playing, float& a_yaw, float& a_pitch);
	// Legacy relative input accumulator; look now comes from the Witcher camera frame.
	void NoteMouseDelta(int a_dx, int a_dy);
	// Mouse hook events. index 0 left, 1 middle, 2 right.
	void NoteMouseButton(int a_index, bool a_down);
}
