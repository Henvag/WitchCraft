#include "NativeCamera.h"
#include "GameBridge.h"
#include "Link.h"
#include "MotionTimeline.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
namespace witchcraft::native {
namespace {
using GetData=bool(*)(void*,void*);
GetData original=nullptr;
std::uintptr_t image=0;
std::atomic<bool> ready=false;
void(*logger)(const char*)=nullptr;
bool ReadMemory(void*,std::uintptr_t address,void* out,std::size_t bytes) {
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,bytes,&got)&&got==bytes;
}
bool CameraData(void* camera,void* data) {
    const bool ok=original(camera,data);
    if(!ok) return ok;
    const auto command=ReadPilotCommand();
    const auto status=ReadPilotStatus();
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    if(!command.active||!command.cameraOwned||!status.hidden||status.failed||
        foreground!=GetCurrentProcessId()||GetTickCount64()-command.publishedAt>150||!Link::Get().McAlive()) return ok;
    Reader reader(image,ReadMemory,nullptr);Sample sample{};GameFrame frame{};
    if(!reader.Snapshot(sample,false)||sample.paused||!ReadGameFrame(frame)||
        frame.worldId!=command.world||frame.collisionEpoch!=command.epoch||frame.relocation!=command.relocation) return ok;
    std::uint32_t count=0;reader.Read(sample.cameraObject+0x60,count);
    const auto stack=reader.Pointer(sample.cameraObject+0x58);
    if(!count||count>16||!stack||reader.Pointer(stack+(count-1)*40)!=reinterpret_cast<std::uintptr_t>(camera)) return ok;
    auto mc=command.followState;skycraft::proto::McState latest{};
    if(Link::Get().ReadMcState(latest)&&latest.teleportAck==mc.teleportAck&&latest.frameCounter>=mc.frameCounter) mc=latest;
    if(!(mc.flags&skycraft::proto::kMcInWorld)||(mc.flags&skycraft::proto::kMcDead)) return ok;
    LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    const auto pose=RenderMotionState(mc,now.QuadPart,frequency.QuadPart);
    auto* values=static_cast<float*>(data);float position[3]{};
    if(CameraPosition(pose,sample.feet,values,position)) {
        for(int i=0;i<3;++i) values[i]=position[i];
        if(mc.cameraMode==0) {values[0x34/4]=.05f;static_cast<unsigned char*>(data)[0x3c]=1;}
        static ULONGLONG lastLog=0;static unsigned applied=0;
        ++applied;
        if(logger&&GetTickCount64()-lastLog>=5000) {
            char line[200]{};std::snprintf(line,sizeof(line),"native camera: applied=%u mode=%u elevation=%.3f requested=(%.3f,%.3f,%.3f)",
                applied,mc.cameraMode,pose.y-sample.feet[2],position[0],position[1],position[2]);logger(line);
            applied=0;lastLog=GetTickCount64();
        }
    }
    return ok;
}
}
bool CameraReady() {return ready.load();}
void PrepareCamera(const Reader& reader,std::uintptr_t base,const Sample* sample,void(*log)(const char*)) {
    if(ready||!sample) return;
    std::uint32_t count=0;
    if(!reader.Read(sample->cameraObject+0x60,count)||!count||count>16) return;
    const auto stack=reader.Pointer(sample->cameraObject+0x58);
    const auto camera=stack?reader.Pointer(stack+(count-1)*40):0;
    constexpr std::uintptr_t table=0x3810198,method=0x1e0f290;
    if(!camera||reader.Pointer(camera)!=base+table||reader.Pointer(base+table+0x10)!=base+method) return;
    const unsigned char expected[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20};
    unsigned char bytes[sizeof(expected)]{};
    if(!reader.Read(base+method,bytes)||std::memcmp(bytes,expected,sizeof(expected))) return;
    auto* slot=reinterpret_cast<void**>(base+table+0x10);DWORD protection=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protection)) return;
    image=base;logger=log;original=reinterpret_cast<GetData>(base+method);
    const bool installed=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&CameraData),
        reinterpret_cast<void*>(base+method))==reinterpret_cast<void*>(base+method);
    DWORD ignored=0;VirtualProtect(slot,sizeof(void*),protection,&ignored);
    if(installed) {ready=true;log("native camera: verified custom-camera data hook installed; first person and creative altitude available");}
}
}
