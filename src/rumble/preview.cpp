#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "preview.hpp"
#include "native_input.hpp"
#include <utility>
#include <cmath>

namespace {
bool Read(std::uintptr_t address, void* destination, std::size_t size) {
    const DWORD error = GetLastError();
    SIZE_T count = 0;
    const bool ok = address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
        destination, size, &count) && count == size;
    SetLastError(error);
    return ok;
}
template<class T> bool Read(std::uintptr_t address, T& value) { return Read(address, &value, sizeof(value)); }
struct Array { std::uintptr_t data; std::int32_t count, capacity; };
struct NativePoint { float amplitude, position, extra; };
static_assert(sizeof(Array) == 16 && sizeof(NativePoint) == 12);

bool ReadCurve(std::uintptr_t object, std::uint8_t type, ac7::RumbleCurve& curve) {
    const auto config = object + 0x60 + type * 32;
    std::uint8_t loops = 0;
    std::int32_t loop_index = 0;
    Array points{};
    if (!Read(config, loops) || !Read(config + 4, loop_index) || !Read(config + 8, points) ||
        points.count < 2 || points.count > 4096 || points.capacity < points.count || !points.data) return false;
    std::vector<NativePoint> native(static_cast<std::size_t>(points.count));
    if (!Read(points.data, native.data(), native.size() * sizeof(NativePoint))) return false;
    curve.loops = loops != 0;
    curve.loop_index = static_cast<std::size_t>(loop_index);
    for (const auto& point : native) curve.points.push_back({point.position, point.amplitude});
    return true;
}

std::optional<ac7::AfterburnerInput> ReadAfterburner(std::uintptr_t object) {
    const auto source = ac7::ResolveAfterburnerSource(object);
    ac7::AfterburnerInput input{};
    // Frozen getter0x81a0b0 is exactly a float load at +0x1638; no extra getter call is needed.
    if (!source || !Read(source + 0x1638, input.value) ||
        !Read(object + 0x6b8, input.fade_rate) || !std::isfinite(input.value) || !std::isfinite(input.fade_rate))
        return std::nullopt;
    return input;
}
}

namespace ac7 {
void RumblePreview::Initialize(PreviewOutput output) {
    output_ = output;
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const auto entropy = static_cast<std::uint64_t>(counter.QuadPart);
    playback_ = RumblePlayback(static_cast<std::uint32_t>(entropy ^ (entropy >> 32)) ^ GetCurrentProcessId());
    playback_.EndResume();
}
bool RumblePreview::OwnThread() {
    const DWORD current = GetCurrentThreadId();
    unsigned long unbound = 0;
    thread_.compare_exchange_strong(unbound, current);
    return thread_.load() == current;
}

void RumblePreview::Reset() {
    playback_.Clear();
    if (!paused_) playback_.EndResume();
    Publish();
    object_ = 0; resuming_ = false;
}
void RumblePreview::Bind(void* object) {
    const auto pointer = reinterpret_cast<std::uintptr_t>(object);
    if (pointer != object_) {
        Reset();
        object_ = pointer;
    }
}
void RumblePreview::Publish() {
    const auto sample = playback_.Sample();
    output_(sample.amplitude, sample.type, sample.paused);
}
void RumblePreview::Notify(void* object, std::uint8_t type, const void* source, float scale) {
    if (!OwnThread()) return;
    if (resuming_ || paused_) return;
    if (!object) { Reset(); return; }
    if (type >= 46) return;
    try {
        RumbleCurve curve, alternate;
        const auto pointer = reinterpret_cast<std::uintptr_t>(object);
        const bool cloud = type == 24 || type == 25;
        if (!ReadCurve(pointer, type, curve) || (cloud && !ReadCurve(pointer, type == 24 ? 25 : 24, alternate))) return;
        Bind(object);
        if (type == 0) {
            const auto context = ReadAfterburner(pointer);
            if (!context) return;
            if (context->value <= 0 && !playback_.AfterburnerGain()) return;
        }
        RumbleInput input;
        if (!CaptureNativeInput(source, input)) return;
        playback_.Notify(type, curve, scale, std::move(input), cloud ? &alternate : nullptr);
    } catch (...) { Reset(); }
}
void RumblePreview::Stop(void* object, std::uint8_t type) {
    if (OwnThread() && object_ && reinterpret_cast<std::uintptr_t>(object) == object_) {
        playback_.Stop(type); Publish();
    }
}
void RumblePreview::Scale(void* object, std::uint8_t type, float scale) {
    if (OwnThread() && object_ && reinterpret_cast<std::uintptr_t>(object) == object_) {
        playback_.SetScale(type, scale); Publish();
    }
}
void RumblePreview::Pause() {
    if (!OwnThread()) return;
    paused_ = true; resuming_ = false;
    playback_.Pause(); Publish();
}
void RumblePreview::BeginResume() {
    if (!OwnThread()) return;
    paused_ = true; resuming_ = true;
    playback_.BeginResume(); Publish();
}
void RumblePreview::EndResume() {
    if (!OwnThread() || !resuming_) return;
    paused_ = false; resuming_ = false;
    playback_.EndResume(); Publish();
}
void RumblePreview::StopAll() {
    if (!OwnThread()) return;
    playback_.StopAll(); Publish();
    object_ = 0;
}
void RumblePreview::Tick(void* object, float delta) {
    if (!OwnThread() || !object_ || reinterpret_cast<std::uintptr_t>(object) != object_) return;
    const auto afterburner = !paused_ && !resuming_ ? ReadAfterburner(object_) : std::nullopt;
    playback_.Tick(delta, afterburner);
    Publish();
}
}
