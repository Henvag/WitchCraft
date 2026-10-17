#include "Input.h"
#include "ManualKeys.h"
#include "ManualGameInput.h"
#include "NativeCamera.h"
#include <mutex>
#include <algorithm>
#include <vector>
namespace witchcraft {
namespace {
std::mutex mutex;
ManualKeys keys;
std::array<bool,256> physical{};
bool enabled=false,attempted=false,installed=false;
ULONGLONG refreshed=0;
DWORD installError=0;
bool play=false,gui=false,mouseOverflow=false;
int viewportW=0,viewportH=0,cursorX=0,cursorY=0;
std::array<bool,3> mouseDown{},mouseOwned{};
std::vector<skycraft::proto::InputEvent> mouseEvents;
unsigned repeatKey=0;
ULONGLONG repeatAt=0;
void Append(ManualInput& result,const skycraft::proto::InputEvent& event) {
    if(result.eventCount<result.events.size()) result.events[result.eventCount++]=event;
    else result.overflow=true;
}
bool Repeatable(unsigned vk) {
    return (vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9')||vk==8||vk==0x20||
        (vk>=0x25&&vk<=0x28)||vk==0x2e||(vk>=0xba&&vk<=0xde);
}
void Text(unsigned vk,ManualInput& result,const std::array<bool,256>& down,unsigned modifiers) {
    if(!gui||(modifiers&2)) return;
    BYTE state[256]{};
    for(unsigned i=0;i<256;++i) state[i]=down[i]?0x80:0;
    state[VK_CAPITAL]|=::GetKeyState(VK_CAPITAL)&1;
    state[VK_SHIFT]=(modifiers&1)?0x80:0;
    state[VK_LSHIFT]=state[VK_RSHIFT]=state[VK_SHIFT];
    state[VK_CONTROL]=state[VK_LCONTROL]=state[VK_RCONTROL]=0;
    const auto thread=::GetWindowThreadProcessId(::GetForegroundWindow(),nullptr);
    const auto layout=::GetKeyboardLayout(thread);
    wchar_t text[8]{};
    const int count=::ToUnicodeEx(vk,::MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC,layout),
        state,text,8,4,layout); // Do not change the game's dead-key state.
    for(int i=0;i<count&&i<8;++i) {
        std::uint32_t value=text[i];
        if(value>=0xd800&&value<=0xdbff&&i+1<count&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff) {
            value=0x10000+((value-0xd800)<<10)+(text[++i]-0xdc00);
        }
        if(value>=32&&value!=127&&!(value>=0xd800&&value<=0xdfff))
            Append(result,{skycraft::proto::kInText,0,static_cast<int>(value),0,0});
    }
}
void Queue(skycraft::proto::InputType type,unsigned code,int a,int b=0) {
    if(mouseEvents.size()<128) mouseEvents.push_back({static_cast<std::uint16_t>(type),static_cast<std::uint16_t>(code),a,b,0});
    else mouseOverflow=true;
}
void MouseButton(unsigned index,bool down,bool capture) {
    if(index>=3) return;
    if(mouseDown[index]!=down) {
        if(capture) {
            if(gui) Queue(skycraft::proto::kInCursor,0,cursorX,cursorY);
            Queue(skycraft::proto::kInMouseButton,index+1,down?1:0);
        }
        mouseDown[index]=down;
    }
    if(capture&&down) mouseOwned[index]=true;
    if(!down) mouseOwned[index]=false;
}
bool Focused() {
    DWORD pid=0;::GetWindowThreadProcessId(::GetForegroundWindow(),&pid);
    return pid==::GetCurrentProcessId();
}
bool System(unsigned vk) {
    return vk==VK_LWIN||vk==VK_RWIN||vk==VK_MENU||vk==VK_LMENU||vk==VK_RMENU||
        (vk==VK_TAB&&(::GetAsyncKeyState(VK_MENU)&0x8000));
}
bool Capture() {return enabled&&::GetTickCount64()-refreshed<=1000;}
}
bool ManualRawMouse(RAWMOUSE& mouse) {
    const bool focus=Focused();
    std::lock_guard lock(mutex);
    const bool capture=Capture()&&play&&focus;
    const bool owned=mouseOwned[0]||mouseOwned[1]||mouseOwned[2];
    if(!capture&&!owned) return false;
    if(capture&&gui&&!(mouse.usFlags&MOUSE_MOVE_ABSOLUTE)) {
        cursorX=static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(cursorX)+mouse.lLastX,0,std::max(0,viewportW-1)));
        cursorY=static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(cursorY)+mouse.lLastY,0,std::max(0,viewportH-1)));
    }
    const USHORT flags=mouse.usButtonFlags;
    const unsigned down[]{RI_MOUSE_LEFT_BUTTON_DOWN,RI_MOUSE_MIDDLE_BUTTON_DOWN,RI_MOUSE_RIGHT_BUTTON_DOWN};
    const unsigned up[]{RI_MOUSE_LEFT_BUTTON_UP,RI_MOUSE_MIDDLE_BUTTON_UP,RI_MOUSE_RIGHT_BUTTON_UP};
    for(unsigned i=0;i<3;++i) {
        if(flags&down[i]) MouseButton(i,true,capture);
        if(flags&up[i]) MouseButton(i,false,capture);
    }
    if(capture&&(flags&RI_MOUSE_WHEEL)) Queue(skycraft::proto::kInScroll,0,static_cast<SHORT>(mouse.usButtonData));
    if(capture||owned) {mouse.usButtonFlags=0;mouse.usButtonData=0;}
    if(capture&&gui) {mouse.lLastX=0;mouse.lLastY=0;}
    // Outside Minecraft screens the camera hook owns the look once verified;
    // Witcher's camera must not also turn (and clamp) with the same movement.
    else if(capture&&!(mouse.usFlags&MOUSE_MOVE_ABSOLUTE)&&(mouse.lLastX||mouse.lLastY)&&
        native::AddLookDelta(mouse.lLastX,mouse.lLastY)) {mouse.lLastX=0;mouse.lLastY=0;}
    return true;
}
bool ManualRawKey(unsigned vk,bool down) {
    const bool focus=Focused(),system=System(vk);
    std::lock_guard lock(mutex);
    return keys.Event(vk,down,Capture(),focus,system,false);
}
bool ManualBlockPolledKey(int vk) {
    if(vk<0||vk>=256||System(static_cast<unsigned>(vk))||!Focused()) return false;
    std::lock_guard lock(mutex);
    return Capture()||keys.blocked[vk];
}
ManualInput PollManualInput(bool requested,bool playMode,bool screen,int width,int height) {
    if(requested&&!attempted) {attempted=true;installed=InstallManualGameInput(installError);}
    const auto now=::GetTickCount64();
    const bool focus=Focused();
    std::array<bool,256> current{};bool held=false;
    // This DLL's user32 imports retain the real physical state. Only imports
    // belonging to the game executable are filtered.
    for(int vk=8;vk<256;++vk) {current[vk]=(::GetAsyncKeyState(vk)&0x8000)!=0;held|=current[vk];}
    std::lock_guard lock(mutex);
    keys.playMode=playMode;play=playMode;
    if(playMode&&screen&&!gui) {cursorX=width/2;cursorY=height/2;}
    gui=playMode&&screen;viewportW=width;viewportH=height;
    keys.guiMode=gui;
    if(!requested||!installed||!focus) {enabled=false;keys.Disable();}
    else if(!enabled&&!held) {keys.Disable();enabled=true;}
    refreshed=now;
    for(unsigned vk=8;vk<256;++vk) {
        if(current[vk]!=physical[vk]) keys.Event(vk,current[vk],enabled,focus,System(vk),false);
        physical[vk]=current[vk];
    }
    ManualInput result;
    if(playMode&&enabled) {
        const int buttons[]{VK_LBUTTON,VK_MBUTTON,VK_RBUTTON};
        for(unsigned i=0;i<3;++i) MouseButton(i,(::GetAsyncKeyState(buttons[i])&0x8000)!=0,true);
        for(unsigned i=0;i<keys.uiCount;++i) {
            const auto event=keys.uiEvents[i];
            Append(result,{skycraft::proto::kInKey,static_cast<std::uint16_t>(event&0x7fff),(event&0x8000)?1:0,0,0});
            const auto vk=keys.uiVirtualKeys[i];
            if(gui&&(event&0x8000)&&Repeatable(vk)) {
                Text(vk,result,current,keys.uiModifiers[i]);repeatKey=vk;repeatAt=now+500;
            }
        }
        if(!gui||!repeatKey||!current[repeatKey]) repeatKey=0;
        else if(now>=repeatAt) {
            Append(result,{skycraft::proto::kInKey,
                static_cast<std::uint16_t>(ManualKeys::UiCode(repeatKey)),1,0,0});
            const unsigned modifiers=(current[VK_SHIFT]||current[VK_LSHIFT]||current[VK_RSHIFT]?1u:0u)|
                (current[VK_CONTROL]||current[VK_LCONTROL]||current[VK_RCONTROL]?2u:0u);
            Text(repeatKey,result,current,modifiers);repeatAt=now+33;
        }
        for(const auto& event:mouseEvents) Append(result,event);
        result.overflow|=keys.overflow||mouseOverflow;
        result.gui=gui;result.cursorX=cursorX;result.cursorY=cursorY;
    }
    if(!enabled) repeatKey=0;
    keys.uiCount=0;keys.overflow=false;mouseEvents.clear();mouseOverflow=false;
    result.ready=enabled;result.forward=enabled&&keys.forward;result.stop=enabled&&keys.stop;
    result.movement=enabled?keys.movement:0;
    result.wSeen=keys.wSeen;result.wBlocked=keys.wBlocked;result.wPassed=keys.wPassed;
    result.stopSeen=keys.stopSeen;result.hookError=installError;
    const auto stats=ReadManualGameStats();result.rawSeen=stats.rawSeen;result.rawBlocked=stats.rawBlocked;result.asyncBlocked=stats.asyncBlocked;
    keys.stop=false;return result;
}
}
