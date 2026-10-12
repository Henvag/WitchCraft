#pragma once
#include <array>
#include <cstdint>
namespace witchcraft {
// Pure key-ownership state. Game input callbacks and overlay share one mutex.
struct ManualKeys {
    std::array<bool,256> blocked{};
    bool forward=false,stop=false;
    bool playMode=false,overflow=false;
    bool guiMode=false;
    std::array<std::uint16_t,64> uiEvents{};
    std::array<unsigned,64> uiVirtualKeys{};
    std::array<unsigned,64> uiModifiers{};
    std::array<bool,256> uiOwned{};
    unsigned uiCount=0;
    static unsigned UiCode(unsigned vk) {
        if(vk>='A'&&vk<='Z') return 4+vk-'A';
        if(vk>='1'&&vk<='9') return 30+vk-'1';
        switch(vk) {
        case 0x74:return 62;case '0':return 39;case 0x0d:return 40;case 0x1b:return 41;
        case 8:return 42;case 9:return 43;case 0x20:return 44;
        case 0xbd:return 45;case 0xbb:return 46;case 0xdb:return 47;case 0xdd:return 48;
        case 0xdc:return 49;case 0xba:return 51;case 0xde:return 52;case 0xc0:return 53;
        case 0xbc:return 54;case 0xbe:return 55;case 0xbf:return 56;
        case 0x24:return 74;case 0x21:return 75;case 0x2e:return 76;case 0x23:return 77;case 0x22:return 78;
        case 0x27:return 79;case 0x25:return 80;case 0x28:return 81;case 0x26:return 82;
        case 0x10:case 0xa0:return 225;case 0xa1:return 229;
        case 0x11:case 0xa2:return 224;case 0xa3:return 228;
        default:return 0;
        }
    }
    bool UiAllowed(unsigned vk) const {
        return guiMode||vk==0x74||vk=='E'||vk=='T'||vk=='Q'||vk=='F'||
            (vk>='1'&&vk<='9')||vk==0x1b||vk==0x10||vk==0xa0||vk==0xa1||
            vk==0x11||vk==0xa2||vk==0xa3;
    }
    std::uint8_t movement=0;
    std::uint32_t wSeen=0,wBlocked=0,wPassed=0,stopSeen=0;
    void Disable() {forward=false;stop=false;movement=0;uiCount=0;overflow=false;uiOwned.fill(false);} // Keep matched-release ownership.
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
                if(const auto code=(UiAllowed(vk)||(!down&&uiOwned[vk]))?UiCode(vk):0) {
                    if(uiCount<uiEvents.size()) {
                        uiVirtualKeys[uiCount]=vk;
                        uiModifiers[uiCount]=(blocked[0x10]||blocked[0xa0]||blocked[0xa1]?1u:0u)|
                            (blocked[0x11]||blocked[0xa2]||blocked[0xa3]?2u:0u);
                        uiEvents[uiCount++]=static_cast<std::uint16_t>(code|(down?0x8000:0));
                        uiOwned[vk]=down;
                    }
                    else overflow=true;
                }
            }
            if(capture&&down&&!previous&&(vk==0x77||(!playMode&&vk==0x1b))) {stop=true;++stopSeen;}
        }
        return true;
    }
};
}
