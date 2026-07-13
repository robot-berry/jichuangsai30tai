#include "aim_follow_controller.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

StableTrackIdMapper::StableTrackIdMapper(const StableTrackIdConfig &config)
    : cfg_(config) {}

void StableTrackIdMapper::reset() {
    frame_index_ = 0;
    next_stable_id_ = 1;
    memories_.clear();
}

std::vector<int> StableTrackIdMapper::update(
    const std::vector<TrackedTargetCandidate> &raw_tracks) {
    ++frame_index_;
    const int max_missing = std::max(0, cfg_.max_missing_frames);
    memories_.erase(
        std::remove_if(memories_.begin(), memories_.end(),
                       [&](const TrackMemory &memory) {
                           return frame_index_ - memory.last_seen_frame > max_missing;
                       }),
        memories_.end());

    std::vector<int> stable_ids(raw_tracks.size(), -1);
    std::vector<bool> memory_used(memories_.size(), false);

    for (int track_pos = 0; track_pos < static_cast<int>(raw_tracks.size()); ++track_pos) {
        for (int memory_pos = 0; memory_pos < static_cast<int>(memories_.size()); ++memory_pos) {
            if (!memory_used[memory_pos] &&
                memories_[memory_pos].raw_track_id == raw_tracks[track_pos].track_id) {
                stable_ids[track_pos] = memories_[memory_pos].stable_track_id;
                memory_used[memory_pos] = true;
                break;
            }
        }
    }

    for (int track_pos = 0; track_pos < static_cast<int>(raw_tracks.size()); ++track_pos) {
        if (stable_ids[track_pos] >= 0) {
            continue;
        }

        int best_memory = -1;
        float best_cost = std::numeric_limits<float>::max();
        for (int memory_pos = 0; memory_pos < static_cast<int>(memories_.size()); ++memory_pos) {
            if (memory_used[memory_pos] ||
                !areaCompatible(raw_tracks[track_pos].area, memories_[memory_pos].area)) {
                continue;
            }
            const float distance = normalizedDistance(raw_tracks[track_pos], memories_[memory_pos]);
            if (distance <= std::max(0.0f, cfg_.max_center_jump_norm) &&
                distance < best_cost) {
                best_cost = distance;
                best_memory = memory_pos;
            }
        }

        if (best_memory >= 0) {
            stable_ids[track_pos] = memories_[best_memory].stable_track_id;
            memory_used[best_memory] = true;
        } else {
            TrackMemory memory;
            memory.stable_track_id = next_stable_id_++;
            memories_.push_back(memory);
            memory_used.push_back(true);
            stable_ids[track_pos] = memory.stable_track_id;
        }
    }

    for (int track_pos = 0; track_pos < static_cast<int>(raw_tracks.size()); ++track_pos) {
        for (auto &memory : memories_) {
            if (memory.stable_track_id != stable_ids[track_pos]) {
                continue;
            }
            memory.raw_track_id = raw_tracks[track_pos].track_id;
            memory.center_x = raw_tracks[track_pos].center_x;
            memory.center_y = raw_tracks[track_pos].center_y;
            memory.area = raw_tracks[track_pos].area;
            memory.last_seen_frame = frame_index_;
            break;
        }
    }
    return stable_ids;
}

float StableTrackIdMapper::normalizedDistance(
    const TrackedTargetCandidate &track, const TrackMemory &memory) const {
    const float dx = track.center_x - memory.center_x;
    const float dy = track.center_y - memory.center_y;
    const float diagonal = std::sqrt(
        cfg_.frame_width * cfg_.frame_width + cfg_.frame_height * cfg_.frame_height);
    if (diagonal <= 1.0f) {
        return 1.0f;
    }
    return std::sqrt(dx * dx + dy * dy) / diagonal;
}

bool StableTrackIdMapper::areaCompatible(float current_area, float previous_area) const {
    if (current_area <= 1.0f || previous_area <= 1.0f) {
        return false;
    }
    const float ratio = std::max(current_area, previous_area) /
                        std::min(current_area, previous_area);
    return ratio <= std::max(1.0f, cfg_.max_area_ratio);
}

