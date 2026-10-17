#include "ScriptBridgeNative.h"
#include "Link.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <immintrin.h>
#include <mutex>

namespace witchcraft::script {
namespace {
// ClampF's script thunk (registered by name at RVA 0x27b4743) and the opcode
// table its parameter evaluation indexes (lea at thunk + 0x0c).
constexpr std::uintptr_t clampThunk=0x27b2ab0,opcodeTable=0x5a75a70;
constexpr std::uint8_t clampPrologue[]{0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0x42,0x30,
    0x48,0x8d,0x35,0xad,0x2f,0x2c,0x03,0xc5,0xf8,0x29,0x74,0x24,0x20,0xc5,0xc8,0x57,0xf6,
    0xc5,0xfa,0x11,0x74,0x24,0x68,0x0f,0xb6,0x08};
using Evaluate=void(*)(void*,void*,void*);
std::uintptr_t image=0;
std::atomic<bool> installed=false,playActive=false;
std::mutex hurtLock;
std::vector<Hurt> pendingHurts;
Bridge bridge; // Game (script) thread only.
void(*logger)(const char*)=nullptr;

void Log(const char* text) {if(logger) logger(text);}
// Reads one script parameter exactly as the original thunk does.
void Parameter(void* frame,float& out) {
    auto& code=*reinterpret_cast<std::uint8_t**>(static_cast<char*>(frame)+0x30);
    const std::uint8_t op=*code;++code;
    const auto evaluate=reinterpret_cast<Evaluate*>(image+opcodeTable)[op];
    if(evaluate) evaluate(*static_cast<void**>(frame),frame,&out);
}
float HandleMessage(float value,int op) {
    using namespace skycraft::proto;
    if(op==kOpNextHit) {
        McEvent event{};
        while(Link::Get().PopEvent(event))
            if(event.type==kEvHitActor) bridge.QueueHit(event.formId,event.a);
    }
    bridge.playActive=playActive.load();
    bool published=false;
    const float reply=bridge.Message(value,op,published);
    if(published) {
        const auto& actors=bridge.Actors();
        Link::Get().WriteActors(actors.data(),static_cast<std::uint32_t>(actors.size()));
        static bool announced=false;
        if(!announced) {Log("script bridge: companion script mod connected; actor table published");announced=true;}
    }
    auto hurts=bridge.TakeHurts();
    if(!hurts.empty()) {
        std::lock_guard lock(hurtLock);
        for(const auto& hurt:hurts) if(pendingHurts.size()<128) pendingHurts.push_back(hurt);
    }
    return reply;
}
// Replacement for the ClampF thunk: identical evaluation and min/max order.
void ClampF(void*,void* frame,float* result) {
    float v=0,lo=0,hi=0;
    Parameter(frame,v);Parameter(frame,lo);Parameter(frame,hi);
    ++*reinterpret_cast<std::uint8_t**>(static_cast<char*>(frame)+0x30);
    float clamped=_mm_cvtss_f32(_mm_min_ss(_mm_max_ss(_mm_set_ss(lo),_mm_set_ss(v)),_mm_set_ss(hi)));
    if(lo==kScriptMagic&&std::isfinite(hi)) clamped=HandleMessage(v,static_cast<int>(hi));
    if(result) *result=clamped;
}
}
bool InstallScriptBridge(std::uintptr_t base,void(*log)(const char*)) {
    if(installed) return true;
    logger=log;
    auto* target=reinterpret_cast<std::uint8_t*>(base+clampThunk);
    std::uint8_t current[sizeof(clampPrologue)]{};SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),target,current,sizeof(current),&read)||read!=sizeof(current)||
        std::memcmp(current,clampPrologue,sizeof(current))) {
        Log("script bridge: ClampF thunk signature mismatch; damage and NPC bridge disabled");
        installed=true;return false; // Never retry a mismatched build.
    }
    image=base;
    std::uint8_t patch[12]{0x48,0xb8};
    const auto destination=reinterpret_cast<std::uint64_t>(&ClampF);
    std::memcpy(patch+2,&destination,8);patch[10]=0xff;patch[11]=0xe0; // mov rax, imm64; jmp rax
    DWORD protection=0;
    if(!VirtualProtect(target,sizeof(patch),PAGE_EXECUTE_READWRITE,&protection)) {Log("script bridge: thunk protection change failed");installed=true;return false;}
    std::memcpy(target,patch,sizeof(patch));
    DWORD ignored=0;VirtualProtect(target,sizeof(patch),protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),target,sizeof(patch));
    installed=true;
    Log("script bridge: verified ClampF thunk replaced; waiting for the companion script mod");
    return true;
}
void SetScriptPlayActive(bool active) {playActive=active;}
std::vector<Hurt> TakeScriptHurts() {
    std::lock_guard lock(hurtLock);
    auto out=std::move(pendingHurts);pendingHurts.clear();return out;
}
}
