#include "Link.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	constexpr int kFloor = 63;
	constexpr int kHalf = 8;
	constexpr std::uint32_t kEpoch = 1;
	constexpr std::uint32_t kWorld = 1;

	bool PublishFloor(witchcraft::Link& a_link)
	{
		std::uint32_t epoch = kEpoch;
		if (!a_link.WriteCollision(skycraft::proto::kColClear, &epoch, sizeof(epoch))) {
			return false;
		}

		skycraft::proto::ColRegion region{};
		region.minX = -kHalf;
		region.minY = 56;
		region.minZ = -kHalf;
		region.maxX = kHalf;
		region.maxY = 64;
		region.maxZ = kHalf;
		region.epoch = kEpoch;
		region.count = (kHalf * 2 + 1) * (kHalf * 2 + 1);

		std::vector<std::uint8_t> payload(sizeof(region) + sizeof(skycraft::proto::ColBlock) * region.count);
		std::memcpy(payload.data(), &region, sizeof(region));
		auto* blocks = reinterpret_cast<skycraft::proto::ColBlock*>(payload.data() + sizeof(region));
		std::uint32_t n = 0;
		for (int x = -kHalf; x <= kHalf; ++x) {
			for (int z = -kHalf; z <= kHalf; ++z) {
				auto& block = blocks[n++];
				block.x = x;
				block.y = kFloor;
				block.z = z;
				for (auto& layer : block.bits) {
					layer = ~0ull;
				}
			}
		}
		return a_link.WriteCollision(skycraft::proto::kColRegion, payload.data(), static_cast<std::uint32_t>(payload.size()));
	}

	void PublishWater(witchcraft::Link& a_link)
	{
		skycraft::proto::WaterGrid grid{};
		grid.originX = -8;
		grid.originZ = -8;
		grid.worldId = kWorld;
		for (float& cell : grid.surface) {
			cell = skycraft::proto::kNoWater;
		}
		a_link.WriteWaterGrid(grid);
	}

	std::uint16_t Scancode(int a_vk)
	{
		if (a_vk >= 'A' && a_vk <= 'Z') {
			return static_cast<std::uint16_t>(4 + (a_vk - 'A'));
		}
		if (a_vk >= '1' && a_vk <= '9') {
			return static_cast<std::uint16_t>(30 + (a_vk - '1'));
		}
		if (a_vk == '0') {
			return 39;
		}
		switch (a_vk) {
		case VK_SPACE: return 44;
		case VK_SHIFT: return 225;
		case VK_CONTROL: return 224;
		case VK_ESCAPE: return 41;
		case VK_TAB: return 43;
		case VK_F5: return 62;
		default: return 0;
		}
	}

	void ForwardKeys(witchcraft::Link& a_link, bool a_capture)
	{
		static bool down[256]{};
		const int keys[] = {
			'W', 'A', 'S', 'D', 'E', 'Q', 'F', ' ',
			VK_SPACE, VK_SHIFT, VK_CONTROL, VK_ESCAPE, VK_TAB, VK_F5,
			'1', '2', '3', '4', '5', '6', '7', '8', '9'
		};
		for (int vk : keys) {
			const bool pressed = a_capture && (::GetAsyncKeyState(vk) & 0x8000) != 0;
			if (pressed == down[vk]) {
				continue;
			}
			down[vk] = pressed;
			const auto code = Scancode(vk);
			if (code) {
				a_link.PushInput(skycraft::proto::kInKey, code, pressed ? 1 : 0);
			}
		}
		static bool left = false;
		static bool right = false;
		const bool l = a_capture && (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		const bool r = a_capture && (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		if (l != left) {
			left = l;
			a_link.PushInput(skycraft::proto::kInMouseButton, 1, l ? 1 : 0);
		}
		if (r != right) {
			right = r;
			a_link.PushInput(skycraft::proto::kInMouseButton, 3, r ? 1 : 0);
		}
	}
}

int main()
{
	auto& link = witchcraft::Link::Get();
	if (!link.Create() || !PublishFloor(link)) {
		std::fputs("WitchCraft: could not create the Minecraft link\n", stderr);
		return 1;
	}
	PublishWater(link);
	link.WriteActors(nullptr, 0);

	std::printf("WitchCraft stand-in. Mapping Local\\SkyCraft_v1, flat floor at y=%d.\n", kFloor + 1);
	std::puts("Start SkyCraft's Minecraft without Skyrim. F8 captures the mouse. Ctrl+C quits.");

	float yaw = 0.0f;
	float pitch = 0.0f;
	bool capture = false;
	bool wasAlive = false;
	POINT last{};
	::GetCursorPos(&last);
	ULONGLONG nextLog = 0;

	for (;;) {
		if (::GetAsyncKeyState(VK_F8) & 1) {
			capture = !capture;
			std::printf("mouse %s\n", capture ? "captured" : "released");
			::GetCursorPos(&last);
		}
		if (capture) {
			POINT now{};
			::GetCursorPos(&now);
			yaw -= static_cast<float>(now.x - last.x) * 0.15f;
			pitch += static_cast<float>(now.y - last.y) * 0.15f;
			if (pitch > 90.0f) {
				pitch = 90.0f;
			}
			if (pitch < -90.0f) {
				pitch = -90.0f;
			}
			::SetCursorPos(last.x, last.y);
		}
		ForwardKeys(link, capture || ::GetForegroundWindow() == ::GetConsoleWindow());

		skycraft::proto::SkyState sky{};
		sky.flags = skycraft::proto::kSkyInGame;
		sky.worldId = kWorld;
		sky.collisionEpoch = kEpoch;
		sky.posX = 0.5;
		sky.posY = 64.0;
		sky.posZ = 0.5;
		sky.yaw = yaw;
		sky.pitch = pitch;
		sky.teleportSeq = 1;
		sky.viewportW = 1920;
		sky.viewportH = 1080;
		sky.gameHour = 14.0f;
		link.WriteSkyState(sky);
		link.Heartbeat();

		const bool alive = link.McAlive();
		if (alive && !wasAlive) {
			std::printf("Minecraft connected (pid %u)\n", link.McPid());
		}
		if (!alive && wasAlive) {
			std::puts("Minecraft disconnected");
		}
		wasAlive = alive;

		skycraft::proto::McState mc{};
		const auto now = ::GetTickCount64();
		if (alive && link.ReadMcState(mc) && now >= nextLog) {
			nextLog = now + 500;
			std::printf("player %.2f %.2f %.2f  %s\n", mc.x, mc.y, mc.z, (mc.flags & skycraft::proto::kMcFlying) ? "flying" : "on foot");
		}
		skycraft::proto::McEvent event{};
		while (link.PopEvent(event)) {
			if (event.type == skycraft::proto::kEvExplosion) {
				std::printf("explosion at %.1f %.1f %.1f radius %.1f\n", event.a, event.b, event.c, event.d);
			} else if (event.type == skycraft::proto::kEvHitActor) {
				std::printf("hit actor %u for %.1f\n", event.formId, event.a);
			}
		}
		::Sleep(16);
	}
}
