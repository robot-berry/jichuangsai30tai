#include "aim_follow_controller.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

}

int main() {
    aim_follow::TargetSelectorConfig selector_cfg;
    selector_cfg.frame_width = 1920.0f;
    selector_cfg.frame_height = 1080.0f;
    selector_cfg.max_center_jump_norm = 0.20f;
    selector_cfg.area_switch_ratio = 1.8f;
    selector_cfg.max_lost_frames = 2;
    aim_follow::TargetSelector selector(selector_cfg);

    std::vector<aim_follow::TargetCandidate> first_candidates = {
        {0, 300.0f, 300.0f, 600.0f, 0.90f},
        {1, 900.0f, 500.0f, 1000.0f, 0.80f},
    };
    assert(selector.select(first_candidates) == 1);

    std::vector<aim_follow::TargetCandidate> keep_candidates = {
        {0, 910.0f, 510.0f, 900.0f, 0.88f},
        {1, 1500.0f, 800.0f, 1100.0f, 0.92f},
    };
    assert(selector.select(keep_candidates) == 0);

    std::vector<aim_follow::TargetCandidate> switch_candidates = {
        {0, 920.0f, 510.0f, 900.0f, 0.88f},
        {1, 1500.0f, 800.0f, 4000.0f, 0.92f},
    };
    assert(selector.select(switch_candidates) == 1);

    for (int i = 0; i < selector_cfg.max_lost_frames + 1; ++i) {
        assert(selector.select({}) == -1);
    }
    assert(selector.select(first_candidates) == 1);

    aim_follow::DistanceEstimatorConfig distance_cfg;
    distance_cfg.target_real_width_m = 0.24f;
    distance_cfg.focal_length_px = 553.0f;
    distance_cfg.min_box_width_px = 1.0f;
    distance_cfg.filter_alpha = 0.30f;
    aim_follow::MonocularDistanceEstimator distance_estimator(distance_cfg);

    const auto one_meter = distance_estimator.update(0.24f * 553.0f / 1.0f);
    assert(one_meter.valid);
    assert(near(one_meter.raw_distance_m, 1.0f));
    assert(near(one_meter.filtered_distance_m, 1.0f));

    const auto two_meter = distance_estimator.update(0.24f * 553.0f / 2.0f);
    assert(two_meter.valid);
    assert(near(two_meter.raw_distance_m, 2.0f));
    assert(near(two_meter.filtered_distance_m, 1.3f));

    const auto invalid_box = distance_estimator.update(0.0f);
    assert(!invalid_box.valid);
    assert(near(invalid_box.filtered_distance_m, 1.3f));

    aim_follow::ControlConfig cfg;
    cfg.frame_width = 1920.0f;
    cfg.frame_height = 1080.0f;
    cfg.target_distance_m = 1.0f;
    cfg.motor1_forward_sign = 1;
    cfg.motor2_forward_sign = 1;

    aim_follow::AimFollowController controller(cfg);

    aim_follow::TargetObservation centered;
    centered.valid = true;
    centered.center_x = 960.0f;
    centered.center_y = 540.0f;
    centered.distance_m = 1.0f;
    centered.timestamp_s = 0.05f;

    const auto hold = controller.update(centered);
    assert(hold.motor1_rpm == 0);
    assert(hold.motor2_rpm == 0);
    assert(hold.yaw == cfg.center_yaw);
    assert(hold.pitch == cfg.center_pitch);

    aim_follow::TargetObservation far = centered;
    far.distance_m = 1.6f;
    far.timestamp_s = 0.10f;
    const auto forward = controller.update(far);
    assert(forward.motor1_rpm > 0);
    assert(forward.motor2_rpm > 0);

    aim_follow::TargetObservation close = centered;
    close.distance_m = 0.55f;
    close.timestamp_s = 0.15f;
    const auto backward = controller.update(close);
    assert(backward.motor1_rpm < 0);
    assert(backward.motor2_rpm < 0);

    aim_follow::TargetObservation right = centered;
    right.center_x = 1500.0f;
    right.distance_m = 1.0f;
    right.timestamp_s = 0.20f;
    const auto aim_right = controller.update(right);
    assert(aim_right.yaw > cfg.center_yaw);

    aim_follow::TargetObservation up = centered;
    up.center_y = 200.0f;
    up.timestamp_s = 0.25f;
    const auto aim_up = controller.update(up);
    assert(aim_up.pitch < cfg.center_pitch);

    aim_follow::TargetObservation lost;
    lost.valid = false;
    lost.timestamp_s = 0.30f;
    for (int i = 0; i < cfg.lost_hold_frames + 1; ++i) {
        controller.update(lost);
    }

    std::cout << "aim_follow_controller_test passed" << std::endl;
    return 0;
}
