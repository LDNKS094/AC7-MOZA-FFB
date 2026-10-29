#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "settings.hpp"
#include <cmath>
#include <locale>
#include <sstream>

namespace {
constexpr const wchar_t* Names[] = {
    L"Afterburner", L"AirBrake", L"Buffet", L"Stall", L"HighGTurn", L"GunFire",
    L"MissileSmallFire", L"MissileLargeFire", L"BombFire", L"RocketFire", L"RailgunFire",
    L"LaserFire", L"FlareFire", L"TakeOffBase", L"TakeOffCarrier", L"LandingBase",
    L"LandingCarrier", L"AerialRefuel", L"GunDamage", L"MissileDamage", L"PlaneCrashed",
    L"OwnPlaneDestroyed", L"CameraPlaneExplosion", L"EnteredClouds", L"InCloudsA",
    L"InCloudsB", L"Icing", L"AirCurrent", L"IGCTakeOff", L"IGCEquipped",
    L"ExitCloudCameraShake", L"BombImpact", L"SmallBombImpact", L"Explosion",
    L"GeneralPurposeS", L"GeneralPurposeM", L"GeneralPurposeL", L"HeliosDamage",
    L"EnemyExplosion", L"InAirCurrentCameraShake", L"Lightning", L"EMLBulletDamage",
    L"RectennaBarrierDamage", L"HeliosBombImpact", L"HeliosBombImpactLoop", L"SubmarineSearch"
};
static_assert(sizeof(Names) / sizeof(*Names) == 46);

double ReadGain(const wchar_t* path, const wchar_t* section, const wchar_t* key) {
    wchar_t text[128]{};
    const auto length = GetPrivateProfileStringW(section, key, L"1", text, 128, path);
    if (length >= 127) return 1;
    std::wistringstream input(text);
    input.imbue(std::locale::classic());
    double value = 1;
    if (!(input >> value) || !std::isfinite(value) || value < 0) return 1;
    input >> std::ws;
    return input.eof() ? value : 1;
}
}

ac7::RumbleGains ac7::LoadRumbleGains(const wchar_t* path) {
    RumbleGains gains;
    gains.global = ReadGain(path, L"Rumble", L"GlobalGain");
    for (std::size_t i = 0; i < gains.effects.size(); ++i)
        gains.effects[i] = ReadGain(path, L"Effects", Names[i]);
    return gains;
}
