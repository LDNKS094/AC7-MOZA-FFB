#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cwchar>
#include "raw_channel.hpp"

namespace {
struct Channel {
    volatile LONG sequence;
    DWORD magic;
    ULONGLONG timestamp;
    double amplitude;
    DWORD process;
    DWORD version;
    std::int32_t type;
    DWORD flags;
};
static_assert(sizeof(Channel) == 40 && offsetof(Channel, amplitude) == 16 &&
              offsetof(Channel, type) == 32 && offsetof(Channel, flags) == 36);
// Pinned ASI: the single mapping lives until process exit.
HANDLE mapping = nullptr;
Channel* channel = nullptr;
}

bool OpenRawRumbleChannel() {
    wchar_t name[96]{};
    if (swprintf_s(name, L"Local\\AC7NativeClassifiedRumble-%lu", GetCurrentProcessId()) < 0) return false;
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Channel), name);
    if (!mapping) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mapping); mapping = nullptr; return false; }
    channel = static_cast<Channel*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Channel)));
    if (!channel) { CloseHandle(mapping); mapping = nullptr; return false; }
    PublishRawRumble(0, -1, true);
    return true;
}

void PublishRawRumble(double amplitude, int type, bool paused) {
    if (!channel) return;
    const DWORD error = GetLastError();
    if (!std::isfinite(amplitude) || amplitude <= 0 || type < 0 || type >= 46 || type == 8 || paused) {
        amplitude = 0;
        type = -1;
    }
    InterlockedIncrement(&channel->sequence);
    channel->magic = 0x37435241; // ARC7: the sole current classified protocol.
    channel->timestamp = GetTickCount64();
    channel->amplitude = amplitude;
    channel->process = GetCurrentProcessId();
    channel->version = 1;
    channel->type = type;
    channel->flags = paused ? 1u : 0u;
    InterlockedIncrement(&channel->sequence);
    SetLastError(error);
}
