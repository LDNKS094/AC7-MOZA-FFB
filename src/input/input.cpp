#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dinput.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include "MinHook.h"

namespace {
struct State {
    LONG axes[8]{};
    DWORD pov[4]{};
    BYTE buttons[128]{};
};
static_assert(sizeof(State) == 176 && offsetof(State, buttons) == 48);
struct Device {
    GUID instance{};
    State state{};
    std::array<bool, 128> present{};
    bool fresh = false;
};
// Game callbacks can nest (frame -> poll -> release); retain one coherent device/frame table.
std::recursive_mutex gate;
std::unordered_map<void*, Device> devices;
std::array<DIOBJECTDATAFORMAT, 140> objects;
DIDATAFORMAT format;
using Initialize = bool(__fastcall*)(void*, const void*, void*, HWND, bool);
using Poll = bool(__fastcall*)(void*);
using Frame = void(__fastcall*)(void*);
using Button = BYTE(__fastcall*)(const void*, const BYTE*, bool);
using Release = void(__fastcall*)(void*);
using Destroy = void(__fastcall*)(void*);
Initialize original_initialize;
Poll original_poll;
Frame original_frame;
Button original_button;
Release original_release;
Destroy original_destroy;

template<class T> T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<BYTE*>(object) + offset);
}
IDirectInputDevice8W* Handle(void* object) { return Field<IDirectInputDevice8W*>(object, 0); }
void Message(const wchar_t* message) { OutputDebugStringW(message); }

void MakeFormat() {
    const GUID* axes[] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis,
        &GUID_RyAxis, &GUID_RzAxis, &GUID_Slider, &GUID_Slider};
    constexpr DWORD optional = 0x80000000; // DirectInput optional-object flag used by c_dfDIJoystick.
    for (DWORD i = 0; i < 8; ++i)
        objects[i] = {axes[i], i * 4, optional | DIDFT_ANYINSTANCE | DIDFT_AXIS, 0};
    for (DWORD i = 0; i < 4; ++i)
        objects[8 + i] = {&GUID_POV, 32 + i * 4, optional | DIDFT_MAKEINSTANCE(i) | DIDFT_POV, 0};
    for (DWORD i = 0; i < 128; ++i)
        objects[12 + i] = {&GUID_Button, 48 + i, optional | DIDFT_MAKEINSTANCE(i) | DIDFT_BUTTON, 0};
    format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS,
        sizeof(State), static_cast<DWORD>(objects.size()), objects.data()};
}
BOOL CALLBACK EnumerateButtons(const DIDEVICEOBJECTINSTANCEW* object, void* context) {
    const DWORD index = DIDFT_GETINSTANCE(object->dwType);
    if ((object->dwType & DIDFT_BUTTON) && index < 128)
        static_cast<Device*>(context)->present[index] = true;
    return DIENUM_CONTINUE;
}

bool __fastcall OnInitialize(void* object, const void* instance, void* input, HWND window, bool background) {
    std::lock_guard<std::recursive_mutex> lock(gate);
    if (Handle(object) && devices.find(object) != devices.end()) return true;
    if (!original_initialize(object, instance, input, window, background)) return false;
    auto* handle = Handle(object);
    Device device;
    std::memcpy(&device.instance, static_cast<BYTE*>(object) + 0x20, sizeof(GUID));
    // This object has not been acquired yet; original properties and device configuration stay in place.
    if (FAILED(handle->SetDataFormat(&format)) ||
        FAILED(handle->EnumObjects(EnumerateButtons, &device, DIDFT_BUTTON))) {
        original_release(object);
        Message(L"[native-input] device initialization failed; full button format unavailable\n");
        return false;
    }
    try {
        devices.insert_or_assign(object, device);
    } catch (...) {
        original_release(object);
        Message(L"[native-input] device initialization failed; state allocation unavailable\n");
        return false;
    }
    return true;
}

bool __fastcall OnPoll(void* object) {
    std::lock_guard<std::recursive_mutex> lock(gate);
    const auto found = devices.find(object);
    if (found == devices.end()) return false;
    auto& device = found->second;
    device.fresh = false;
    auto* handle = Handle(object);
    BYTE* destination = static_cast<BYTE*>(object) + 0xb0 + Field<DWORD>(object, 0x150) * 80;
    const auto clear = [&] { std::memset(destination, 0, 80); };
    if (!handle) { clear(); return false; }
    auto& acquired = Field<BYTE>(object, 0x154);
    if (!acquired) {
        const HRESULT result = handle->Acquire();
        if (FAILED(result)) {
            clear();
            if (result == DIERR_UNPLUGGED) {
                devices.erase(found);
                original_release(object);
            }
            return false;
        }
        acquired = 1;
    }
    // Preserve the native skip-poll branch; do not publish a synthetic fresh button state.
    if (Field<BYTE>(object, 0x155)) return true;
    HRESULT result = handle->Poll();
    if (result == DIERR_INPUTLOST) {
        acquired = 0;
        result = handle->Acquire();
        if (FAILED(result)) { clear(); return false; }
        acquired = 1;
        result = handle->Poll();
        if (FAILED(result)) { clear(); return false; }
    }
    State state{};
    result = handle->GetDeviceState(sizeof(state), &state);
    if (result == DIERR_INPUTLOST) {
        acquired = 0;
        result = handle->Acquire();
        if (FAILED(result)) { clear(); return false; }
        acquired = 1;
        result = handle->GetDeviceState(sizeof(state), &state);
    }
    if (FAILED(result)) { clear(); return false; }
    std::memcpy(destination, &state, 80);
    device.state = state;
    device.fresh = true;
    return true;
}

