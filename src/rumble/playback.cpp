#include "playback.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
#include <limits>

namespace ac7 {
namespace {
bool ValidCurve(const RumbleCurve& curve) {
    if (curve.points.size() < 2 || curve.points.size() > 4096) return false;
    // Native point order may be non-monotonic (ExitCloudCameraShake); never sort it.
    for (const auto& point : curve.points) {
        if (!std::isfinite(point.position) || !std::isfinite(point.amplitude) ||
            point.position < 0 || point.amplitude < 0) return false;
    }
    return true;
}

// Curves remain in native point order. Callers supply live input and any extra multiplier.
bool EvaluateRumble(const RumbleCurve& curve, RumbleState& state, float delta,
                    std::optional<float> input, float multiplier = 1) {
    if (!state.active) return true;
    if (!ValidCurve(curve) || !std::isfinite(delta) || delta < 0 ||
        !std::isfinite(state.position) || state.position < 0 ||
        !std::isfinite(state.scale) || state.scale < 0 ||
        !std::isfinite(multiplier) || (input && !std::isfinite(*input))) {
        state.active = false; state.value = 0; return false;
    }
    const auto& points = curve.points;
    const float axis = input ? *input : state.position;
    std::size_t left = 0;
    while (left + 1 < points.size() && axis > points[left + 1].position) ++left;
    float value = 0;
    if (left == points.size() - 1) {
        if (input) {
            value = points[left].amplitude;
        } else if (curve.loops) {
            const auto index = curve.loop_index < points.size() ? curve.loop_index : 0;
            state.position = points[index].position;
            value = points[index].amplitude;
        } else {
            // Native leaves a stale value when ending; consumers must honor active.
            state.active = false;
            return true;
        }
    } else {
        const auto& a = points[left];
        const auto& b = points[left + 1];
        const float width = b.position - a.position;
        const float ratio = std::fabs(b.position - axis) <= 1e-8f || std::fabs(width) <= 1e-8f
            ? 1.0f : (axis - a.position) / width;
        value = (b.amplitude - a.amplitude) * ratio + a.amplitude;
    }
    state.value = (value * state.scale) * multiplier;
    state.position += delta;
    if (!std::isfinite(state.value) || !std::isfinite(state.position)) {
        state.active = false; state.value = 0; return false;
    }
    return true;
}

bool EvaluateCloudRumble(const RumbleCurve& curve, const RumbleCurve& alternate,
                         std::uint8_t& type, RumbleState& state, float delta,
                         std::optional<float> input, std::uint16_t random_sample) {
    if (!state.active) return true;
    if ((type != 24 && type != 25) || !ValidCurve(curve) || !ValidCurve(alternate) ||
        !std::isfinite(delta) || delta < 0 || !std::isfinite(state.position) || state.position < 0 ||
        !std::isfinite(state.scale) || state.scale < 0 || (input && !std::isfinite(*input)) ||
        random_sample > 32767) {
        state.active = false; state.value = 0; return false;
    }
    if (state.position >= 5.0f) {
        const auto next = static_cast<std::uint8_t>(random_sample * (1.0f / 32767.0f) < 0.5f ? 25 : 24);
        const auto& selected = next == type ? curve : alternate;
        type = next;
        state.value = selected.points.front().amplitude * state.scale;
        state.position = delta;
        if (!std::isfinite(state.value)) { state.active = false; state.value = 0; return false; }
        return true;
    }
    // Cloud endpoints hold until the special five-second branch, not an ordinary time loop.
    return EvaluateRumble(curve, state, delta, input ? input : std::optional<float>(state.position));
}

}

bool RumblePlayback::Notify(std::uint8_t type, const RumbleCurve& curve, double scale, RumbleInput input,
                            const RumbleCurve* cloud_alternate) {
    // Resume may synchronously resubmit effects; preserve the saved independent state.
    if (paused_ || resuming_ || type >= 46 || effects_.size() >= 16 ||
        !std::isfinite(scale) || scale < 0 || !std::isfinite(static_cast<float>(scale)) ||
        !ValidCurve(curve)) return false;
    const bool cloud = type == 24 || type == 25;
    if (cloud && (!cloud_alternate || !ValidCurve(*cloud_alternate))) return false;
    if (cloud && std::any_of(effects_.begin(), effects_.end(), [](const Effect& effect) {
        return effect.type == 24 || effect.type == 25;
    })) {
        SetScale(24, scale); SetScale(25, scale);
        return true; // Shared A/B instance: preserve age, current type and provider.
    }
    effects_.push_back({type, curve, {0, 0, static_cast<float>(scale), true}, std::move(input),
                        cloud ? std::optional<RumbleCurve>(*cloud_alternate) : std::nullopt});
    return true;
}

void RumblePlayback::Stop(std::uint8_t type) {
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
        [type](const Effect& effect) { return effect.type == type; }), effects_.end());
}

