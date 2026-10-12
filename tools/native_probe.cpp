#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "../host/NativeLayout.h"
#include "../host/NativeJump.h"
#include "../host/NativeGravity.h"
#include "../protocol/skycraft_protocol.h"
#include <atomic>
#include <cstdlib>
#include "../host/RenderTiming.h"

int SampleFollow(const witchcraft::native::Reader& reader,std::uintptr_t base,unsigned seconds,const char* path) {
    using namespace skycraft::proto;
    if(!seconds||seconds>120) return 6;
    auto mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,kMappingName);
    const auto* data=mapping?static_cast<const std::uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0x400)):nullptr;
    if(!data) {if(mapping) CloseHandle(mapping);return 6;}
    Header header{};std::memcpy(&header,data,sizeof(header));
    if(header.magic!=kMagic||header.version!=kVersion) {UnmapViewOfFile(data);CloseHandle(mapping);return 6;}
    auto* file=std::fopen(path,"w");
    if(!file) {UnmapViewOfFile(data);CloseHandle(mapping);return 6;}
    std::fprintf(file,"seconds,paused,skyFlags,mcFlags,mcFrame,mcTickAgeMs,feetX,feetY,feetZ,cameraX,cameraY,cameraZ,forwardX,forwardY,forwardZ,mcX,mcY,mcZ,mcCurX,mcCurY,mcCurZ,cameraPivotX,cameraPivotY,cameraPivotZ,cameraOffsetX,cameraOffsetY,cameraOffsetZ,cameraStagesValid\n");
    LARGE_INTEGER start{},clock{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    unsigned rows=0;
    for(;;) {
        QueryPerformanceCounter(&clock);
        const double elapsed=static_cast<double>(clock.QuadPart-start.QuadPart)/frequency.QuadPart;
        if(elapsed>=seconds) break;
        witchcraft::native::Sample sample{};McState mc{};SkyState sky{};bool haveMc=false,haveSky=false;
        if(reader.Snapshot(sample)) {
            for(int attempt=0;attempt<8;++attempt) {
                const auto* sequence=reinterpret_cast<const volatile std::uint32_t*>(data+kOffMcState);
                const auto before=*sequence;if(before&1) continue;
                std::atomic_thread_fence(std::memory_order_acquire);std::memcpy(&mc,data+kOffMcState,sizeof(mc));
                std::atomic_thread_fence(std::memory_order_acquire);
                if(before==*sequence) {haveMc=true;break;}
            }
            for(int attempt=0;attempt<8;++attempt) {
                const auto* sequence=reinterpret_cast<const volatile std::uint32_t*>(data+kOffSkyState);
                const auto before=*sequence;if(before&1) continue;
                std::atomic_thread_fence(std::memory_order_acquire);std::memcpy(&sky,data+kOffSkyState,sizeof(sky));
                std::atomic_thread_fence(std::memory_order_acquire);
                if(before==*sequence) {haveSky=true;break;}
            }
            if(haveMc&&haveSky) {
                const auto visual=witchcraft::RenderMcState(mc,clock.QuadPart,frequency.QuadPart);
                float pivot[4]{},offset[4]{};unsigned count=0;bool stagesValid=false;
                if(reader.Read(sample.cameraObject+0x60,count)&&count>0&&count<=64) {
                    const auto list=reader.Pointer(sample.cameraObject+0x58);
                    const auto record=list?list+(count-1)*40ull:0;
                    const auto interface=reader.Pointer(record);
                    const auto handle=reader.Pointer(record?record+8:0);
                    const auto camera=reader.Pointer(handle?handle+8:0);
                    // This probe never invokes engine methods. Match both ICamera
                    // entry points before interpreting CCustomCamera fields.
                    const auto vt=reader.Pointer(interface);
                    stagesValid=camera&&interface==camera+0x170&&
                        vt==base+0x3810198&&reader.Pointer(vt+8)==base+0x1e0f710&&
                        reader.Pointer(vt+0x10)==base+0x1e0f290&&
                        reader.Read(camera+0x1d0,pivot)&&reader.Read(camera+0x230,offset);
                    for(int i=0;i<4&&stagesValid;++i)
                        stagesValid=std::isfinite(pivot[i])&&std::isfinite(offset[i])&&
                            std::abs(pivot[i])<=1000000&&std::abs(offset[i])<=100;
                }
                std::fprintf(file,"%.6f,%u,%u,%u,%llu,%.3f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u\n",
                    elapsed,sample.paused?1u:0u,sky.flags,mc.flags,static_cast<unsigned long long>(mc.frameCounter),
                    (clock.QuadPart-mc.tickQpc)*1000.0/frequency.QuadPart,
                    sample.feet[0],sample.feet[2],-sample.feet[1],sample.camera[0],sample.camera[2],-sample.camera[1],
                    sample.forward[0],sample.forward[2],-sample.forward[1],visual.x,visual.y,visual.z,mc.curX,mc.curY,mc.curZ,
                    pivot[0],pivot[2],-pivot[1],offset[0],offset[2],-offset[1],stagesValid?1u:0u);
                ++rows;
            }
        }
        Sleep(8);
    }
    std::fclose(file);UnmapViewOfFile(data);CloseHandle(mapping);
    std::printf("Read-only follow capture: %u samples over %u seconds\n",rows,seconds);
    return rows?0:7;
}

