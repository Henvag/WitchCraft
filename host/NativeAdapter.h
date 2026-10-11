#pragma once
namespace witchcraft {
// Prepare the exact-build observation callback from the host worker, outside
// DllMain. This phase never drives Geralt or declares collision ready.
bool StartNativeAdapter(void (*log)(const char*));
void PollNativeAdapter();
}