void RumblePlayback::SetScale(std::uint8_t type, double scale) {
    if (!std::isfinite(scale) || scale < 0 || !std::isfinite(static_cast<float>(scale))) { Stop(type); return; }
    for (auto& effect : effects_) {
        // Native setter changes Scale, not position or the last evaluated value.
        if (effect.type == type) effect.state.scale = static_cast<float>(scale);
    }
}

void RumblePlayback::Pause() { paused_ = true; }
void RumblePlayback::BeginResume() { resuming_ = true; paused_ = true; }
void RumblePlayback::EndResume() {
    resuming_ = false;
    paused_ = false;
    for (auto& effect : effects_) effect.state.value = 0;
}
void RumblePlayback::StopAll() { effects_.clear(); }
void RumblePlayback::Clear() {
    effects_.clear();
    afterburner_gain_.reset();
    resuming_ = false;
}

RumbleOutput RumblePlayback::Sample() const {
    RumbleOutput result;
    result.paused = paused_ || resuming_;
    if (result.paused || gains_.global == 0) return result;
    double value = 0;
    for (const auto& effect : effects_) {
        if (!effect.state.active || effect.state.value <= 0 || gains_.effects[effect.type] == 0) continue;
        double contribution = (static_cast<double>(effect.state.value) * gains_.global) * gains_.effects[effect.type];
        // Huge finite settings saturate; the device range is clipped only after winner selection.
        if (!std::isfinite(contribution)) contribution = std::numeric_limits<double>::max();
        if (contribution > value || (value > 0 && contribution == value && effect.type < result.type)) {
            value = contribution;
            result.type = effect.type; // Exact ties use the lowest type, independently of insertion order.
        }
    }
    result.amplitude = value;
    return result;
}

void RumblePlayback::Tick(double delta, std::optional<AfterburnerInput> afterburner) {
    if (!std::isfinite(delta) || delta < 0 || !std::isfinite(static_cast<float>(delta))) { Clear(); return; }
    if (paused_ || resuming_) return;
    const float step = static_cast<float>(delta);
    if (!afterburner || !std::isfinite(afterburner->value) || !std::isfinite(afterburner->fade_rate)) {
        afterburner_gain_.reset();
    } else if (afterburner->value > 0) {
        afterburner_gain_ = 1.0f;
    } else if (afterburner_gain_) {
        *afterburner_gain_ -= step * afterburner->fade_rate;
        if (!std::isfinite(*afterburner_gain_)) afterburner_gain_.reset();
    }
    for (auto& effect : effects_) {
        std::optional<float> input;
        if (effect.input) {
            try {
                const auto sample = effect.input();
                if (!sample.available) { effect.state.active = false; continue; }
                input = sample.axis;
            } catch (...) { effect.state.active = false; continue; }
        }
        if (effect.cloud_alternate) {
            const auto previous_type = effect.type;
            const auto sample = effect.state.position >= 5.0f
                ? static_cast<std::uint16_t>(random_() & 32767u) : std::uint16_t{0};
            EvaluateCloudRumble(effect.curve, *effect.cloud_alternate, effect.type, effect.state, step, input, sample);
            if (effect.type != previous_type) std::swap(effect.curve, *effect.cloud_alternate);
        } else if (effect.type == 0) {
            if (!afterburner_gain_) { effect.state.active = false; continue; }
            EvaluateRumble(effect.curve, effect.state, step, input, *afterburner_gain_);
        } else {
            EvaluateRumble(effect.curve, effect.state, step, input);
        }
    }
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
        [](const Effect& effect) { return !effect.state.active; }), effects_.end());
}
}
