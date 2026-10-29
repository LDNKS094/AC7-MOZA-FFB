#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "native_input.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>

namespace ac7 {
namespace {
struct alignas(16) Storage { std::array<std::byte, 64> bytes{}; };
static_assert(sizeof(Storage) == 64 && alignof(Storage) == 16);
using Copy = void*(__fastcall*)(void*, const void*);
using StaticClass = std::uintptr_t(__fastcall*)();
using Valid = bool(__fastcall*)(void*);
using Value = float(__fastcall*)(void*);
Copy copy_source = nullptr;
StaticClass player_controller_class = nullptr;
StaticClass player_plane_class = nullptr;
struct PreserveLastError {
    DWORD value = GetLastError();
    ~PreserveLastError() { SetLastError(value); }
};

bool Read(const void* address, void* destination, std::size_t size) {
    const DWORD error = GetLastError();
    SIZE_T count = 0;
    const bool ok = address && ReadProcessMemory(GetCurrentProcess(), address, destination, size, &count) && count == size;
    SetLastError(error);
    return ok;
}
// The same UClass base-chain check used by native Tick; no object serial/table lookup.
bool IsA(std::uintptr_t object, std::uintptr_t expected) {
    std::uintptr_t actual = 0, chain = 0, entry = 0;
    std::int32_t index = -1, count = -1;
    return object && expected && Read(reinterpret_cast<const void*>(object + 0x10), &actual, sizeof(actual)) && actual &&
        Read(reinterpret_cast<const void*>(expected + 0x90), &index, sizeof(index)) && index >= 0 &&
        Read(reinterpret_cast<const void*>(actual + 0x90), &count, sizeof(count)) && index <= count &&
        Read(reinterpret_cast<const void*>(actual + 0x88), &chain, sizeof(chain)) && chain &&
        Read(reinterpret_cast<const void*>(chain + static_cast<std::uintptr_t>(index) * 8), &entry, sizeof(entry)) &&
        entry == expected + 0x88;
}

// This is the original small-object wrapper layout, not UObject index/serial tracking.
bool Resolve(const void* source, void*& object, bool& empty) {
    Storage snapshot;
    if (!Read(source, &snapshot, sizeof(snapshot))) return false;
    std::int32_t blocks = 0;
    std::uintptr_t heap = 0;
    std::memcpy(&blocks, snapshot.bytes.data() + 0x30, sizeof(blocks));
    std::memcpy(&heap, snapshot.bytes.data() + 0x20, sizeof(heap));
    if (blocks < 0) return false;
    empty = blocks == 0;
    object = empty ? nullptr : heap ? reinterpret_cast<void*>(heap) : const_cast<void*>(source);
    return true;
}

struct CapturedInput {
    Storage storage;
    Copy copy = nullptr;
    DWORD owner = GetCurrentThreadId();
    bool cloned = false;

    ~CapturedInput() {
        const PreserveLastError error;
        // All normal releases occur through game-thread Stop/Clear/erase, never a worker.
        // shortcut: at process teardown on a foreign thread let the OS reclaim storage, not game allocators.
        if (cloned && owner == GetCurrentThreadId()) {
            const Storage empty;
            copy(&storage, &empty); // Native release also frees any wrapper heap allocation.
        }
    }
    RumbleInputSample Sample() {
        const PreserveLastError error;
        if (owner != GetCurrentThreadId()) return {false, std::nullopt};
        void* object = nullptr;
        bool empty = false;
        if (!Resolve(&storage, object, empty) || empty) return {false, std::nullopt};
        std::uintptr_t table = 0;
        Valid valid = nullptr;
        Value value = nullptr;
        if (!Read(object, &table, sizeof(table)) ||
            !Read(reinterpret_cast<const void*>(table + 0x38), &valid, sizeof(valid)) || !valid ||
            !Read(reinterpret_cast<const void*>(table + 0x60), &value, sizeof(value)) || !value)
            return {false, std::nullopt};
        // Use the original provider validity contract. Native invalid delegates fall back to age.
        if (!valid(object)) return {};
        return {true, value(object)};
    }
};
}

bool InitializeNativeInputs(std::uintptr_t image_base) {
    copy_source = nullptr;
    player_controller_class = nullptr;
    player_plane_class = nullptr;
    const auto address = reinterpret_cast<const void*>(image_base + 0x1769870);
    const unsigned char prefix[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x3b, 0xd1};
    unsigned char actual[sizeof(prefix)]{};
    if (!Read(address, actual, sizeof(actual)) || std::memcmp(actual, prefix, sizeof(prefix))) return false;
    const unsigned char class_prefix[] = {0x4c, 0x8b, 0xdc, 0x48, 0x83, 0xec, 0x78};
    unsigned char class_actual[sizeof(class_prefix)]{};
    for (const auto rva : {std::uintptr_t{0x1c08560}, std::uintptr_t{0x938fb0}}) {
        if (!Read(reinterpret_cast<const void*>(image_base + rva), class_actual, sizeof(class_actual)) ||
            std::memcmp(class_actual, class_prefix, sizeof(class_prefix))) return false;
    }
    const unsigned char getter_prefix[] = {0xf3, 0x0f, 0x10, 0x81, 0x38, 0x16, 0x00, 0x00, 0xc3};
    unsigned char getter_actual[sizeof(getter_prefix)]{};
    if (!Read(reinterpret_cast<const void*>(image_base + 0x81a0b0), getter_actual, sizeof(getter_actual)) ||
        std::memcmp(getter_actual, getter_prefix, sizeof(getter_prefix))) return false;
    copy_source = reinterpret_cast<Copy>(const_cast<void*>(address));
    player_controller_class = reinterpret_cast<StaticClass>(image_base + 0x1c08560);
    player_plane_class = reinterpret_cast<StaticClass>(image_base + 0x938fb0);
    return true;
}

std::uintptr_t ResolveAfterburnerSource(std::uintptr_t rumble) {
    const PreserveLastError error;
    if (!rumble || !player_controller_class || !player_plane_class) return 0;
    std::uintptr_t controller = 0, plane = 0;
    // Native secondary-this is rumble+0x28: [this-8] -> PlayerController -> PlayerPlane.
    if (!Read(reinterpret_cast<const void*>(rumble + 0x20), &controller, sizeof(controller)) ||
        !controller || !IsA(controller, player_controller_class()) ||
        !Read(reinterpret_cast<const void*>(controller + 0x370), &plane, sizeof(plane)) ||
        !plane || !IsA(plane, player_plane_class())) return 0;
    return plane; // Resolve afresh, without reading or modifying the native +0x50 cache.
}

bool CaptureNativeInput(const void* source, RumbleInput& input) {
    const PreserveLastError error;
    input = {};
    if (!copy_source) return false;
    void* object = nullptr;
    bool empty = false;
    if (!Resolve(source, object, empty)) return false;
    if (empty) return true;
    auto captured = std::make_shared<CapturedInput>();
    captured->copy = copy_source;
    captured->copy(&captured->storage, source);
    captured->cloned = true;
    // Reject a failed clone, without retaining or freeing the caller's temporary wrapper.
    if (!Resolve(&captured->storage, object, empty) || empty) return false;
    input = [captured] { return captured->Sample(); };
    return true;
}
}