TrackedTargetSelector::TrackedTargetSelector(const TrackedTargetSelectorConfig &config)
    : cfg_(config) {}

void TrackedTargetSelector::reset() {
    locked_track_id_ = -1;
    missing_frames_ = 0;
}

int TrackedTargetSelector::select(const std::vector<TrackedTargetCandidate> &candidates) {
    if (locked_track_id_ >= 0) {
        for (const auto &candidate : candidates) {
            if (candidate.track_id == locked_track_id_) {
                missing_frames_ = 0;
                return candidate.index;
            }
        }

        ++missing_frames_;
        if (missing_frames_ <= std::max(0, cfg_.max_missing_frames)) {
            return -1;
        }
        reset();
    }

    if (candidates.empty()) {
        return -1;
    }
    return selectInitial(candidates);
}

int TrackedTargetSelector::lockedTrackId() const {
    return locked_track_id_;
}

int TrackedTargetSelector::missingFrames() const {
    return missing_frames_;
}

int TrackedTargetSelector::selectInitial(
    const std::vector<TrackedTargetCandidate> &candidates) {
    int best_pos = 0;
    for (int i = 1; i < static_cast<int>(candidates.size()); ++i) {
        if (candidates[i].area > candidates[best_pos].area ||
            (candidates[i].area == candidates[best_pos].area &&
             candidates[i].score > candidates[best_pos].score)) {
            best_pos = i;
        }
    }

    locked_track_id_ = candidates[best_pos].track_id;
    missing_frames_ = 0;
    return candidates[best_pos].index;
}

MonocularDistanceEstimator::MonocularDistanceEstimator(const DistanceEstimatorConfig &config)
    : cfg_(config) {}

