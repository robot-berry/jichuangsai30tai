#pragma once

#include <cstdint>
#include <vector>

namespace aim_follow {

struct TargetCandidate {
    int index = -1;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float area = 0.0f;
    float score = 0.0f;
};

struct TargetSelectorConfig {
    float frame_width = 1920.0f;
    float frame_height = 1080.0f;
    float max_center_jump_norm = 0.25f;
    float area_switch_ratio = 1.8f;
    int max_lost_frames = 5;
};

class TargetSelector {
public:
    explicit TargetSelector(const TargetSelectorConfig &config = TargetSelectorConfig());

    void reset();
    void setConfig(const TargetSelectorConfig &config);
    const TargetSelectorConfig &config() const;

    int select(const std::vector<TargetCandidate> &candidates);

private:
    TargetSelectorConfig cfg_;
    bool has_last_target_ = false;
    float last_center_x_ = 0.0f;
    float last_center_y_ = 0.0f;
    float last_area_ = 0.0f;
    int lost_frames_ = 0;

    float normalizedDistance(float x, float y) const;
    void remember(const TargetCandidate &candidate);
};

struct TargetObservation {
    bool valid = false;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float box_width = 0.0f;
    float distance_m = -1.0f;
    float timestamp_s = 0.0f;
};

struct ControlConfig {
    // HDMI/display coordinate size used by the post-process stage.
    float frame_width = 1920.0f;
    float frame_height = 1080.0f;

    // Gimbal command range measured from the existing CAN protocol.
    int center_yaw = 150;
    int center_pitch = 150;
    int min_yaw = 100;
    int max_yaw = 200;
    int min_pitch = 100;
    int max_pitch = 200;

    // Image based visual-servo gains. The input error is normalized to [-1, 1].
    float yaw_kp = 38.0f;
    float yaw_kd = 8.0f;
    float pitch_kp = 42.0f;
    float pitch_kd = 8.0f;
    float aim_deadzone_norm = 0.035f;
    float max_cmd_step = 8.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;

    // Fixed-distance following. distance_error = current_distance - target_distance.
    float target_distance_m = 1.0f;
    float distance_deadband_m = 0.12f;
    float follow_kp_rpm_per_m = 180.0f;
    int min_follow_rpm = 35;
    int max_follow_rpm = 160;
    int motor_rpm_min = -1000;
    int motor_rpm_max = 1000;

    // Some chassis mount the left/right motors in opposite directions.
    // Keep both as +1 first; if the car rotates instead of moving straight,
    // set one side to -1 during board testing.
    int motor1_forward_sign = 1;
    int motor2_forward_sign = 1;

    // If the target is lost for several frames, stop the chassis and slowly
    // return yaw to center while keeping pitch.
    int lost_hold_frames = 5;
};

struct ControlOutput {
    int motor1_rpm = 0;
    int motor2_rpm = 0;
    int pitch = 150;
    int yaw = 150;
    bool target_valid = false;
    bool distance_valid = false;
    float norm_error_x = 0.0f;
    float norm_error_y = 0.0f;
    float distance_error_m = 0.0f;
};

class AimFollowController {
public:
    explicit AimFollowController(const ControlConfig &config = ControlConfig());

    void reset();
    void setConfig(const ControlConfig &config);
    const ControlConfig &config() const;

    ControlOutput update(const TargetObservation &obs);

private:
    ControlConfig cfg_;
    bool has_last_error_ = false;
    float last_error_x_ = 0.0f;
    float last_error_y_ = 0.0f;
    float last_time_s_ = 0.0f;
    int lost_frames_ = 0;
    int last_yaw_;
    int last_pitch_;

    int clampInt(int value, int lo, int hi) const;
    int stepLimit(int previous, int desired) const;
    float normalizeError(float value, float frame_size) const;
    int calcAxisCommand(float error,
                        float last_error,
                        float dt,
                        float kp,
                        float kd,
                        int center,
                        int previous,
                        int lo,
                        int hi,
                        bool invert) const;
    int calcFollowRpm(float distance_m, float *distance_error_out) const;
};

} // namespace aim_follow