void __fastcall OnFrame(void* manager) {
    std::lock_guard<std::recursive_mutex> lock(gate);
    for (auto& item : devices) item.second.fresh = false;
    original_frame(manager);
}
BYTE __fastcall OnButton(const void* snapshot, const BYTE* token, bool recursive) {
    if (token[0] != 12) return original_button(snapshot, token, recursive);
    const unsigned index = token[1];
    if (index >= 128) return 0; // State memory boundary; unsupported config has no defined mapping.
    GUID instance;
    std::memcpy(&instance, static_cast<const BYTE*>(snapshot) + 0x68, sizeof(instance));
    std::lock_guard<std::recursive_mutex> lock(gate);
    for (const auto& item : devices) {
        const auto& device = item.second;
        if (IsEqualGUID(instance, device.instance))
            return device.fresh && device.present[index] ? device.state.buttons[index] : 0;
    }
    return 0;
}
void __fastcall OnRelease(void* object) {
    std::lock_guard<std::recursive_mutex> lock(gate);
    devices.erase(object);
    original_release(object);
}
void __fastcall OnDestroy(void* wrapper) {
    std::lock_guard<std::recursive_mutex> lock(gate);
    devices.erase(Field<void*>(wrapper, 0x10));
    original_destroy(wrapper);
}
struct Target { DWORD rva; const char* prefix; void* detour; void** original; };

DWORD WINAPI Start(void*) {
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    Target targets[] = {
        {0x20f53e0, "\x40\x53\x56\x57\x48\x83\xec\x70\x48\x8b\x05\x11", reinterpret_cast<void*>(OnInitialize), reinterpret_cast<void**>(&original_initialize)},
        {0x20f7f40, "\x40\x53\x48\x83\xec\x20\x48\x8b\xd9\x48\x8b\x09", reinterpret_cast<void*>(OnPoll), reinterpret_cast<void**>(&original_poll)},
        {0x20f3fc0, "\x48\x8b\xc4\x55\x48\x81\xec\xe0\x00\x00\x00\x80", reinterpret_cast<void*>(OnFrame), reinterpret_cast<void**>(&original_frame)},
        {0x20f4b20, "\x48\x83\xec\x28\x44\x0f\xb6\x0a\x45\x84\xc9\x0f", reinterpret_cast<void*>(OnButton), reinterpret_cast<void**>(&original_button)},
        {0x20f4220, "\x40\x53\x48\x83\xec\x20\x48\x8b\xd9\x48\x8b\x09", reinterpret_cast<void*>(OnRelease), reinterpret_cast<void**>(&original_release)},
        {0x20f3db0, "\x40\x53\x48\x83\xec\x20\x48\x8b\x59\x10\x48\x85", reinterpret_cast<void*>(OnDestroy), reinterpret_cast<void**>(&original_destroy)}
    };
    if (pe->FileHeader.TimeDateStamp != 1667396750 || pe->OptionalHeader.SizeOfImage != 71409664 ||
        pe->OptionalHeader.CheckSum != 65142356) {
        Message(L"[native-input] unsupported game build\n"); return 1;
    }
    for (const auto& target : targets)
        if (std::memcmp(base + target.rva, target.prefix, 12)) {
            Message(L"[native-input] hook bytes differ; input extension not installed\n"); return 1;
        }
    auto* limit = base + 0x20f889d;
    if (std::memcmp(limit - 3, "\x80\x7a\x01\x20", 4)) return 1;
    MakeFormat();
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Start), &pinned) || MH_Initialize() != MH_OK) return 1;
    for (const auto& target : targets) {
        if (MH_CreateHook(base + target.rva, target.detour, target.original) != MH_OK ||
            MH_QueueEnableHook(base + target.rva) != MH_OK) {
            for (const auto& own : targets) MH_RemoveHook(base + own.rva);
            Message(L"[native-input] hook setup failed\n"); return 1;
        }
    }
    DWORD protection;
    if (!VirtualProtect(limit, 1, PAGE_EXECUTE_READWRITE, &protection)) {
        for (const auto& target : targets) MH_RemoveHook(base + target.rva);
        return 1;
    }
    if (MH_ApplyQueued() != MH_OK) {
        for (const auto& target : targets) MH_DisableHook(base + target.rva);
        DWORD ignored;
        VirtualProtect(limit, 1, protection, &ignored);
        Message(L"[native-input] hook activation failed\n"); return 1;
    }
    *limit = 128;
    FlushInstructionCache(GetCurrentProcess(), limit, 1);
    DWORD ignored;
    VirtualProtect(limit, 1, protection, &ignored);
    Message(L"[native-input] ready; native Button1-128 per device\n");
    return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        const HANDLE thread = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
