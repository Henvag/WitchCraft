#pragma once
namespace witchcraft {
// Read physical F8 outside captured input too. Track held keys while unfocused
// so returning to the window cannot manufacture an activation press.
class PlayToggleKey {
    bool held_=false;
public:
    bool Update(bool down,bool focused) {
        const bool pressed=focused&&down&&!held_;held_=down;return pressed;
    }
};
}
