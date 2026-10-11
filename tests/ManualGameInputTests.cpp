#include "ManualGameInput.h"
#include <array>
using namespace witchcraft;
unsigned calls=0;bool allow=false;
bool Key(unsigned vk,bool down) {++calls;return allow&&vk=='W'&&down;}
unsigned mouseCalls=0;
bool Mouse(RAWMOUSE& mouse) {++mouseCalls;mouse.usButtonFlags=0;mouse.usButtonData=0;mouse.lLastX=mouse.lLastY=0;return true;}
int main() {
    RAWINPUT raw{};raw.header.dwType=RIM_TYPEKEYBOARD;raw.header.dwSize=offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD);
    raw.data.keyboard.VKey='W';raw.data.keyboard.MakeCode=17;raw.data.keyboard.Message=WM_KEYDOWN;
    auto before=raw;
    if(FilterManualRaw(&raw,raw.header.dwSize,Key)||std::memcmp(&raw,&before,sizeof(raw))) return 1;
    allow=true;
    if(FilterManualRaw(&raw,raw.header.dwSize-1,Key)||calls!=1) return 2;
    if(!FilterManualRaw(&raw,raw.header.dwSize,Key)||raw.data.keyboard.VKey!=255||raw.data.keyboard.MakeCode!=0||raw.header.dwSize!=before.header.dwSize) return 3;
    raw=before;raw.header.dwType=RIM_TYPEMOUSE;before=raw;
    if(FilterManualRaw(&raw,sizeof(raw),Key)||std::memcmp(&raw,&before,sizeof(raw))) return 4;
    if(FilterManualRaw(nullptr,100,Key)) return 5;
    alignas(16) std::array<std::uint8_t,4096> image{};
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=128;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data()+128);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.NumberOfRvaAndSizes=16;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]={512,40};
    auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image.data()+512);
    descriptor->Name=900;descriptor->OriginalFirstThunk=600;descriptor->FirstThunk=700;
    reinterpret_cast<IMAGE_THUNK_DATA64*>(image.data()+600)->u1.AddressOfData=800;
    std::memcpy(image.data()+802,"GetRawInputData",15);
    if(FindInputImport(image.data(),image.size(),"GetRawInputData")!=reinterpret_cast<void**>(image.data()+700)) return 6;
    if(FindInputImport(image.data(),image.size(),"Missing")) return 7;
    descriptor->FirstThunk=4095;
    if(FindInputImport(image.data(),image.size(),"GetRawInputData")) return 8;
    descriptor->FirstThunk=700;nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size=5000;
    if(FindInputImport(image.data(),image.size(),"GetRawInputData")) return 9;
    if(FindInputImport(image.data(),16,"GetRawInputData")) return 10;
    raw={};raw.header.dwType=RIM_TYPEMOUSE;raw.header.dwSize=offsetof(RAWINPUT,data)+sizeof(RAWMOUSE);
    raw.data.mouse.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN|RI_MOUSE_WHEEL;raw.data.mouse.usButtonData=120;raw.data.mouse.lLastX=15;
    before=raw;
    if(FilterManualMouse(&raw,raw.header.dwSize-1,Mouse)||mouseCalls||std::memcmp(&raw,&before,sizeof(raw))) return 11;
    if(!FilterManualMouse(&raw,raw.header.dwSize,Mouse)||mouseCalls!=1||raw.data.mouse.usButtonFlags||raw.data.mouse.lLastX||raw.header.dwSize!=before.header.dwSize) return 12;
    raw=before;raw.header.dwType=RIM_TYPEKEYBOARD;before=raw;
    if(FilterManualMouse(&raw,sizeof(raw),Mouse)||mouseCalls!=1||std::memcmp(&raw,&before,sizeof(raw))) return 13;
    raw.header.dwType=RIM_TYPEMOUSE;raw.header.dwSize=1000;
    if(FilterManualMouse(&raw,sizeof(raw),Mouse)||mouseCalls!=1||FilterManualMouse(nullptr,100,Mouse)) return 14;
}
