#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace witchcraft {
inline void** FindInputImport(std::uint8_t* base,std::size_t size,const char* wanted) {
    auto inside=[&](std::size_t offset,std::size_t count) {return offset<=size&&count<=size-offset;};
    if(!inside(0,sizeof(IMAGE_DOS_HEADER))) return nullptr;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||!inside(dos->e_lfanew,sizeof(IMAGE_NT_HEADERS64))) return nullptr;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt->OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_IMPORT) return nullptr;
    const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress||!inside(dir.VirtualAddress,dir.Size)) return nullptr;
    for(std::size_t offset=0;offset+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=dir.Size;offset+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto* descriptor=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress+offset);
        if(!descriptor->Name) break;
        if(!descriptor->OriginalFirstThunk||!descriptor->FirstThunk) continue;
        for(std::size_t index=0;;++index) {
            const auto source=static_cast<std::size_t>(descriptor->OriginalFirstThunk)+index*sizeof(IMAGE_THUNK_DATA64);
            const auto target=static_cast<std::size_t>(descriptor->FirstThunk)+index*sizeof(IMAGE_THUNK_DATA64);
            if(!inside(source,sizeof(IMAGE_THUNK_DATA64))||!inside(target,sizeof(IMAGE_THUNK_DATA64))) return nullptr;
            const auto entry=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+source)->u1.AddressOfData;
            if(!entry) break;
            if(IMAGE_SNAP_BY_ORDINAL64(entry)) continue;
            if(entry>size||!inside(static_cast<std::size_t>(entry),sizeof(WORD)+1)) return nullptr;
            const auto name=static_cast<std::size_t>(entry)+sizeof(WORD);
            const auto* end=static_cast<const char*>(std::memchr(base+name,0,size-name));
            if(!end) return nullptr;
            const auto length=static_cast<std::size_t>(end-reinterpret_cast<const char*>(base+name));
            if(length==std::strlen(wanted)&&!std::memcmp(base+name,wanted,length)) return reinterpret_cast<void**>(base+target);
        }
    }
    return nullptr;
}
inline bool FilterManualRaw(void* data,UINT bytes,bool(*key)(unsigned,bool)) {
    constexpr auto keyboardBytes=offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD);
    if(!data||bytes<keyboardBytes) return false;
    auto* raw=static_cast<RAWINPUT*>(data);
    if(raw->header.dwType!=RIM_TYPEKEYBOARD||raw->header.dwSize<keyboardBytes||raw->header.dwSize>bytes||raw->data.keyboard.VKey==0xff) return false;
    auto& event=raw->data.keyboard;
    if(!key(event.VKey,(event.Flags&RI_KEY_BREAK)==0)) return false;
    // VKey 255 is documented as discardable; also neutralize scan-code paths.
    event.MakeCode=0;event.VKey=0xff;event.Flags=RI_KEY_BREAK;event.Message=WM_KEYUP;event.ExtraInformation=0;return true;
}
inline bool FilterManualMouse(void* data,UINT bytes,bool(*mouse)(RAWMOUSE&)) {
    constexpr auto minimum=offsetof(RAWINPUT,data)+sizeof(RAWMOUSE);
    if(!data||bytes<minimum) return false;
    auto* raw=static_cast<RAWINPUT*>(data);
    if(raw->header.dwType!=RIM_TYPEMOUSE||raw->header.dwSize<minimum||raw->header.dwSize>bytes) return false;
    return mouse(raw->data.mouse);
}
struct ManualGameStats {std::uint32_t rawSeen=0,rawBlocked=0,asyncBlocked=0;};
bool InstallManualGameInput(DWORD& error);
ManualGameStats ReadManualGameStats();
bool ManualRawMouse(RAWMOUSE& mouse);
bool ManualRawKey(unsigned vk,bool down);
bool ManualBlockPolledKey(int vk);
}
