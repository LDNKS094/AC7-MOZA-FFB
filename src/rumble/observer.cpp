#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "MinHook.h"
#include "preview.hpp"
#include "native_input.hpp"
#include "raw_channel.hpp"

// Core playback only. No log-only hooks, event stream or file output.
static ac7::RumblePreview preview;
using Notify = void(__fastcall*)(void*, std::uint8_t, void*, float);
using StopType = void(__fastcall*)(void*, std::uint8_t);
using StopTypes = void(__fastcall*)(void*, std::uint8_t, std::uint8_t);
using Control = void(__fastcall*)(void*);
using Tick = void(__fastcall*)(void*, float);
using Scale = void(__fastcall*)(void*, std::uint8_t, float);
using Scales = void(__fastcall*)(void*, std::uint8_t, std::uint8_t, float);
static Notify original_notify;
static StopType original_stop_type;
static StopTypes original_stop_types;
static Control original_pause, original_unpause, original_stop_all;
static Tick original_tick;
static Scale original_scale;
static Scales original_scales;
static void __fastcall OnNotify(void* object, std::uint8_t type, void* input_source, float scale) {
    // The caller owns this source storage; never retain its raw address.
    preview.Notify(object, type, input_source, scale);
    original_notify(object, type, input_source, scale);
}
static void __fastcall OnStopType(void* object, std::uint8_t type) {
    preview.Stop(object, type);
    original_stop_type(object, type);
}
static void __fastcall OnStopTypes(void* object, std::uint8_t first, std::uint8_t second) {
    preview.Stop(object, first);
    if (second != first) preview.Stop(object, second);
    original_stop_types(object, first, second);
}
static void __fastcall OnPause(void* object) {
    preview.Pause();
    original_pause(object);
}
static void __fastcall OnUnpause(void* object) {
    preview.BeginResume();
    original_unpause(object);
    preview.EndResume();
}
static void __fastcall OnStopAll(void* object) {
    preview.StopAll();
    original_stop_all(object);
}
static void __fastcall OnTick(void* secondary_this, float delta) {
    // This tickable interface is at +0x28 in the verified vibration object layout.
    void* object = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(secondary_this) - 0x28);
    original_tick(secondary_this, delta);
    preview.Tick(object, delta); // Preserve ordering; preview resolves its source independently of the native cache.
}
static void __fastcall OnScale(void* object, std::uint8_t type, float scale) {
    preview.Scale(object, type, scale);
    original_scale(object, type, scale);
}
static void __fastcall OnScales(void* object, std::uint8_t type1, std::uint8_t type2, float scale) {
    preview.Scale(object, type1, scale);
    preview.Scale(object, type2, scale);
    original_scales(object, type1, type2, scale);
}

struct Target {
    std::uint32_t rva;
    const char* prefix;
    void* detour;
    void** original;
};

static DWORD WINAPI Start(void*) {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto image_base = reinterpret_cast<std::uintptr_t>(base);
    preview.Initialize(PublishRawRumble); // Dedicated raw channel; no device ownership or output calls.
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (pe->FileHeader.TimeDateStamp != 1667396750 ||
        pe->OptionalHeader.SizeOfImage != 71409664 || pe->OptionalHeader.CheckSum != 65142356) {
        return 1;
    }
    Target targets[] = {
        {0x3c68c0, "\x40\x55\x56\x41\x54\x41\x55\x41\x57\x48\x8d\x6c", reinterpret_cast<void*>(OnNotify), reinterpret_cast<void**>(&original_notify)},
        {0x3c6f00, "\x48\x89\x5c\x24\x10\x48\x89\x6c\x24\x18\x48\x89", reinterpret_cast<void*>(OnStopType), reinterpret_cast<void**>(&original_stop_type)},
        {0x3c6d40, "\x48\x89\x5c\x24\x10\x48\x89\x6c\x24\x18\x56\x57", reinterpret_cast<void*>(OnStopTypes), reinterpret_cast<void**>(&original_stop_types)},
        {0x3e01d0, "\x48\x8b\x89\xd0\x06\x00\x00\x48\x85\xc9\x0f\x85", reinterpret_cast<void*>(OnPause), reinterpret_cast<void**>(&original_pause)},
        {0x3e06d0, "\x48\x8b\x89\xd0\x06\x00\x00\x48\x85\xc9\x0f\x85", reinterpret_cast<void*>(OnUnpause), reinterpret_cast<void**>(&original_unpause)},
        {0x3e0560, "\x40\x56\x48\x83\xec\x20\x48\x83\xb9\xd0\x06\x00", reinterpret_cast<void*>(OnStopAll), reinterpret_cast<void**>(&original_stop_all)},
        {0x3c70c0, "\x40\x53\x48\x83\xec\x50\x83\xb9\x18\x06\x00\x00", reinterpret_cast<void*>(OnTick), reinterpret_cast<void**>(&original_tick)},
        {0x3c6830, "\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x30\x0f\x29", reinterpret_cast<void*>(OnScale), reinterpret_cast<void**>(&original_scale)},
        {0x3c67a0, "\x48\x89\x5c\x24\x08\x48\x89\x74\x24\x10\x57\x48", reinterpret_cast<void*>(OnScales), reinterpret_cast<void**>(&original_scales)},
    };
    for (const auto& target : targets) {
        if (std::memcmp(base + target.rva, target.prefix, 12)) {
            return 1;
        }
    }
    if (!ac7::InitializeNativeInputs(image_base)) {
        return 1;
    }
    if (!OpenRawRumbleChannel()) {
        return 1;
    }
    // The ASI must remain mapped while game threads can execute its detours.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&Start), &pinned)) return 1;
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) return 1;
    for (const auto& target : targets) {
        status = MH_CreateHook(base + target.rva, target.detour, target.original);
        if (status == MH_OK) status = MH_QueueEnableHook(base + target.rva);
        if (status != MH_OK) {
            for (const auto& own : targets) MH_RemoveHook(base + own.rva);
            return 1;
        }
    }
    status = MH_ApplyQueued();
    if (status != MH_OK) {
        // Keep trampolines alive if another game thread already entered a detour.
        for (const auto& own : targets) MH_DisableHook(base + own.rva);
        return 1;
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        const HANDLE thread = CreateThread(nullptr, 0, Start, module, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