void PrintLink() {
    using namespace skycraft::proto;
    auto mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,kMappingName);
    if (!mapping) { puts("LINK: shared mapping unavailable"); return; }
    const auto* data=static_cast<const std::uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,kOffCollisionRing+0x80));
    if (!data) { CloseHandle(mapping); return; }
    Header header{}; std::memcpy(&header,data,sizeof(header));
    if (header.magic==kMagic&&header.version==kVersion) {
        const auto tail=*reinterpret_cast<const volatile std::uint64_t*>(data+kOffCollisionRing+kColRingTailOff);
        const auto head=*reinterpret_cast<const volatile std::uint64_t*>(data+kOffCollisionRing+kColRingHeadOff);
        printf("LINK: hostPid=%u mcPid=%u hostAge=%llu mcAge=%llu collisionWritten=%llu consumed=%llu pending=%llu\n",
            header.skyrimPid,header.mcPid,static_cast<unsigned long long>(GetTickCount64()-header.skyrimHeartbeatMs),
            static_cast<unsigned long long>(GetTickCount64()-header.mcHeartbeatMs),
            static_cast<unsigned long long>(head),static_cast<unsigned long long>(tail),static_cast<unsigned long long>(head-tail));
        for (int attempt=0;attempt<8;++attempt) {
            const auto* seq=reinterpret_cast<const volatile std::uint32_t*>(data+kOffSkyState);
            const auto before=*seq;
            if (before&1) continue;
            std::atomic_thread_fence(std::memory_order_acquire);
            SkyState sky{}; std::memcpy(&sky,data+kOffSkyState,sizeof(sky));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (before!=*seq) continue;
            printf("LINK: world=%u epoch=%u skyFlags=%u\n",sky.worldId,sky.collisionEpoch,sky.flags);
            break;
        }
    }
    UnmapViewOfFile(data); CloseHandle(mapping);
}

bool ReadRemote(void* process, std::uintptr_t address, void* out, std::size_t bytes) {
    SIZE_T received = 0;
    return address && ReadProcessMemory(static_cast<HANDLE>(process), reinterpret_cast<void*>(address), out, bytes, &received) && received == bytes;
}

