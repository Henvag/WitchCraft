#include "NativeLayout.h"
#include "NativeJump.h"
#include "NativeGravity.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>

using namespace witchcraft::native;
constexpr std::uintptr_t base = 0x140000000, table = 0x200000000,
    game = table + 0x1000, player = table + 0x2000, camera = table + 0x3000;
std::map<std::uintptr_t, std::uint8_t> memory;
void Check(bool value) { if (!value) { puts("native layout check failed"); std::exit(1); } }
void Bytes(std::uintptr_t p, const void* src, std::size_t n) {
    auto bytes = static_cast<const std::uint8_t*>(src);
    for (std::size_t i = 0; i < n; ++i) memory[p + i] = bytes[i];
}
template<class T> void Put(std::uintptr_t p, const T& value) { Bytes(p, &value, sizeof(value)); }
bool Read(void*, std::uintptr_t p, void* out, std::size_t n) {
    auto bytes = static_cast<std::uint8_t*>(out);
    for (std::size_t i = 0; i < n; ++i) {
        auto found = memory.find(p + i);
        if (found == memory.end()) return false;
        bytes[i] = found->second;
    }
    return true;
}
bool failWrites=false;
unsigned writes=0;
bool Write(std::uintptr_t p,const void* data,std::size_t n) {
    ++writes;if(failWrites) return false;Bytes(p,data,n);return true;
}
int main() {
    Reader reader(base, Read, nullptr);
    std::uintptr_t jumpComponent=123;
    Check(!JumpController(reader,base,player,jumpComponent)&&!jumpComponent);
    const std::uintptr_t component=table+0x10000,manager=table+0x20000,primary=table+0x30000;
    Put(player+0x218,component);Put(component+0x10a0,manager);Put(manager+0x20,primary);
    Put(component,base+0x383a100);Put(primary,base+0x388c968);
    Put(base+0x383a100+0x638,base+0x2024780);Put(base+0x388c968+0x30,base+0x207e4a0);
    for(const auto& s:jumpSignatures) Bytes(base+s.rva,s.bytes.data(),s.count);
    Check(JumpController(reader,base,player,jumpComponent)&&jumpComponent==component);
    Put(primary,base+0x388c960);Check(!JumpController(reader,base,player,jumpComponent)&&!jumpComponent);
    Put(primary,base+0x388c968);
    memory[base+jumpQueueRva+0x4b]^=1;Check(!JumpController(reader,base,player,jumpComponent));
    memory[base+jumpQueueRva+0x4b]^=1;
    Put(manager+0x20,std::uintptr_t{0});Check(!JumpController(reader,base,player,jumpComponent));
    Put(manager+0x20,primary);
    Put(player,base+playerVtable);
    const std::uintptr_t physical=table+0x40000,wrapper=table+0x50000;
    Put(primary+0x10,physical);Put(component+0x16d8,wrapper);Put(wrapper+0x10,physical);
    Put(physical,base+0x39b32f0);
    const std::uint8_t gravityCode[]{0x0f,0xb6,0x81,0xc4,0x02,0,0,0x41,0x80,0xe1,0x01,0x24,0xef};
    Bytes(base+0x1fe1531,gravityCode,sizeof(gravityCode));
    Put(physical+0x2c4,std::uint8_t{0x71});
    JumpGravity gravity;
    Check(gravity.Acquire(reader,base,player,Write)&&gravity.Owned());
    std::uint8_t gf=0;Check(reader.Read(physical+0x2c4,gf)&&gf==0x61);
    const auto priorWrites=writes;Check(gravity.Acquire(reader,base,player,Write)&&writes==priorWrites);
    Put(physical+0x2c4,std::uint8_t{0x65}); // Another engine flag changes during ownership.
    failWrites=true;Check(!gravity.Restore(reader,base,Write)&&gravity.Owned());failWrites=false;
    Check(gravity.Restore(reader,base,Write)&&!gravity.Owned());
    Check(reader.Read(physical+0x2c4,gf)&&gf==0x75); // Restore only gravity; preserve new flag.
    Put(physical+0x2c4,std::uint8_t{0x65});
    Check(!gravity.Acquire(reader,base,player,Write)&&!gravity.Owned()); // Game already disabled it.
    Put(physical+0x2c4,std::uint8_t{0x75});Check(gravity.Acquire(reader,base,player,Write));
    Put(physical,base+0x39b32f8);const auto beforeStale=writes;
    Check(!gravity.Restore(reader,base,Write)&&writes==beforeStale&&gravity.Owned());
    Put(physical,base+0x39b32f0);Check(gravity.Restore(reader,base,Write));
    Check(!reader.ValidateCode());
    for (const auto& s : signatures) Bytes(base + s.rva, s.bytes.data(), s.count);
    Check(reader.ValidateCode());
    memory[base + advanceThreads] ^= 1;
    Check(!reader.ValidateCode()); // Different build or another mod's hook.
    memory[base + advanceThreads] ^= 1;
    Sample sample{};
    Check(!reader.Snapshot(sample));
    Put(base + globals, table);
    const std::uintptr_t objects[]{game, player, camera};
    for (int i = 0; i < 3; ++i) {
        auto handle = table + 0x5000 + 0x20 * i;
        Put(table + 0x20 + 8 * i, handle); Put(handle + 8, objects[i]);
    }
    Put(game, base + gameVtable); Put(player, base + playerVtable);
    Put(camera, base + cameraVtable);
    auto worldHandle = table + 0x6000;
    Put(game + 0xf0, worldHandle); Put(worldHandle + 8, table + 0x7000);
    Put(game + 0x160, std::uint32_t{0});
    float feet[]{1070,1795,1.6f}, eye[]{1069,1793,3.5f}, forward[]{0,1,0};
    Put(player + 0xa0, feet); Put(camera + 0x70, eye);
    Put(camera + 0x790, forward); Put(camera + 0xa0, 67.0f);
    Check(reader.Snapshot(sample) && !sample.paused && sample.feet[0] == 1070);
    const auto complete=sample;
    Put(camera + 0xa0, 0.0f); // Director clears the lens before invoking GetData.
    Check(!reader.Snapshot(sample));
    Check(reader.Snapshot(sample,false)&&sample.player==player&&sample.world==table+0x7000);
    sample.feet[0]+=1;sample.paused=true;
    Check(ReuseCamera(sample,complete,100)&&sample.feet[0]==1071&&sample.paused&&sample.rawFov==67);
    Check(!ReuseCamera(sample,complete,251));
    ++sample.world;Check(!ReuseCamera(sample,complete,10));sample=complete;
    ++sample.player;Check(!ReuseCamera(sample,complete,10));sample=complete;
    ++sample.cameraObject;Check(!ReuseCamera(sample,complete,10));
    Put(player, base + playerVtable + 8);
    Check(!reader.Snapshot(sample,false)); // Stage-specific read still checks identity.
    Put(player, base + playerVtable);Put(camera + 0xa0, 67.0f);
    Check(reader.Snapshot(sample));
    double converted[3]{}; ToMinecraft(sample.feet, converted);
    Check(converted[0] == feet[0] && converted[1] == feet[2] && converted[2] == -feet[1]);
    Put(game + 0x160, std::uint32_t{1});
    Check(reader.Snapshot(sample) && sample.paused);
    Put(player, base + playerVtable + 8);
    Check(!reader.Snapshot(sample)); // Stale or incompatible object.
    Put(player, base + playerVtable);
    Put(camera + 0x790, std::numeric_limits<float>::quiet_NaN());
    Check(!reader.Snapshot(sample));
    Put(camera + 0x790, forward);
    Put(player + 0xa0, 1.0e30f);
    Check(!reader.Snapshot(sample)); // Corrupt finite coordinates cannot become integer regions.
    Put(player + 0xa0, feet);
    Put(worldHandle + 8, std::uintptr_t{0});
    Check(!reader.Snapshot(sample)); // Unloaded world never becomes a valid frame.
    puts("native layout rejection and coordinate checks passed");
}
