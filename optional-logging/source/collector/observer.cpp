#include "channel.hpp"
#include "MinHook.h"
#include <cstring>
#include <cwchar>

// Optional collector. It neither imports the main ASI nor writes files or device output.
static icing::Channel* channel = nullptr;
static SRWLOCK writer_lock = SRWLOCK_INIT;
using Request = void(__fastcall*)(void*, std::uint8_t, float);
using StateRequest = void(__fastcall*)(void*, std::uint8_t, void*, float);
using Evaluate = void(__fastcall*)(void*, void*, float);
static Request original_request;
static StateRequest original_state_request;
static Evaluate original_evaluate;

static void Record(int event, void* object, int type, double value, int active = -1) {
    const DWORD saved = GetLastError();
    const auto heartbeat = InterlockedCompareExchange64(&channel->reader_heartbeat, 0, 0);
    if (heartbeat > 0 && static_cast<LONG64>(GetTickCount64()) - heartbeat <= 1000) {
        AcquireSRWLockExclusive(&writer_lock);
        const LONG64 ordinal = channel->written + 1;
        auto& entry = channel->entries[(ordinal - 1) % icing::Capacity];
        InterlockedExchange64(&entry.sequence, ordinal * 2 - 1);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        entry.qpc = now.QuadPart;
        entry.object = reinterpret_cast<std::uint64_t>(object);
        entry.value = value;
        entry.type = type;
        entry.event = event;
        entry.thread = GetCurrentThreadId();
        entry.active = active;
        InterlockedExchange64(&entry.sequence, ordinal * 2);
        InterlockedExchange64(&channel->written, ordinal);
        ReleaseSRWLockExclusive(&writer_lock);
    }
    SetLastError(saved);
}

static void __fastcall OnRequest(void* controller, std::uint8_t type, float scale) {
    Record(1, controller, type, scale);
    original_request(controller, type, scale);
}
static void __fastcall OnStateRequest(void* controller, std::uint8_t type, void* source, float scale) {
    Record(2, controller, type, scale);
    original_state_request(controller, type, source, scale);
}
static void __fastcall OnEvaluate(void* object, void* instance, float delta) {
    const auto heartbeat = InterlockedCompareExchange64(&channel->reader_heartbeat, 0, 0);
    if (heartbeat == 0 || static_cast<LONG64>(GetTickCount64()) - heartbeat > 1000) {
        original_evaluate(object, instance, delta); return;
    }
    const auto type = *static_cast<const std::uint8_t*>(instance);
    Record(3, instance, type, delta);
    original_evaluate(object, instance, delta);
    // Native evaluation does not free the instance; preserve game LastError around the observer.
    const DWORD saved = GetLastError();
    float value = 0;
    std::uint8_t active = 0;
    SIZE_T copied = 0;
    if (ReadProcessMemory(GetCurrentProcess(), static_cast<const std::uint8_t*>(instance) + 0x54, &value, sizeof(value), &copied) && copied == sizeof(value) &&
        ReadProcessMemory(GetCurrentProcess(), static_cast<const std::uint8_t*>(instance) + 0x58, &active, sizeof(active), &copied) && copied == sizeof(active))
        Record(4, instance, type, value, active);
    SetLastError(saved);
}

struct Target { std::uint32_t rva; const char* prefix; void* detour; void** original; };

static DWORD WINAPI Start(void*) {
    wchar_t name[80];
    swprintf_s(name, L"Local\\AC7IcingEvents-%lu", GetCurrentProcessId());
    const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(icing::Channel), name);
    if (!mapping) return 1;
    channel = static_cast<icing::Channel*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(icing::Channel)));
    if (!channel) { CloseHandle(mapping); return 1; }
    channel->magic = icing::Magic;
    channel->version = 1;
    channel->pid = GetCurrentProcessId();
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    channel->frequency = frequency.QuadPart;

    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    Target targets[] = {
        {0x3dfdd0, "\x40\x53\x56\x57\x48\x81\xec\x80\x00\x00\x00\x0f", reinterpret_cast<void*>(OnRequest), reinterpret_cast<void**>(&original_request)},
        {0x3dfee0, "\x48\x89\x5c\x24\x08\x48\x89\x74\x24\x10\x57\x48", reinterpret_cast<void*>(OnStateRequest), reinterpret_cast<void**>(&original_state_request)},
        {0x3c7390, "\x40\x53\x41\x54\x41\x56\x48\x83\xec\x40\x44\x0f", reinterpret_cast<void*>(OnEvaluate), reinterpret_cast<void**>(&original_evaluate)}
    };
    if (pe->FileHeader.TimeDateStamp != 1667396750 || pe->OptionalHeader.SizeOfImage != 71409664 || pe->OptionalHeader.CheckSum != 65142356) {
        InterlockedExchange(&channel->status, -1); return 1;
    }
    for (const auto& target : targets) {
        if (std::memcmp(base + target.rva, target.prefix, 12)) { InterlockedExchange(&channel->status, -1); return 1; }
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Start), &pinned)) { InterlockedExchange(&channel->status, -2); return 1; }
    if (MH_Initialize() != MH_OK) { InterlockedExchange(&channel->status, -2); return 1; }
    for (const auto& target : targets) {
        auto status = MH_CreateHook(base + target.rva, target.detour, target.original);
        if (status == MH_OK) status = MH_QueueEnableHook(base + target.rva);
        if (status != MH_OK) {
            for (const auto& own : targets) MH_RemoveHook(base + own.rva);
            InterlockedExchange(&channel->status, -2); return 1;
        }
    }
    if (MH_ApplyQueued() != MH_OK) {
        for (const auto& target : targets) MH_DisableHook(base + target.rva);
        InterlockedExchange(&channel->status, -2); return 1;
    }
    InterlockedExchange(&channel->status, 1);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