class Reader {
public:
    HANDLE process = nullptr;
    std::uintptr_t base = 0;
    std::uintptr_t names = 0;
    template<class T> bool read(std::uintptr_t p, T& value) const {
        SIZE_T size = 0;
        return p && ReadProcessMemory(process, reinterpret_cast<void*>(p), &value, sizeof(value), &size) && size == sizeof(value);
    }
    std::uintptr_t pointer(std::uintptr_t p) const {
        std::uintptr_t v = 0; read(p, v); return v;
    }
    std::string type(std::uintptr_t object) const {
        auto vtable = pointer(object);
        auto locator = pointer(vtable - sizeof(void*));
        std::uint32_t typeRva = 0;
        if (!read(locator + 12, typeRva)) return "?";
        char name[128]{};
        if (!read(base + typeRva + 16, name)) return "?";
        name[127] = 0;
        return name;
    }
    std::string name(std::uint32_t index) const {
        if (index > 300000) return "?";
        auto table = pointer(names + 0x11830);
        auto holder = pointer(table + index * 8ull);
        auto text = pointer(holder);
        char buffer[128]{};
        if (!read(text, buffer)) return "?";
        buffer[127] = 0;
        return buffer;
    }
    void functions(std::uintptr_t object,bool all=false) const {
        auto klass = pointer(object + 0x18);
        for (int depth = 0; klass && depth < 16; ++depth) {
            std::uint32_t id = 0, count = 0;
            read(klass + 0x2c, id); read(klass + 0x50, count);
            printf(" class %s functions %u\n", name(id).c_str(), count);
            auto list = pointer(klass + 0x48);
            if (count > 3000) break;
            for (unsigned i = 0; i < count; ++i) {
                auto fn = pointer(list + i * 8ull);
                read(fn + 0x10, id);
                const auto text = name(id);
                if (!all&&text.find("WorldPosition") == std::string::npos && text.find("Teleport") == std::string::npos &&
                    text.find("CameraPosition") == std::string::npos && text.find("CameraDirection") == std::string::npos &&
                    text.find("Fov") == std::string::npos && text.find("Paused") == std::string::npos &&
                    text.find("World") == std::string::npos && text.find("HideInGame") == std::string::npos &&
                    text.find("Trace") == std::string::npos && text.find("Physics") == std::string::npos &&
                    text.find("Collision") == std::string::npos && text.find("Gravity") == std::string::npos &&
                    text.find("Movement") == std::string::npos && text.find("Ground") == std::string::npos &&
                    text.find("Snap") == std::string::npos && text.find("Physical") == std::string::npos &&
                    text.find("Kinematic") == std::string::npos && text.find("Water") == std::string::npos &&
                    text.find("Swim") == std::string::npos) continue;
                std::uint32_t flags = 0; read(fn + 0x18, flags);
                std::uint32_t nativeIndex = 0; read(fn + 0xe0, nativeIndex);
                if (flags & 1) printf("   native %s rva=%llx\n", text.c_str(),
                    static_cast<unsigned long long>(pointer(base + ((flags & 2) ? 0x5a75a70 : 0x5a5da70) + nativeIndex * ((flags & 2) ? 8ull : 24ull)) - base));
                printf("  function %s object=%llx flags=%08x e0=%llx e8=%llx f0=%llx\n", text.c_str(),
                    static_cast<unsigned long long>(fn), flags,
                    static_cast<unsigned long long>(pointer(fn + 0xe0)),
                    static_cast<unsigned long long>(pointer(fn + 0xe8) - base),
                    static_cast<unsigned long long>(pointer(fn + 0xf0) - base));
            }
            klass = pointer(klass + 0x10);
        }
    }
};
int main(int argc, char** argv) {
    if (argc==2&&std::strcmp(argv[1],"--link")==0) { PrintLink(); return 0; }
    const bool diagnose = argc == 2 && std::strcmp(argv[1], "--diagnose") == 0;
    const bool capture=argc==4&&std::strcmp(argv[1],"--sample-follow")==0;
    if (diagnose) PrintLink();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    DWORD id = 0;
    if (Process32FirstW(snapshot, &entry)) do {
        if (!_wcsicmp(entry.szExeFile, L"witcher3.exe")) { id = entry.th32ProcessID; break; }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (!id) { puts("Witcher is not running"); return 1; }
    Reader reader;
    reader.process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!reader.process) { printf("OpenProcess failed: %lu\n", GetLastError()); return 2; }
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, id);
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module)) reader.base = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
    CloseHandle(snapshot);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!reader.read(reader.base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !reader.read(reader.base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE) return 3;
    printf("PID %lu image %llx timestamp %08lx size %08lx\n", id,
        static_cast<unsigned long long>(reader.base), nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage);
    witchcraft::native::Reader layout(reader.base, ReadRemote, reader.process);
    if (nt.FileHeader.TimeDateStamp != witchcraft::native::timestamp ||
        nt.OptionalHeader.SizeOfImage != witchcraft::native::imageSize) {
        puts("Native adapter fingerprint/signature mismatch"); return 4;
    }
    bool codeMatches = true;
    for (const auto& s : witchcraft::native::signatures) {
        unsigned char bytes[32]{};
        if (!ReadRemote(reader.process, reader.base + s.rva, bytes, s.count) ||
            std::memcmp(bytes, s.bytes.data(), s.count)) {
            printf("signature mismatch at %llx (first bytes %02x %02x %02x %02x %02x)\n",
                static_cast<unsigned long long>(s.rva), bytes[0],bytes[1],bytes[2],bytes[3],bytes[4]);
            codeMatches = false;
            // An installed observation hook changes this entry. Diagnostic
            // mode only reads memory and never authorizes engine calls.
            if (!diagnose || s.rva != witchcraft::native::advanceThreads) return 4;
        }
    }
    witchcraft::native::Sample sample{};
    if (!layout.Snapshot(sample)) {
        puts("Native adapter snapshot unavailable (menu/load/unsupported objects)");
        if (!diagnose&&!capture) return 5;
    } else {
        printf("Native adapter snapshot VERIFIED; original code matches=%u (read-only)\n", codeMatches ? 1u : 0u);
    }
    if(capture) {
        const int result=SampleFollow(layout,reader.base,static_cast<unsigned>(std::strtoul(argv[2],nullptr,10)),argv[3]);
        CloseHandle(reader.process);return result;
    }
    // Exact GetGlobal instruction signature from the matching installed map.
    unsigned char signature[10]{};
    const unsigned char expected[]{0x48,0x8b,0x05,0xa9,0xe7,0x36,0x03,0x48,0x63,0xca};
    if (!reader.read(reader.base + 0x270f2c0, signature) || std::memcmp(signature, expected, sizeof(expected))) {
        puts("GetGlobal signature mismatch; refusing guessed layout"); return 4;
    }
    auto globals = reader.pointer(reader.base + 0x5a7da70);
    reader.names = reader.pointer(reader.base + 0x5857dd8);
    auto groups = reader.pointer(reader.base + 0x5a58ec0);
    auto groupNames = reader.pointer(groups + 8);
    std::uint32_t groupCount = 0; reader.read(groups + 0x10, groupCount);
    printf("COLLISION GROUPS count=%u\n", groupCount);
    if (groupCount <= 64) for (unsigned i = 0; i < groupCount; ++i) {
        std::uint32_t id = 0; reader.read(groupNames + i * 4, id);
        printf(" collision group[%u] = %s\n", i, reader.name(id).c_str());
    }
    printf("script globals %llx\n", static_cast<unsigned long long>(globals));
    for (int i = 0; i < 10; ++i) {
        auto handle = reader.pointer(globals + 0x20 + 8 * i);
        auto object = reader.pointer(handle + 8);
        if (object) printf("global[%d] object=%llx type=%s vtable=%llx\n", i,
            static_cast<unsigned long long>(object), reader.type(object).c_str(),
            static_cast<unsigned long long>(reader.pointer(object) - reader.base));
        if (object && i < 3) reader.functions(object,i==2);
        if (i == 0) {
            auto world = reader.pointer(reader.pointer(object + 0xf0) + 8);
            printf("WORLD: %llx %s\n", static_cast<unsigned long long>(world), reader.type(world).c_str());
            auto physics = reader.pointer(world + 0x1f0);
            auto physicsVtable = reader.pointer(physics);
            printf("PHYSICS: %llx vtableRva=%llx type=%s\n", static_cast<unsigned long long>(physics),
                static_cast<unsigned long long>(physicsVtable - reader.base), reader.type(physics).c_str());
            for (int slot = 0; slot < 4; ++slot)
                printf(" physics virtual[%d] rva=%llx\n", slot,
                    static_cast<unsigned long long>(reader.pointer(physicsVtable + slot * 8) - reader.base));
            if (world) reader.functions(world);
        }
        if (i == 1) {
            float position[3]{};
            reader.read(object + 0xa0, position);
            printf("PLAYER POSITION: %.5f %.5f %.5f\n", position[0], position[1], position[2]);
            if(diagnose) {
                std::uintptr_t verifiedJump=0;
                printf("JUMP QUEUE identity/signatures verified=%u (read-only)\n",
                    witchcraft::native::JumpController(layout,reader.base,object,verifiedJump)?1u:0u);
                printf("JUMP GRAVITY identity/signatures verified=%u (read-only)\n",
                    witchcraft::native::JumpPhysics(layout,reader.base,object,verifiedJump)?1u:0u);
                // Read the two branches used by the verified Teleport helper.
                // Never invoke virtual functions or write character state here.
                auto group=reader.pointer(object+0xb8);
                auto controller=reader.pointer(object+0x218);
                auto root=reader.pointer(group+0x58);
                auto parent=reader.pointer(root+0x30);
                const std::uintptr_t objects[]{group,root,parent,controller};
                const char* labels[]{"attachment group","group root","root parent","movement controller"};
                for(int n=0;n<4;++n) {
                    const auto component=objects[n];
                    const auto vt=reader.pointer(component);
                    printf("PLAYER %s: object=%llx type=%s vtableRva=%llx\n",labels[n],
                        static_cast<unsigned long long>(component),reader.type(component).c_str(),
                        static_cast<unsigned long long>(vt?vt-reader.base:0));
                    if(component&&vt>=reader.base&&vt<reader.base+nt.OptionalHeader.SizeOfImage) {
                        for(unsigned slot:{0x1f8u,0x460u,0x638u}) {
                            const auto method=reader.pointer(vt+slot);
                            printf(" component slot=%x methodRva=%llx\n",slot,
                                static_cast<unsigned long long>(method>=reader.base&&method<reader.base+nt.OptionalHeader.SizeOfImage?method-reader.base:0));
                        }
                        reader.functions(component);
                    }
                }
                if(controller) {
                    auto manager=reader.pointer(controller+0x10a0);
                    auto primary=reader.pointer(manager+0x20);
                    auto vt=reader.pointer(primary);
                    printf("PRIMARY physical controller: object=%llx type=%s vtableRva=%llx\n",
                        static_cast<unsigned long long>(primary),reader.type(primary).c_str(),static_cast<unsigned long long>(vt?vt-reader.base:0));
                    if(primary&&vt>=reader.base&&vt<reader.base+nt.OptionalHeader.SizeOfImage)
                        for(unsigned slot:{0x30u,0x38u,0x40u,0x50u,0x68u})
                            printf(" primary slot=%x methodRva=%llx\n",slot,static_cast<unsigned long long>(reader.pointer(vt+slot)-reader.base));
                    std::uint16_t flags=0;reader.read(controller+0x1260,flags);
                    printf("MOVEMENT flags1260=%04x\n",flags);
                    auto physical=reader.pointer(primary+0x10);
                    auto wrapper=reader.pointer(controller+0x16d8);
                    auto gravityObject=reader.pointer(wrapper+0x10);
                    std::uint8_t gravityFlags=0;reader.read(physical+0x2c4,gravityFlags);
                    printf("GRAVITY physical=%llx wrapperPhysical=%llx type=%s vtableRva=%llx flags=%02x\n",
                        static_cast<unsigned long long>(physical),static_cast<unsigned long long>(gravityObject),
                        reader.type(physical).c_str(),static_cast<unsigned long long>(reader.pointer(physical)-reader.base),gravityFlags);
                }
            }
        }
        if (i == 2) {
            unsigned cameraCount=0;reader.read(object+0x60,cameraCount);
            const auto cameraList=reader.pointer(object+0x58);
            printf("CAMERA STACK count=%u list=%llx\n",cameraCount,static_cast<unsigned long long>(cameraList));
            if(cameraList&&cameraCount>0&&cameraCount<=64) {
                const auto record=cameraList+(cameraCount-1)*40ull;
                const auto cameraInterface=reader.pointer(record);
                const auto cameraHandle=reader.pointer(record+8);
                const auto cameraObject=reader.pointer(cameraHandle+8);
                printf("TOP CAMERA interface=%llx type=%s object=%llx type=%s\n",
                    static_cast<unsigned long long>(cameraInterface),reader.type(cameraInterface).c_str(),
                    static_cast<unsigned long long>(cameraObject),reader.type(cameraObject).c_str());
                if(cameraObject) reader.functions(cameraObject,true);
                const auto vt=reader.pointer(cameraInterface);
                if(vt>=reader.base&&vt<reader.base+nt.OptionalHeader.SizeOfImage) {
                    printf("TOP CAMERA interfaceVtableRva=%llx\n",static_cast<unsigned long long>(vt-reader.base));
                    for(unsigned slot=0;slot<0x40;slot+=8)
                        printf(" camera interface slot=%x methodRva=%llx\n",slot,
                            static_cast<unsigned long long>(reader.pointer(vt+slot)-reader.base));
                }
                float cameraPosition[4]{};reader.read(cameraObject+0x1d0,cameraPosition);
                printf("TOP CAMERA world position=(%.5f,%.5f,%.5f,%.5f)\n",
                    cameraPosition[0],cameraPosition[1],cameraPosition[2],cameraPosition[3]);
                float offset[4]{};reader.read(cameraObject+0x230,offset);
                printf("TOP CAMERA world offset=(%.5f,%.5f,%.5f,%.5f)\n",
                    offset[0],offset[1],offset[2],offset[3]);
            }
            float position[4]{}, direction[3]{}, fov = 0;
            reader.read(object + 0x70, position);
            reader.read(object + 0x790, direction);
            reader.read(object + 0xa0, fov);
            printf("CAMERA POSITION: %.5f %.5f %.5f\nCAMERA FORWARD: %.5f %.5f %.5f\nCAMERA FOV: %.5f\n",
                position[0],position[1],position[2],direction[0],direction[1],direction[2],fov);
        }
    }
    CloseHandle(reader.process);
}
