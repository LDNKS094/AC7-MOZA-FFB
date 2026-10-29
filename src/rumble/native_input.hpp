#pragma once
#include "playback.hpp"
#include <cstdint>

namespace ac7 {
// Initialize on the loader thread before enabling hooks; capture/sample/release on one game thread.
bool InitializeNativeInputs(std::uintptr_t image_base);
// Game-thread, fresh original PlayerController -> PlayerPlane route; no native-cache writes.
std::uintptr_t ResolveAfterburnerSource(std::uintptr_t rumble);
bool CaptureNativeInput(const void* source, RumbleInput& input);
}
