#pragma once
#include "ScriptBridge.h"
namespace witchcraft::script {
// Game thread, once the image is validated. Replaces the ClampF script thunk
// after checking its exact bytes; returns false (and stays off) on mismatch.
bool InstallScriptBridge(std::uintptr_t base,void(*log)(const char*));
// Overlay thread: whether camera-owned Minecraft play is active, and Geralt's
// reported damage to forward to Minecraft as kInHurt.
void SetScriptPlayActive(bool active);
std::vector<Hurt> TakeScriptHurts();
}
