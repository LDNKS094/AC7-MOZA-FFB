#pragma once
#include <array>

namespace ac7 {
struct RumbleGains {
    double global = 1;
    std::array<double, 46> effects;
    RumbleGains() { effects.fill(1); }
};
RumbleGains LoadRumbleGains(const wchar_t* path);
}
