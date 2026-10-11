#include "Link.h"

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <sddl.h>
#include <string>
#include <vector>

namespace witchcraft
{
	namespace
	{
		template <class T>
		std::atomic_ref<T> Atomic(T& a_value)
		{
			return std::atomic_ref<T>(a_value);
		}

		constexpr std::uint64_t kMcTimeoutMs = 3000;

		// Same access rules as SkyCraft: this user, plus SYSTEM and Administrators, at medium
		// integrity. An elevated host would otherwise hide the mapping from a normal Minecraft.
		PSECURITY_DESCRIPTOR SharedWithThisUser()
		{
			std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
			HANDLE       token = nullptr;
			if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
				DWORD size = 0;
				::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
				std::vector<std::uint8_t> buffer(size);
				if (size && ::GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
					LPWSTR sid = nullptr;
					if (::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) {
						sddl += std::wstring(L"(A;;GA;;;") + sid + L")";
						::LocalFree(sid);
					}
				}
				::CloseHandle(token);
			}
			sddl += L"S:(ML;;NW;;;ME)";
			PSECURITY_DESCRIPTOR descriptor = nullptr;
			if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
				return nullptr;
			}
			return descriptor;
		}
	}

	Link& Link::Get()
	{
		static Link link;
		return link;
	}

	bool Link::Create()
	{
		if (base_) {
			return true;
		}
		const auto size = skycraft::proto::kMappingBytes;
		SECURITY_ATTRIBUTES access{ sizeof(access), SharedWithThisUser(), FALSE };
		mapping_ = ::CreateFileMappingW(INVALID_HANDLE_VALUE, access.lpSecurityDescriptor ? &access : nullptr, PAGE_READWRITE,
			static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xFFFFFFFF), skycraft::proto::kMappingName);
		if (access.lpSecurityDescriptor) {
			::LocalFree(access.lpSecurityDescriptor);
		}
		if (!mapping_) {
			return false;
		}
		base_ = static_cast<std::uint8_t*>(::MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
		if (!base_) {
			::CloseHandle(mapping_);
			mapping_ = nullptr;
			return false;
		}

		auto* header = At<skycraft::proto::Header>(skycraft::proto::kOffHeader);
		std::memset(base_ + skycraft::proto::kOffSkyState, 0, sizeof(skycraft::proto::SkyState));
		std::memset(base_ + skycraft::proto::kOffOverlayCtl, 0, 0x100);
		std::memset(base_ + skycraft::proto::kOffInputRing, 0, skycraft::proto::kInputRingDataOff);
		std::memset(base_ + skycraft::proto::kOffCollisionRing, 0, skycraft::proto::kColRingDataOff);
		std::memset(base_ + skycraft::proto::kOffActorTable, 0, sizeof(skycraft::proto::ActorTable));
		std::memset(base_ + skycraft::proto::kOffEventRing, 0, skycraft::proto::kEventRingDataOff);
		std::memset(base_ + skycraft::proto::kOffWorldEntities, 0, sizeof(skycraft::proto::WorldEntities));
		std::memset(base_ + skycraft::proto::kOffRenderRing, 0, skycraft::proto::kRenRingDataOff);
		header->version = skycraft::proto::kVersion;
		header->skyrimPid = ::GetCurrentProcessId();
		header->skyrimHeartbeatMs = ::GetTickCount64();
		Atomic(header->magic).store(skycraft::proto::kMagic, std::memory_order_release);
		return true;
	}

	bool Link::McAlive() const
	{
		if (!base_) {
			return false;
		}
		auto& beat = At<skycraft::proto::Header>(skycraft::proto::kOffHeader)->mcHeartbeatMs;
		const auto last = Atomic(beat).load(std::memory_order_acquire);
		return last != 0 && ::GetTickCount64() - last < kMcTimeoutMs;
	}

	std::uint32_t Link::McPid() const
	{
		return base_ ? Atomic(At<skycraft::proto::Header>(skycraft::proto::kOffHeader)->mcPid).load(std::memory_order_acquire) : 0;
	}
	bool Link::CollisionConsumed() const
	{
		if (!base_) return false;
		const auto tail=Atomic(*At<std::uint64_t>(skycraft::proto::kOffCollisionRing+skycraft::proto::kColRingTailOff)).load(std::memory_order_acquire);
		const auto head=Atomic(*At<std::uint64_t>(skycraft::proto::kOffCollisionRing+skycraft::proto::kColRingHeadOff)).load(std::memory_order_acquire);
		return tail==head;
	}

	void Link::Heartbeat()
	{
		if (base_) {
			Atomic(At<skycraft::proto::Header>(skycraft::proto::kOffHeader)->skyrimHeartbeatMs).store(::GetTickCount64(), std::memory_order_release);
		}
	}

	void Link::WriteSkyState(const skycraft::proto::SkyState& a_state)
	{
		if (!base_) {
			return;
		}
		auto* dst = At<skycraft::proto::SkyState>(skycraft::proto::kOffSkyState);
		auto  seq = Atomic(dst->seq);
		const auto s = seq.load(std::memory_order_relaxed);
		seq.store(s + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		std::memcpy(reinterpret_cast<std::uint8_t*>(dst) + 4, reinterpret_cast<const std::uint8_t*>(&a_state) + 4, sizeof(skycraft::proto::SkyState) - 4);
		seq.store(s + 2, std::memory_order_release);
	}

	void Link::WriteWaterGrid(const skycraft::proto::WaterGrid& a_grid)
	{
		if (!base_) {
			return;
		}
		auto* dst = At<skycraft::proto::WaterGrid>(skycraft::proto::kOffWaterGrid);
		auto  seq = Atomic(dst->seq);
		const auto s = seq.load(std::memory_order_relaxed);
		seq.store(s + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		std::memcpy(reinterpret_cast<std::uint8_t*>(dst) + 4, reinterpret_cast<const std::uint8_t*>(&a_grid) + 4, sizeof(skycraft::proto::WaterGrid) - 4);
		seq.store(s + 2, std::memory_order_release);
	}

	bool Link::ReadMcState(skycraft::proto::McState& a_out) const
	{
		if (!base_) {
			return false;
		}
		auto* src = At<skycraft::proto::McState>(skycraft::proto::kOffMcState);
		auto  seq = Atomic(src->seq);
		for (int attempt = 0; attempt < 64; ++attempt) {
			const auto s1 = seq.load(std::memory_order_acquire);
			if (s1 & 1) {
				YieldProcessor();
				continue;
			}
			std::memcpy(&a_out, src, sizeof(skycraft::proto::McState));
			std::atomic_thread_fence(std::memory_order_acquire);
			if (seq.load(std::memory_order_relaxed) == s1) {
				return true;
			}
		}
		return false;
	}

	void Link::PushInput(skycraft::proto::InputType a_type, std::uint16_t a_code, std::int32_t a_a, std::int32_t a_b, std::int32_t a_c)
	{
		if (!base_) {
			return;
		}
		auto* ring = base_ + skycraft::proto::kOffInputRing;
		auto& headRef = *reinterpret_cast<std::uint64_t*>(ring + skycraft::proto::kInputRingHeadOff);
		auto& tailRef = *reinterpret_cast<std::uint64_t*>(ring + skycraft::proto::kInputRingTailOff);
		const auto head = Atomic(headRef).load(std::memory_order_relaxed);
		const auto tail = Atomic(tailRef).load(std::memory_order_acquire);
		if (head - tail >= skycraft::proto::kInputRingEntries) {
			return;
		}
		auto* entry = reinterpret_cast<skycraft::proto::InputEvent*>(ring + skycraft::proto::kInputRingDataOff) + (head & (skycraft::proto::kInputRingEntries - 1));
		*entry = { static_cast<std::uint16_t>(a_type), a_code, a_a, a_b, a_c };
		Atomic(headRef).store(head + 1, std::memory_order_release);
	}

	bool Link::WriteCollision(skycraft::proto::ColType a_type, const void* a_payload, std::uint32_t a_bytes)
	{
		if (!base_) {
			return false;
		}
		auto* ring = base_ + skycraft::proto::kOffCollisionRing;
		auto& headRef = *reinterpret_cast<std::uint64_t*>(ring + skycraft::proto::kColRingHeadOff);
		auto& tailRef = *reinterpret_cast<std::uint64_t*>(ring + skycraft::proto::kColRingTailOff);
		auto* data = ring + skycraft::proto::kColRingDataOff;
		constexpr auto size = skycraft::proto::kColRingDataBytes;

		const std::uint64_t msgBytes = (sizeof(skycraft::proto::ColMsgHeader) + a_bytes + 7) & ~7ull;
		if (msgBytes > size / 2) {
			return false;
		}
		auto       head = Atomic(headRef).load(std::memory_order_relaxed);
		const auto tail = Atomic(tailRef).load(std::memory_order_acquire);
		auto       pos = head % size;
		const auto padBytes = (pos + msgBytes > size) ? size - pos : 0;
		if (size - (head - tail) < msgBytes + padBytes) {
			return false;
		}
		if (padBytes) {
			*reinterpret_cast<skycraft::proto::ColMsgHeader*>(data + pos) = { skycraft::proto::kColPad, 0 };
			head += padBytes;
			pos = 0;
		}
		*reinterpret_cast<skycraft::proto::ColMsgHeader*>(data + pos) = { a_type, a_bytes };
		if (a_bytes && a_payload) {
			std::memcpy(data + pos + sizeof(skycraft::proto::ColMsgHeader), a_payload, a_bytes);
		}
		Atomic(headRef).store(head + msgBytes, std::memory_order_release);
		return true;
	}

	void Link::WriteActors(const skycraft::proto::ActorRecord* a_records, std::uint32_t a_count)
	{
		if (!base_) {
			return;
		}
		auto*      table = At<skycraft::proto::ActorTable>(skycraft::proto::kOffActorTable);
		auto       seq = Atomic(table->seq);
		const auto s = seq.load(std::memory_order_relaxed);
		seq.store(s + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		const auto count = a_count < skycraft::proto::kMaxActors ? a_count : skycraft::proto::kMaxActors;
		table->count = count;
		if (count && a_records) {
			std::memcpy(table->actors, a_records, sizeof(skycraft::proto::ActorRecord) * count);
		}
		seq.store(s + 2, std::memory_order_release);
	}

	bool Link::PopEvent(skycraft::proto::McEvent& a_out)
	{
		if (!base_) {
			return false;
		}
		auto*      ring = base_ + skycraft::proto::kOffEventRing;
		auto&      headRef = *reinterpret_cast<std::uint64_t*>(ring + skycraft::proto::kEventRingHeadOff);
		auto&      tailRef = *reinterpret_cast<std::uint64_t*>(ring + skycraft::proto::kEventRingTailOff);
		const auto head = Atomic(headRef).load(std::memory_order_acquire);
		auto       tail = Atomic(tailRef).load(std::memory_order_relaxed);
		if (tail >= head) {
			return false;
		}
		if (head - tail > skycraft::proto::kEventRingEntries) {
			tail = head - skycraft::proto::kEventRingEntries;
		}
		a_out = reinterpret_cast<const skycraft::proto::McEvent*>(ring + skycraft::proto::kEventRingDataOff)[tail & (skycraft::proto::kEventRingEntries - 1)];
		Atomic(tailRef).store(tail + 1, std::memory_order_release);
		return true;
	}

	void Link::DrainRender(const std::function<void(std::uint32_t, const std::uint8_t*, std::uint32_t)>& a_fn, std::uint64_t a_maxBytes)
	{
		if (!base_) {
			return;
		}
		auto*      ring = base_ + skycraft::proto::kOffRenderRing;
		auto&      headRef = *reinterpret_cast<std::uint64_t*>(ring + skycraft::proto::kRenRingHeadOff);
		auto&      tailRef = *reinterpret_cast<std::uint64_t*>(ring + skycraft::proto::kRenRingTailOff);
		const auto head = Atomic(headRef).load(std::memory_order_acquire);
		auto       tail = Atomic(tailRef).load(std::memory_order_relaxed);
		auto*      data = ring + skycraft::proto::kRenRingDataOff;
		constexpr auto size = skycraft::proto::kRenRingDataBytes;
		std::uint64_t  done = 0;
		while (tail < head && done < a_maxBytes) {
			const auto pos = tail % size;
			if (pos + sizeof(skycraft::proto::ColMsgHeader) > size) {
				tail += size - pos;
				continue;
			}
			const auto* hdr = reinterpret_cast<const skycraft::proto::ColMsgHeader*>(data + pos);
			if (hdr->type == skycraft::proto::kRenPad) {
				tail += size - pos;
				continue;
			}
			if (hdr->payloadBytes > size || pos + sizeof(skycraft::proto::ColMsgHeader) + static_cast<std::uint64_t>(hdr->payloadBytes) > size) {
				tail = head;
				break;
			}
			a_fn(hdr->type, data + pos + sizeof(skycraft::proto::ColMsgHeader), hdr->payloadBytes);
			const auto msgBytes = (sizeof(skycraft::proto::ColMsgHeader) + static_cast<std::uint64_t>(hdr->payloadBytes) + 7ull) & ~7ull;
			tail += msgBytes;
			done += msgBytes;
		}
		Atomic(tailRef).store(tail, std::memory_order_release);
	}

	bool Link::AcquireOverlayFrame()
	{
		if (!base_) {
			return false;
		}
		auto& state = At<skycraft::proto::OverlayCtl>(skycraft::proto::kOffOverlayCtl)->state;
		if (!(Atomic(state).load(std::memory_order_acquire) & skycraft::proto::kOverlayDirty)) {
			return false;
		}
		const auto old = Atomic(state).exchange(overlayFront_, std::memory_order_acq_rel);
		overlayFront_ = old & 3;
		return true;
	}

	const std::uint8_t* Link::FrontPixels() const
	{
		return base_ + skycraft::proto::kOffOverlayPixels + skycraft::proto::kOverlaySlotBytes * overlayFront_;
	}

	const skycraft::proto::OverlaySlotHdr* Link::FrontHeader() const
	{
		return At<skycraft::proto::OverlaySlotHdr>(skycraft::proto::kOffOverlaySlotHdr + sizeof(skycraft::proto::OverlaySlotHdr) * overlayFront_);
	}
}
