#include "aim_follow_controller.hpp"

#include <algorithm>
#include <cmath>

namespace aim_follow {

TargetSelector::TargetSelector(const TargetSelectorConfig &config)
    : cfg_(config) {}

void TargetSelector::reset() {
    has_last_target_ = false;
    last_center_x_ = 0.0f;
    last_center_y_ = 0.0f;
    last_area_ = 0.0f;
    lost_frames_ = 0;
}

void TargetSelector::setConfig(const TargetSelectorConfig &config) {
    cfg_ = config;
    reset();
}

const TargetSelectorConfig &TargetSelector::config() const {
    return cfg_;
}

int TargetSelector::select(const std::vector<TargetCandidate> &candidates) {
    if (candidates.empty()) {
        ++lost_frames_;
        if (lost_frames_ > cfg_.max_lost_frames) {
            reset();
        }
        return -1;
    }

    int largest_pos = 0;
    for (int i = 1; i < static_cast<int>(candidates.size()); ++i) {
        if (candidates[i].area > candidates[largest_pos].area) {
            largest_pos = i;
        }
    }

    int selected_pos = largest_pos;
    if (has_last_target_ && lost_frames_ <= cfg_.max_lost_frames) {
        int nearest_pos = 0;
        float nearest_dist = normalizedDistance(candidates[0].center_x, candidates[0].center_y);
        for (int i = 1; i < static_cast<int>(candidates.size()); ++i) {
            const float dist = normalizedDistance(candidates[i].center_x, candidates[i].center_y);
            if (dist < nearest_dist) {
                nearest_dist = dist;
                nearest_pos = i;
            }
        }

        const bool nearest_is_close = nearest_dist <= cfg_.max_center_jump_norm;
        const bool largest_is_much_bigger =
            candidates[largest_pos].area > std::max(1.0f, last_area_) * cfg_.area_switch_ratio;
        selected_pos = (nearest_is_close && !largest_is_much_bigger) ? nearest_pos : largest_pos;
    }

    remember(candidates[selected_pos]);
    lost_frames_ = 0;
    return candidates[selected_pos].index;
}

float TargetSelector::normalizedDistance(float x, float y) const {
    const float dx = x - last_center_x_;
    const float dy = y - last_center_y_;
    const float diag = std::sqrt(cfg_.frame_width * cfg_.frame_width +
                                 cfg_.frame_height * cfg_.frame_height);
    if (diag <= 1.0f) {
        return 1.0f;
    }
    return std::sqrt(dx * dx + dy * dy) / diag;
}

void TargetSelector::remember(const TargetCandidate &candidate) {
    has_last_target_ = true;
    last_center_x_ = candidate.center_x;
    last_center_y_ = candidate.center_y;
    last_area_ = candidate.area;
}

MonocularDistanceEstimator::MonocularDistanceEstimator(const DistanceEstimatorConfig &config)
    : cfg_(config) {}

void MonocularDistanceEstimator::reset() {
    filtered_distance_m_ = -1.0f;
}

void MonocularDistanceEstimator::setConfig(const DistanceEstimatorConfig &config) {
    cfg_ = config;
    reset();
}

const DistanceEstimatorConfig &MonocularDistanceEstimator::config() const {
    return cfg_;
}

DistanceEstimate MonocularDistanceEstimator::update(float box_width_px) {
    DistanceEstimate estimate;
    if (box_width_px < cfg_.min_box_width_px ||
        cfg_.target_real_width_m <= 0.0f ||
        cfg_.focal_length_px <= 0.0f ||
        !std::isfinite(box_width_px)) {
        estimate.filtered_distance_m = filtered_distance_m_;
        return estimate;
    }

    const float raw_distance = cfg_.target_real_width_m * cfg_.focal_length_px / box_width_px;
    if (raw_distance <= 0.0f || !std::isfinite(raw_distance)) {
        estimate.filtered_distance_m = filtered_distance_m_;
        return estimate;
    }

    const float alpha = clampAlpha(cfg_.filter_alpha);
    if (filtered_distance_m_ <= 0.0f || !std::isfinite(filtered_distance_m_)) {
        filtered_distance_m_ = raw_distance;
    } else {
        filtered_distance_m_ = alpha * raw_distance + (1.0f - alpha) * filtered_distance_m_;
    }

    estimate.valid = true;
    estimate.raw_distance_m = raw_distance;
    estimate.filtered_distance_m = filtered_distance_m_;
    return estimate;
}

float MonocularDistanceEstimator::clampAlpha(float alpha) const {
    if (!std::isfinite(alpha)) {
        return 1.0f;
    }
    return std::clamp(alpha, 0.0f, 1.0f);
}

AimFollowController::AimFollowController(const ControlConfig &config)
    : cfg_(config),
      last_yaw_(config.center_yaw),
      last_pitch_(config.center_pitch) {}

void AimFollowController::reset() {
    has_last_error_ = false;
    last_error_x_ = 0.0f;
    last_error_y_ = 0.0f;
    last_time_s_ = 0.0f;
    lost_frames_ = 0;
    last_yaw_ = cfg_.center_yaw;
    last_pitch_ = cfg_.center_pitch;
}

void AimFollowController::setConfig(const ControlConfig &config) {
    cfg_ = config;
    reset();
}

const ControlConfig &AimFollowController::config() const {
    return cfg_;
}

ControlOutput AimFollowController::update(const TargetObservation &obs) {
    ControlOutput out;
    out.pitch = last_pitch_;
    out.yaw = last_yaw_;

    if (!obs.valid) {
        ++lost_frames_;
        has_last_error_ = false;
        out.target_valid = false;

        if (lost_frames_ >= cfg_.lost_hold_frames) {
            out.motor1_rpm = 0;
            out.motor2_rpm = 0;
            out.yaw = stepLimit(last_yaw_, cfg_.center_yaw);
            last_yaw_ = out.yaw;
        }
        return out;
    }

    lost_frames_ = 0;
    out.target_valid = true;

    const float error_x = normalizeError(obs.center_x - cfg_.frame_width * 0.5f,
                                         cfg_.frame_width * 0.5f);
    const float error_y = normalizeError(obs.center_y - cfg_.frame_height * 0.5f,
                                         cfg_.frame_height * 0.5f);
    out.norm_error_x = error_x;
    out.norm_error_y = error_y;

    float dt = 0.05f;
    if (has_last_error_ && obs.timestamp_s > last_time_s_) {
        dt = std::clamp(obs.timestamp_s - last_time_s_, 0.02f, 0.20f);
    }

    out.yaw = calcAxisCommand(error_x,
                              has_last_error_ ? last_error_x_ : error_x,
                              dt,
                              cfg_.yaw_kp,
                              cfg_.yaw_kd,
                              cfg_.center_yaw,
                              last_yaw_,
                              cfg_.min_yaw,
                              cfg_.max_yaw,
                              cfg_.invert_yaw);
    out.pitch = calcAxisCommand(error_y,
                                has_last_error_ ? last_error_y_ : error_y,
                                dt,
                                cfg_.pitch_kp,
                                cfg_.pitch_kd,
                                cfg_.center_pitch,
                                last_pitch_,
                                cfg_.min_pitch,
                                cfg_.max_pitch,
                                cfg_.invert_pitch);

    float distance_error = 0.0f;
    const int follow_rpm = calcFollowRpm(obs.distance_m, &distance_error);
    out.distance_valid = obs.distance_m > 0.0f && std::isfinite(obs.distance_m);
    out.distance_error_m = distance_error;
    out.motor1_rpm = clampInt(follow_rpm * cfg_.motor1_forward_sign,
                              cfg_.motor_rpm_min,
                              cfg_.motor_rpm_max);
    out.motor2_rpm = clampInt(follow_rpm * cfg_.motor2_forward_sign,
                              cfg_.motor_rpm_min,
                              cfg_.motor_rpm_max);

    last_error_x_ = error_x;
    last_error_y_ = error_y;
    last_time_s_ = obs.timestamp_s;
    has_last_error_ = true;
    last_yaw_ = out.yaw;
    last_pitch_ = out.pitch;

    return out;
}

int AimFollowController::clampInt(int value, int lo, int hi) const {
    return std::max(lo, std::min(value, hi));
}

int AimFollowController::stepLimit(int previous, int desired) const {
    const int max_step = std::max(1, static_cast<int>(std::round(cfg_.max_cmd_step)));
    if (desired > previous + max_step) {
        return previous + max_step;
    }
    if (desired < previous - max_step) {
        return previous - max_step;
    }
    return desired;
}

float AimFollowController::normalizeError(float value, float half_frame_size) const {
    if (half_frame_size <= 1.0f || !std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value / half_frame_size, -1.0f, 1.0f);
}

int AimFollowController::calcAxisCommand(float error,
                                         float last_error,
                                         float dt,
                                         float kp,
                                         float kd,
                                         int center,
                                         int previous,
                                         int lo,
                                         int hi,
                                         bool invert) const {
    const float e = (std::fabs(error) < cfg_.aim_deadzone_norm) ? 0.0f : error;
    const float derivative = (dt > 1e-3f) ? (e - last_error) / dt : 0.0f;
    const float sign = invert ? -1.0f : 1.0f;
    const int desired = center + static_cast<int>(std::round(sign * (kp * e + kd * derivative)));
    return clampInt(stepLimit(previous, desired), lo, hi);
}

int AimFollowController::calcFollowRpm(float distance_m, float *distance_error_out) const {
    if (distance_error_out) {
        *distance_error_out = 0.0f;
    }
    if (distance_m <= 0.0f || !std::isfinite(distance_m)) {
        return 0;
    }

    const float error = distance_m - cfg_.target_distance_m;
    if (distance_error_out) {
        *distance_error_out = error;
    }
    if (std::fabs(error) <= cfg_.distance_deadband_m) {
        return 0;
    }

    int rpm = static_cast<int>(std::round(cfg_.follow_kp_rpm_per_m * error));
    if (rpm > 0) {
        rpm = std::max(cfg_.min_follow_rpm, rpm);
    } else if (rpm < 0) {
        rpm = std::min(-cfg_.min_follow_rpm, rpm);
    }

    return clampInt(rpm, -cfg_.max_follow_rpm, cfg_.max_follow_rpm);
}

} // namespace aim_follow
