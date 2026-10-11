#include "ManualGameInput.h"
#include <atomic>
namespace witchcraft {
namespace {
using RawFn=UINT(WINAPI*)(HRAWINPUT,UINT,LPVOID,PUINT,UINT);
using AsyncFn=SHORT(WINAPI*)(int);
std::atomic<RawFn> originalRaw{nullptr};
std::atomic<AsyncFn> originalAsync{nullptr};
std::atomic<std::uint32_t> rawSeen{0},rawBlocked{0},asyncBlocked{0};
bool RecordKey(unsigned vk,bool down) {++rawSeen;return ManualRawKey(vk,down);}
UINT WINAPI RawInput(HRAWINPUT input,UINT command,LPVOID data,PUINT size,UINT headerSize) {
    const UINT bytes=originalRaw.load()(input,command,data,size,headerSize);
    if(command==RID_INPUT&&bytes!=static_cast<UINT>(-1)&&FilterManualMouse(data,bytes,ManualRawMouse)) ++rawBlocked;
    if(command==RID_INPUT&&bytes!=static_cast<UINT>(-1)&&FilterManualRaw(data,bytes,RecordKey)) ++rawBlocked;
    return bytes;
}
SHORT WINAPI AsyncKey(int vk) {
    if(ManualBlockPolledKey(vk)) {++asyncBlocked;return 0;}
    return originalAsync.load()(vk);
}
bool Replace(void** slot,void* expected,void* replacement,DWORD& error) {
    DWORD protection=0;
    if(!::VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protection)) {error=::GetLastError();return false;}
    const bool changed=::InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    DWORD unused=0;
    const bool restored=::VirtualProtect(slot,sizeof(void*),protection,&unused)!=FALSE;
    if(!changed) error=ERROR_INVALID_DATA;else if(!restored) error=::GetLastError();
    return changed&&restored;
}
}
bool InstallManualGameInput(DWORD& error) {
    static_assert(sizeof(void*)==8);
    auto* base=reinterpret_cast<std::uint8_t*>(::GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>0x100000) {error=ERROR_BAD_EXE_FORMAT;return false;}
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) {error=ERROR_BAD_EXE_FORMAT;return false;}
    auto** raw=FindInputImport(base,nt->OptionalHeader.SizeOfImage,"GetRawInputData");
    auto** async=FindInputImport(base,nt->OptionalHeader.SizeOfImage,"GetAsyncKeyState");
    const auto user=::GetModuleHandleW(L"user32.dll");
    const auto rawExpected=reinterpret_cast<void*>(::GetProcAddress(user,"GetRawInputData"));
    const auto asyncExpected=reinterpret_cast<void*>(::GetProcAddress(user,"GetAsyncKeyState"));
    if(!raw||!async||!rawExpected||!asyncExpected||*raw!=rawExpected||*async!=asyncExpected) {error=ERROR_INVALID_DATA;return false;}
    originalRaw=reinterpret_cast<RawFn>(rawExpected);originalAsync=reinterpret_cast<AsyncFn>(asyncExpected);
    if(!Replace(raw,rawExpected,reinterpret_cast<void*>(&RawInput),error)) return false;
    if(!Replace(async,asyncExpected,reinterpret_cast<void*>(&AsyncKey),error)) {
        DWORD rollbackError=0;Replace(raw,reinterpret_cast<void*>(&RawInput),rawExpected,rollbackError);return false;
    }
    error=ERROR_SUCCESS;return true;
}
ManualGameStats ReadManualGameStats() {return {rawSeen.load(),rawBlocked.load(),asyncBlocked.load()};}
}
