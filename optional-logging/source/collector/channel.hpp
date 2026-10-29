#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace icing {
constexpr std::uint32_t Magic = 0x474c3741;
constexpr std::size_t Capacity = 4096;
struct Entry {
    volatile LONG64 sequence;
    LONG64 qpc;
    std::uint64_t object;
    double value;
    std::int32_t type, event;
    std::uint32_t thread;
    std::int32_t active;
};
struct Channel {
    std::uint32_t magic, version, pid;
    volatile LONG status;
    volatile LONG64 written;
    LONG64 frequency;
    volatile LONG64 reader_heartbeat;
    std::uint8_t reserved[24];
    Entry entries[Capacity];
};
static_assert(sizeof(Entry) == 48 && offsetof(Channel, entries) == 64);
static_assert(offsetof(Entry, value) == 24 && offsetof(Channel, reader_heartbeat) == 32);
}
