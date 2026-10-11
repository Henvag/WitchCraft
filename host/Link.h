#pragma once

#include "skycraft_protocol.h"

#include <cstdint>
#include <functional>

namespace witchcraft
{
	// Owner of the shared-memory mapping. The host creates it; Minecraft opens it.
	// Layout and mapping name are SkyCraft's, so its Fabric mod is the Minecraft side.
	class Link
	{
	public:
		static Link& Get();

		bool Create();
		[[nodiscard]] bool Valid() const { return base_ != nullptr; }

		[[nodiscard]] bool McAlive() const;
		void               Heartbeat();
		[[nodiscard]] std::uint32_t McPid() const;
		[[nodiscard]] bool CollisionConsumed() const;

		void WriteSkyState(const skycraft::proto::SkyState& a_state);
		void WriteWaterGrid(const skycraft::proto::WaterGrid& a_grid);
		bool ReadMcState(skycraft::proto::McState& a_out) const;

		void PushInput(skycraft::proto::InputType a_type, std::uint16_t a_code, std::int32_t a_a = 0, std::int32_t a_b = 0, std::int32_t a_c = 0);
		bool WriteCollision(skycraft::proto::ColType a_type, const void* a_payload, std::uint32_t a_bytes);
		void WriteActors(const skycraft::proto::ActorRecord* a_records, std::uint32_t a_count);
		bool PopEvent(skycraft::proto::McEvent& a_out);

		// Latest Minecraft HUD frame (hotbar, hearts, hand). False if nothing new arrived.
		bool AcquireOverlayFrame();

		// Block meshes, the player body, and other entities. Minecraft waits until these are consumed.
		void DrainRender(const std::function<void(std::uint32_t, const std::uint8_t*, std::uint32_t)>& a_fn, std::uint64_t a_maxBytes);
		[[nodiscard]] const std::uint8_t* FrontPixels() const;
		[[nodiscard]] const skycraft::proto::OverlaySlotHdr* FrontHeader() const;

	private:
		template <class T>
		T* At(std::uint64_t a_off) const { return reinterpret_cast<T*>(base_ + a_off); }

		void*         mapping_{ nullptr };
		std::uint8_t* base_{ nullptr };
		std::uint32_t overlayFront_{ 2 };
	};
}
