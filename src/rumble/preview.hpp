#pragma once
#include "playback.hpp"
#include <atomic>
#include <cstdint>

namespace ac7 {
using PreviewOutput = void(*)(double, int, bool);

// Owns independent native input copies and publishes the classified output.
class RumblePreview {
public:
    void Initialize(PreviewOutput output);
    void Notify(void* object, std::uint8_t type, const void* source, float scale);
    void Stop(void* object, std::uint8_t type);
    void Scale(void* object, std::uint8_t type, float scale);
    void Pause();
    void BeginResume();
    void EndResume();
    void StopAll();
    void Tick(void* object, float delta);
private:
    bool OwnThread();
    void Bind(void* object);
    void Publish();
    void Reset();
    PreviewOutput output_ = nullptr;
    std::atomic<unsigned long> thread_{0};
    std::uintptr_t object_ = 0;
    RumblePlayback playback_;
    bool paused_ = false;
    bool resuming_ = false;
};
}
