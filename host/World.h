#pragma once

#include "skycraft_protocol.h"
#include "GameBridge.h"

#include <cstdint>

namespace witchcraft
{
	// Draws Minecraft's block sections, the player body, and other entities into a
	// premultiplied BGRA image the size of the Witcher window. The HUD is composited
	// on top by the caller. False until a frame was actually drawn.
	bool DrawWorld(std::uint8_t* a_bgra, int a_width, int a_height, const skycraft::proto::McState& a_mc, const GameFrame* a_game = nullptr, bool avatarOnly = false);

	// Take block meshes off the queue without drawing. Safe to call between frames.
	void PumpRender();
}
