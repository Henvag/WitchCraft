#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

// Deliberately restricted to the user's mapped DX12 executable. These RVAs
// include the map's 0x1000 code-section offset. Never reuse for another build.
namespace witchcraft::native {
inline constexpr std::uint32_t timestamp = 0x6ac555a3;
inline constexpr std::uint32_t imageSize = 0x06402000;
inline constexpr std::uintptr_t advanceThreads = 0x270b150;
inline constexpr std::uintptr_t globals = 0x5a7da70;
inline constexpr std::uintptr_t gameVtable = 0x37b41d0;
inline constexpr std::uintptr_t playerVtable = 0x381e630;
inline constexpr std::uintptr_t cameraVtable = 0x3810308;

struct Signature {
    std::uintptr_t rva;
    std::array<std::uint8_t, 32> bytes;
    std::size_t count;
};
inline constexpr Signature signatures[]{
    {advanceThreads, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20}, 20},
    {0x270f2c0, {0x48,0x8b,0x05,0xa9,0xe7,0x36,0x03,0x48,0x63,0xca}, 10},
    {0x2267e50, {0x48,0xff,0x42,0x30,0xc5,0xfa,0x10,0x81,0xa0,0,0,0,
        0xc5,0xfa,0x10,0x89,0xa4,0,0,0,0xc5,0xfa,0x10,0x91,0xa8,0,0,0}, 28},
    {0x2382890, {0x48,0xff,0x42,0x30,0xc5,0xfa,0x10,0x41,0x70,
        0xc5,0xfa,0x10,0x49,0x74,0xc5,0xfa,0x10,0x51,0x78}, 19},
    {0x23826f0, {0x48,0xff,0x42,0x30,0xc5,0xfa,0x10,0x81,0x90,0x07,0,0,
        0xc5,0xfa,0x10,0x89,0x94,0x07,0,0}, 20},
    {0x23826d0, {0x48,0xff,0x42,0x30,0x4d,0x85,0xc0,0x74,0x09,
        0x8b,0x81,0xa0,0,0,0,0x41,0x89,0,0xc3}, 19},
    {0x227a2d0, {0x48,0xff,0x42,0x30,0x4d,0x85,0xc0,0x74,0x0d,
        0x83,0xb9,0x60,0x01,0,0,0,0x0f,0x95,0xc0,0x41,0x88,0,0xc3}, 23}
};

struct Sample {
    std::uintptr_t player = 0, world = 0, cameraObject = 0;
    float feet[3]{}, camera[3]{}, forward[3]{};
    float rawFov = 0;
    bool paused = false;
};

// The memory provider must fail on inaccessible memory, rather than dereference
// stale engine objects. Used both in-process and by the read-only external probe.
class Reader {
public:
    using ReadFn = bool (*)(void*, std::uintptr_t, void*, std::size_t);
    Reader(std::uintptr_t base, ReadFn read, void* context)
        : base_(base), read_(read), context_(context) {}
    template<class T> bool Read(std::uintptr_t address, T& value) const {
        return address && read_ && read_(context_, address, &value, sizeof(value));
    }
    std::uintptr_t Pointer(std::uintptr_t address) const {
        std::uintptr_t value = 0;
        Read(address, value);
        return value;
    }
    bool ValidateCode() const {
        for (const auto& s : signatures) {
            std::array<std::uint8_t, 32> buffer{};
            if (!read_ || !read_(context_, base_ + s.rva, buffer.data(), s.count) ||
                std::memcmp(buffer.data(), s.bytes.data(), s.count)) return false;
        }
        return true;
    }
    bool Snapshot(Sample& out, bool cameraComplete = true) const {
        Sample sample{};
        const auto table = Pointer(base_ + globals);
        if (!table) return false;
        const auto game = Global(table, 0);
        sample.player = Global(table, 1);
        sample.cameraObject = Global(table, 2);
        if (!game || !sample.player || !sample.cameraObject ||
            Pointer(game) != base_ + gameVtable ||
            Pointer(sample.player) != base_ + playerVtable ||
            Pointer(sample.cameraObject) != base_ + cameraVtable) return false;
        const auto worldHandle = Pointer(game + 0xf0);
        sample.world = worldHandle ? Pointer(worldHandle + 8) : 0;
        if (!sample.world) return false;
        std::uint32_t pauseCount = 0;
        if (!Read(game + 0x160, pauseCount) ||
            !Read(sample.player + 0xa0, sample.feet)) return false;
        for(float v:sample.feet) if(!std::isfinite(v)||std::abs(v)>1000000) return false;
        sample.paused = pauseCount != 0;
        // GetData runs after the director clears its output pose/lens and
        // before rebuilding them. Validate actor/world identity independently
        // of the incomplete director output at this camera stage.
        if(!cameraComplete) {out=sample;return true;}
        if (!Read(sample.cameraObject + 0x70, sample.camera) ||
            !Read(sample.cameraObject + 0x790, sample.forward) ||
            !Read(sample.cameraObject + 0xa0, sample.rawFov)) return false;
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(sample.feet[i]) || !std::isfinite(sample.camera[i]) ||
                !std::isfinite(sample.forward[i]) || std::abs(sample.feet[i]) > 1000000 ||
                std::abs(sample.camera[i]) > 1000000) return false;
        const auto length = std::hypot(sample.forward[0], sample.forward[1], sample.forward[2]);
        if (length < 0.9 || length > 1.1 || !std::isfinite(sample.rawFov) ||
            sample.rawFov <= 20 || sample.rawFov >= 140) return false;
        sample.paused = pauseCount != 0;
        out = sample;
        return true;
    }
private:
    std::uintptr_t Global(std::uintptr_t table, int index) const {
        const auto handle = Pointer(table + 0x20 + 8 * index);
        return handle ? Pointer(handle + 8) : 0;
    }
    std::uintptr_t base_;
    ReadFn read_;
    void* context_;
};

// The director briefly clears its output while assembling a camera frame.
// Reuse only the lens/pose, never stale actor or world identity, during that gap.
inline bool ReuseCamera(Sample& current,const Sample& previous,std::uint64_t ageMs) {
    if(ageMs>250||!current.player||current.player!=previous.player||
        current.world!=previous.world||current.cameraObject!=previous.cameraObject) return false;
    std::copy_n(previous.camera,3,current.camera);
    std::copy_n(previous.forward,3,current.forward);current.rawFov=previous.rawFov;
    return true;
}
template<class T, class U> void ToMinecraft(const T (&src)[3], U (&dst)[3]) {
    dst[0] = src[0]; dst[1] = src[2]; dst[2] = -src[1];
}
}
