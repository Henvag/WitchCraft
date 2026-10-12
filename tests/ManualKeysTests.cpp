#include "ManualKeys.h"
using namespace witchcraft;
int main() {
    ManualKeys keys;
    if(!keys.Event('W',true,true,true,false,false)||!keys.forward||keys.wBlocked!=1) return 1;
    keys.Disable();
    if(!keys.Event('W',true,false,true,false,false)||keys.forward) return 2;
    if(!keys.Event('W',false,false,true,false,false)||keys.blocked['W']) return 3;
    if(keys.Event('W',true,false,true,false,false)||keys.wPassed!=1) return 4;
    if(!keys.Event(0x77,true,true,true,false,false)||!keys.stop||keys.stopSeen!=1) return 5;
    keys.stop=false;
    if(!keys.Event(0x77,true,true,true,false,false)||keys.stop) return 6;
    if(!keys.Event(0x77,false,true,true,false,false)) return 7;
    if(!keys.Event(0x1b,true,true,true,false,false)||!keys.stop||keys.stopSeen!=2) return 8;
    if(keys.Event('W',true,true,false,false,false)||keys.forward||keys.stop||keys.blocked[0x1b]) return 9;
    if(keys.Event(0x5b,true,true,true,true,false)) return 10;
    if(!keys.Event('W',true,true,true,false,true)||keys.forward) return 11;
    if(!keys.Event('W',false,true,true,false,true)||keys.forward) return 12;
    if(!keys.Event('W',true,true,true,false,false)||!keys.forward) return 13;
    if(!keys.Event('W',false,true,true,false,false)||keys.forward) return 14;
    if(keys.Event(256,true,true,true,false,false)) return 15;
    if(!keys.Event('A',true,true,true,false,false)||keys.movement!=2) return 16;
    if(!keys.Event('D',true,true,true,false,false)||keys.movement!=10) return 17;
    if(!keys.Event('S',true,true,true,false,false)||keys.movement!=14) return 18;
    if(!keys.Event('W',true,true,true,false,false)||keys.movement!=15) return 19;
    if(!keys.Event('A',false,true,true,false,false)||keys.movement!=13) return 20;
    keys.Disable();
    if(!keys.Event('D',true,false,true,false,false)||keys.movement) return 21;
    if(!keys.Event('D',false,false,true,false,false)||keys.movement) return 22;
    if(!keys.Event(0x20,true,true,true,false,false)||keys.movement!=16) return 23;
    keys.Disable();
    if(!keys.Event(0x20,true,false,true,false,false)||keys.movement) return 24;
    if(!keys.Event(0x20,false,false,true,false,false)||keys.blocked[0x20]) return 25;
    ManualKeys play;play.playMode=true;
    if(!play.Event('E',true,true,true,false,false)||play.uiCount!=1||play.uiEvents[0]!=(0x8000|8)) return 26;
    play.Event('E',true,true,true,false,false);if(play.uiCount!=1) return 27;
    play.Event('E',false,true,true,false,false);if(play.uiCount!=2||play.uiEvents[1]!=8) return 28;
    play.Event(0x1b,true,true,true,false,false);if(play.stop||play.uiEvents[2]!=(0x8000|41)) return 29;
    play.Event('9',true,true,true,false,false);if(play.uiEvents[3]!=(0x8000|38)) return 30;
    play.Event(0x77,true,true,true,false,false);if(!play.stop||play.stopSeen!=1) return 31;
    for(int i=0;i<100;++i) play.Event('E',i%2,true,true,false,false);
    if(!play.overflow||play.uiCount!=64) return 32;
    play.Disable();if(play.uiCount||play.overflow) return 33;
    ManualKeys chat;chat.playMode=true;
    chat.Event('T',true,true,true,false,false);
    if(chat.uiCount!=1||chat.uiEvents[0]!=(0x8000|23)) return 34;
    chat.Event('W',true,true,true,false,false);
    if(chat.uiCount!=1) return 35; // Gameplay movement remains owned by PlaySession.
    chat.Event('W',false,true,true,false,false);
    chat.guiMode=true;
    chat.Event('W',true,true,true,false,false);
    chat.Event(0x0d,true,true,true,false,false);
    if(chat.uiEvents[1]!=(0x8000|26)||chat.uiEvents[2]!=(0x8000|40)) return 36;
    chat.guiMode=false;chat.Event('W',false,true,true,false,false);
    if(chat.uiCount!=4||chat.uiEvents[3]!=26) return 37; // Matched release after chat closes.
    if(ManualKeys::UiCode(8)!=42||ManualKeys::UiCode(0x25)!=80||ManualKeys::UiCode(0xbf)!=56) return 38;
    chat.guiMode=true;chat.Event(0xa0,true,true,true,false,false);
    chat.Event('7',true,true,true,false,false);const auto shifted=chat.uiCount-1;
    chat.Event(0xa0,false,true,true,false,false);
    if(chat.uiModifiers[shifted]!=1) return 39; // Shift at keypress survives release before polling.
}
