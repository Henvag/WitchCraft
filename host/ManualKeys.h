#pragma once
#include <array>
#include <cstdint>
namespace witchcraft {
// Pure key-ownership state. Game input callbacks and overlay share one mutex.
struct ManualKeys {
    std::array<bool,256> blocked{};
    bool forward=false,stop=false;
    bool playMode=false,overflow=false;
    std::array<std::uint16_t,64> uiEvents{};
    unsigned uiCount=0;
    static unsigned UiCode(unsigned vk) {
        if(vk>='1'&&vk<='9') return 30+vk-'1';
        return vk=='E'?8:vk==0x1b?41:vk==0x10||vk==0xa0?225:vk==0xa1?229:vk==0x11||vk==0xa2?224:vk==0xa3?228:0;
    }
    std::uint8_t movement=0;
    std::uint32_t wSeen=0,wBlocked=0,wPassed=0,stopSeen=0;
    void Disable() {forward=false;stop=false;movement=0;uiCount=0;overflow=false;} // Keep matched-release ownership.
    bool Event(unsigned vk,bool down,bool capture,bool focused,bool system,bool injected) {
        if(vk>=blocked.size()) return false;
        if(vk=='W') ++wSeen;
        if(!focused) {Disable();blocked.fill(false);return false;}
        const bool swallow=(capture&&!system)||blocked[vk];
        if(vk=='W') {if(swallow) ++wBlocked;else ++wPassed;}
        if(!swallow) return false;
        const bool previous=blocked[vk];blocked[vk]=down;
        if(!injected) {
            const unsigned bit=vk=='W'?1:vk=='A'?2:vk=='S'?4:vk=='D'?8:vk==0x20?16:0;
            if(capture&&down) movement|=bit;else movement&=~bit;
            if(vk=='W') forward=capture&&down;
            if(playMode&&capture&&previous!=down) {
                if(const auto code=UiCode(vk)) {
                    if(uiCount<uiEvents.size()) uiEvents[uiCount++]=static_cast<std::uint16_t>(code|(down?0x8000:0));
                    else overflow=true;
                }
            }
            if(capture&&down&&!previous&&(vk==0x77||(!playMode&&vk==0x1b))) {stop=true;++stopSeen;}
        }
        return true;
    }
};
}
