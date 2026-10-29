#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <vector>
#include "settings.hpp"

namespace ac7 {
struct RumblePoint { float position; float amplitude; };
struct RumbleCurve {
    std::vector<RumblePoint> points;
    bool loops = false;
    std::size_t loop_index = 0;
};
struct RumbleState {
    float position = 0;
    float value = 0;
    float scale = 1;
    bool active = true;
};

struct AfterburnerInput { float value; float fade_rate; };
struct RumbleOutput { double amplitude = 0; int type = -1; bool paused = false; };

// available=false is a read/ownership failure; an available empty axis follows native validity fallback.
struct RumbleInputSample {
    bool available = true;
    std::optional<float> axis;
};
// No game calls or device I/O in the core. The callback owns its captured provider.
using RumbleInput = std::function<RumbleInputSample()>;
class RumblePlayback {
public:
    explicit RumblePlayback(std::uint32_t seed = 5489u) : random_(seed) {}
    bool Notify(std::uint8_t type, const RumbleCurve& curve, double scale, RumbleInput input = {},
                const RumbleCurve* cloud_alternate = nullptr);
    void Stop(std::uint8_t type);
    void SetScale(std::uint8_t type, double scale);
    void Pause();
    void BeginResume();
    void EndResume();
    void StopAll();
    void Clear();
    void Tick(double delta, std::optional<AfterburnerInput> afterburner = std::nullopt);
    void SetGains(const RumbleGains& gains) { gains_ = gains; }
    RumbleOutput Sample() const;
    std::optional<float> AfterburnerGain() const { return afterburner_gain_; }
private:
    struct Effect {
        std::uint8_t type;
        RumbleCurve curve;
        RumbleState state;
        RumbleInput input;
        std::optional<RumbleCurve> cloud_alternate;
    };
    std::vector<Effect> effects_;
    std::mt19937 random_;
    std::optional<float> afterburner_gain_;
    RumbleGains gains_;
    bool paused_ = true;
    bool resuming_ = false;
};
}