void MonocularDistanceEstimator::reset() {
    filtered_distance_m_ = -1.0f;
    raw_history_.clear();
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

    const int window_size = std::clamp(cfg_.median_window_size, 1, 31);
    raw_history_.push_back(raw_distance);
    if (static_cast<int>(raw_history_.size()) > window_size) {
        raw_history_.erase(raw_history_.begin());
    }

    const float robust_distance = medianDistance();
    const float alpha = clampAlpha(cfg_.filter_alpha);
    if (filtered_distance_m_ <= 0.0f || !std::isfinite(filtered_distance_m_)) {
        filtered_distance_m_ = robust_distance;
    } else {
        const float delta = robust_distance - filtered_distance_m_;
        if (std::fabs(delta) > std::max(0.0f, cfg_.stability_deadband_m)) {
            const float max_step = std::max(0.001f, cfg_.max_filtered_step_m);
            filtered_distance_m_ += std::clamp(alpha * delta, -max_step, max_step);
        }
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

const char *laserAimStateName(LaserAimState state) {
    switch (state) {
    case LaserAimState::WaitingForCenter:
        return "WAIT_CENTER";
    case LaserAimState::CoarseSearch:
        return "COARSE_SEARCH";
    case LaserAimState::FineAim:
        return "FINE_AIM";
    case LaserAimState::Locked:
        return "LOCKED";
    }
    return "UNKNOWN";
}

LaserAimController::LaserAimController(const LaserAimConfig &config)
    : cfg_(config) {
    reset();
}

void LaserAimController::reset() {
    state_ = LaserAimState::WaitingForCenter;
    current_yaw_ = std::clamp(cfg_.center_yaw, cfg_.min_yaw, cfg_.max_yaw);
    current_pitch_ = std::clamp(cfg_.center_pitch, cfg_.min_pitch, cfg_.max_pitch);
    centered_frames_ = 0;
    laser_seen_frames_ = 0;
    laser_lost_frames_ = 0;
    coarse_hold_counter_ = 0;
    lock_frames_ = 0;
    yaw_direction_ = -1;
    pitch_direction_ = 1;
    has_last_laser_ = false;
    last_laser_x_ = 0.0f;
    last_laser_y_ = 0.0f;
}

LaserAimOutput LaserAimController::update(const LaserAimObservation &obs) {
    LaserAimOutput out;
    out.state = state_;
    out.pitch = current_pitch_;
    out.yaw = current_yaw_;
    out.laser_valid = obs.laser_valid;

    if (!obs.target_valid || !obs.target_centered || !obs.vehicle_stationary) {
        state_ = LaserAimState::WaitingForCenter;
        centered_frames_ = 0;
        laser_seen_frames_ = 0;
        laser_lost_frames_ = 0;
        coarse_hold_counter_ = 0;
        lock_frames_ = 0;
        has_last_laser_ = false;
        out.state = state_;
        return out;
    }

    ++centered_frames_;
    if (obs.laser_valid) {
        const float dx = obs.laser_x - last_laser_x_;
        const float dy = obs.laser_y - last_laser_y_;
        const float motion = std::sqrt(dx * dx + dy * dy);
        const bool needs_motion_confirmation =
            state_ == LaserAimState::WaitingForCenter ||
            state_ == LaserAimState::CoarseSearch;
        if (obs.laser_motion_confirmed) {
            laser_seen_frames_ = std::max(
                laser_seen_frames_, std::max(1, cfg_.laser_confirm_frames));
        } else if (!needs_motion_confirmation || !has_last_laser_ ||
            motion >= std::max(0.0f, cfg_.coarse_laser_motion_min_px)) {
            ++laser_seen_frames_;
        } else {
            laser_seen_frames_ = 1;
        }
        has_last_laser_ = true;
        last_laser_x_ = obs.laser_x;
        last_laser_y_ = obs.laser_y;
    } else {
        laser_seen_frames_ = 0;
        has_last_laser_ = false;
    }
    if (centered_frames_ < std::max(1, cfg_.centered_hold_frames)) {
        state_ = LaserAimState::WaitingForCenter;
        out.state = state_;
        return out;
    }

    if (state_ == LaserAimState::WaitingForCenter) {
        state_ = laser_seen_frames_ >= std::max(1, cfg_.laser_confirm_frames)
            ? LaserAimState::FineAim
            : LaserAimState::CoarseSearch;
    }

    if (state_ == LaserAimState::CoarseSearch) {
        out.active = true;
        if (laser_seen_frames_ >= std::max(1, cfg_.laser_confirm_frames)) {
            state_ = LaserAimState::FineAim;
            laser_lost_frames_ = 0;
            lock_frames_ = 0;
        } else {
            ++coarse_hold_counter_;
            if (coarse_hold_counter_ >= std::max(1, cfg_.coarse_hold_frames)) {
                coarse_hold_counter_ = 0;
                advanceCoarseSearch();
            }
            out.state = state_;
            out.pitch = current_pitch_;
            out.yaw = current_yaw_;
            return out;
        }
    }

    out.active = true;
    if (!obs.laser_valid) {
        ++laser_lost_frames_;
        if (laser_lost_frames_ > std::max(0, cfg_.laser_lost_frames)) {
            state_ = LaserAimState::CoarseSearch;
            coarse_hold_counter_ = 0;
            lock_frames_ = 0;
        }
        out.state = state_;
        return out;
    }

    laser_lost_frames_ = 0;
    out.error_x = normalize(obs.target_center_x - obs.laser_x, cfg_.frame_width * 0.5f);
    out.error_y = normalize(obs.target_center_y - obs.laser_y, cfg_.frame_height * 0.5f);
    const bool within_deadzone =
        (!cfg_.fine_yaw_enable ||
         std::fabs(out.error_x) <= std::max(0.0f, cfg_.fine_deadzone_norm)) &&
        std::fabs(out.error_y) <= std::max(0.0f, cfg_.fine_deadzone_norm);

    if (within_deadzone) {
        ++lock_frames_;
        if (lock_frames_ >= std::max(1, cfg_.lock_hold_frames)) {
            state_ = LaserAimState::Locked;
        }
    } else {
        lock_frames_ = 0;
        state_ = LaserAimState::FineAim;
        if (cfg_.fine_yaw_enable) {
            current_yaw_ = std::clamp(
                current_yaw_ + fineStep(out.error_x, cfg_.fine_yaw_kp,
                                        cfg_.fine_max_step, cfg_.invert_yaw),
                cfg_.min_yaw, cfg_.max_yaw);
        }
        current_pitch_ = std::clamp(
            current_pitch_ + fineStep(out.error_y, cfg_.fine_pitch_kp,
                                      cfg_.fine_max_step, cfg_.invert_pitch),
            cfg_.min_pitch, cfg_.max_pitch);
    }

    out.state = state_;
    out.pitch = current_pitch_;
    out.yaw = current_yaw_;
    return out;
}

LaserAimState LaserAimController::state() const {
    return state_;
}

void LaserAimController::advanceCoarseSearch() {
    const int yaw_step = std::max(1, cfg_.coarse_yaw_step);
    const int pitch_step = std::max(1, cfg_.coarse_pitch_step);
    if (!cfg_.coarse_yaw_enable) {
        current_yaw_ = std::clamp(cfg_.center_yaw, cfg_.min_yaw, cfg_.max_yaw);
        const int next_pitch = current_pitch_ + pitch_direction_ * pitch_step;
        if (next_pitch <= cfg_.min_pitch || next_pitch >= cfg_.max_pitch) {
            current_pitch_ = std::clamp(next_pitch, cfg_.min_pitch, cfg_.max_pitch);
            pitch_direction_ = -pitch_direction_;
        } else {
            current_pitch_ = next_pitch;
        }
        return;
    }
    const int next_yaw = current_yaw_ + yaw_direction_ * yaw_step;
    if (next_yaw <= cfg_.min_yaw || next_yaw >= cfg_.max_yaw) {
        current_yaw_ = std::clamp(next_yaw, cfg_.min_yaw, cfg_.max_yaw);
        yaw_direction_ = -yaw_direction_;
        const int next_pitch = current_pitch_ + pitch_direction_ * pitch_step;
        if (next_pitch <= cfg_.min_pitch || next_pitch >= cfg_.max_pitch) {
            current_pitch_ = std::clamp(next_pitch, cfg_.min_pitch, cfg_.max_pitch);
            pitch_direction_ = -pitch_direction_;
        } else {
            current_pitch_ = next_pitch;
        }
    } else {
        current_yaw_ = next_yaw;
    }
}

int LaserAimController::fineStep(float error, float kp, int max_step, bool invert) const {
    if (!std::isfinite(error) ||
        std::fabs(error) <= std::max(0.0f, cfg_.fine_deadzone_norm)) {
        return 0;
    }
    const float sign = invert ? -1.0f : 1.0f;
    int step = static_cast<int>(std::round(sign * kp * error));
    if (step == 0) {
        step = sign * error > 0.0f ? 1 : -1;
    }
    const int limit = std::max(1, max_step);
    return std::clamp(step, -limit, limit);
}

float LaserAimController::normalize(float value, float half_frame_size) const {
    if (!std::isfinite(value) || half_frame_size <= 1.0f) {
        return 0.0f;
    }
    return std::clamp(value / half_frame_size, -1.0f, 1.0f);
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
    last_target_side_ = 0;
    search_direction_ = cfg_.default_search_direction < 0 ? -1 : 1;
    distance_hold_active_ = false;
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

        if (lost_frames_ > cfg_.lost_hold_frames) {
            distance_hold_active_ = false;
        }

        if (lost_frames_ >= cfg_.lost_hold_frames) {
            out.yaw = stepLimit(last_yaw_, cfg_.center_yaw);
            last_yaw_ = out.yaw;
        }

        if (cfg_.search_enabled && lost_frames_ > cfg_.lost_hold_frames) {
            const int search_frame = lost_frames_ - cfg_.lost_hold_frames - 1;
            if (search_frame == 0) {
                search_direction_ = last_target_side_ != 0
                    ? last_target_side_
                    : (cfg_.default_search_direction < 0 ? -1 : 1);
            } else if (cfg_.search_sweep_frames > 0 &&
                       search_frame % cfg_.search_sweep_frames == 0) {
                search_direction_ = -search_direction_;
            }

            const int search_rpm = std::max(0, cfg_.search_rpm) * search_direction_;
            out.steer_rpm = search_rpm;
            out.searching = search_rpm != 0;
            out.motor1_rpm = clampInt(search_rpm * cfg_.motor1_steer_sign,
                                      cfg_.motor_rpm_min,
                                      cfg_.motor_rpm_max);
            out.motor2_rpm = clampInt(search_rpm * cfg_.motor2_steer_sign,
                                      cfg_.motor_rpm_min,
                                      cfg_.motor_rpm_max);
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
    if (std::fabs(error_x) > cfg_.steer_deadzone_norm) {
        last_target_side_ = error_x < 0.0f ? -1 : 1;
    }

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
    const int steer_rpm = cfg_.chassis_steer_enabled ? calcSteerRpm(error_x) : 0;
    // Center the target before any forward/backward distance correction. This
    // keeps the first motion predictable and prevents diagonal surges.
    const int follow_rpm = cfg_.distance_follow_enabled && steer_rpm == 0
        ? calcFollowRpm(obs.distance_m, &distance_error)
        : 0;
    out.distance_valid = obs.distance_m > 0.0f && std::isfinite(obs.distance_m);
    out.distance_error_m = distance_error;
    out.forward_rpm = follow_rpm;
    out.steer_rpm = steer_rpm;
    if (steer_rpm != 0) {
        // Verified on the vehicle: (+,+) moves forward, (-,-) moves backward,
        // (-,+) turns left, and (+,-) turns right.
        out.motor1_rpm = clampInt(steer_rpm * cfg_.motor1_steer_sign,
                                  cfg_.motor_rpm_min,
                                  cfg_.motor_rpm_max);
        out.motor2_rpm = clampInt(steer_rpm * cfg_.motor2_steer_sign,
                                  cfg_.motor_rpm_min,
                                  cfg_.motor_rpm_max);
    } else {
        out.motor1_rpm = clampInt(follow_rpm * cfg_.motor1_forward_sign,
                                  cfg_.motor_rpm_min,
                                  cfg_.motor_rpm_max);
        out.motor2_rpm = clampInt(follow_rpm * cfg_.motor2_forward_sign,
                                  cfg_.motor_rpm_min,
                                  cfg_.motor_rpm_max);
    }

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
    // Gimbal CAN commands are position setpoints. Accumulate the visual error
    // from the previously issued setpoint so a persistent off-center target
    // keeps moving toward the reticle instead of settling with residual error.
    const int desired = previous + static_cast<int>(std::round(sign * (kp * e + kd * derivative)));
    return clampInt(stepLimit(previous, desired), lo, hi);
}

int AimFollowController::calcFollowRpm(float distance_m, float *distance_error_out) {
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
    const float stop_deadband = std::max(0.0f, cfg_.distance_deadband_m);
    const float resume_deadband =
        std::max(stop_deadband, cfg_.distance_resume_deadband_m);
    const float absolute_error = std::fabs(error);
    if (distance_hold_active_) {
        if (absolute_error <= resume_deadband) {
            return 0;
        }
        distance_hold_active_ = false;
    }
    if (absolute_error <= stop_deadband) {
        distance_hold_active_ = true;
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

float MonocularDistanceEstimator::medianDistance() const {
    if (raw_history_.empty()) {
        return -1.0f;
    }
    std::vector<float> sorted = raw_history_;
    std::sort(sorted.begin(), sorted.end());
    const size_t middle = sorted.size() / 2;
    if ((sorted.size() % 2) == 0) {
        return (sorted[middle - 1] + sorted[middle]) * 0.5f;
    }
    return sorted[middle];
}

int AimFollowController::calcSteerRpm(float error_x) const {
    if (!std::isfinite(error_x) || std::fabs(error_x) <= cfg_.steer_deadzone_norm) {
        return 0;
    }

    int rpm = static_cast<int>(std::round(cfg_.steer_kp_rpm * error_x));
    if (rpm > 0) {
        rpm = std::max(cfg_.min_steer_rpm, rpm);
    } else if (rpm < 0) {
        rpm = std::min(-cfg_.min_steer_rpm, rpm);
    }
    return clampInt(rpm, -cfg_.max_steer_rpm, cfg_.max_steer_rpm);
}

} // namespace aim_follow
