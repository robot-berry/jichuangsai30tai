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

    aim_follow::TrackedTargetSelectorConfig tracked_selector_cfg;
    tracked_selector_cfg.max_missing_frames = 2;
    aim_follow::TrackedTargetSelector tracked_selector(tracked_selector_cfg);
    std::vector<aim_follow::TrackedTargetCandidate> first_tracks = {
        {0, 11, 400.0f, 400.0f, 500.0f, 0.90f},
        {1, 22, 900.0f, 500.0f, 1000.0f, 0.80f},
    };
    assert(tracked_selector.select(first_tracks) == 1);
    assert(tracked_selector.lockedTrackId() == 22);

    std::vector<aim_follow::TrackedTargetCandidate> crossing_tracks = {
        {0, 11, 910.0f, 510.0f, 4000.0f, 0.95f},
        {1, 22, 420.0f, 400.0f, 700.0f, 0.70f},
    };
    assert(tracked_selector.select(crossing_tracks) == 1);
    assert(tracked_selector.lockedTrackId() == 22);

    std::vector<aim_follow::TrackedTargetCandidate> only_other_track = {
        {0, 11, 910.0f, 510.0f, 4000.0f, 0.95f},
    };
    assert(tracked_selector.select(only_other_track) == -1);
    assert(tracked_selector.select(crossing_tracks) == 1);
    assert(tracked_selector.missingFrames() == 0);
    assert(tracked_selector.select(only_other_track) == -1);
    assert(tracked_selector.select(only_other_track) == -1);
    assert(tracked_selector.select(only_other_track) == 0);
    assert(tracked_selector.lockedTrackId() == 11);

    aim_follow::StableTrackIdConfig stable_id_cfg;
    stable_id_cfg.frame_width = 640.0f;
    stable_id_cfg.frame_height = 640.0f;
    stable_id_cfg.max_missing_frames = 3;
    stable_id_cfg.max_center_jump_norm = 0.15f;
    stable_id_cfg.max_area_ratio = 2.5f;
    aim_follow::StableTrackIdMapper stable_id_mapper(stable_id_cfg);

    const auto initial_ids = stable_id_mapper.update({
        {0, 101, 160.0f, 300.0f, 10000.0f, 0.90f},
        {1, 202, 480.0f, 300.0f, 9000.0f, 0.88f},
    });
    assert(initial_ids.size() == 2);
    assert(initial_ids[0] != initial_ids[1]);

    const auto one_visible_ids = stable_id_mapper.update({
        {0, 202, 478.0f, 302.0f, 9200.0f, 0.87f},
    });
    assert(one_visible_ids.size() == 1);
    assert(one_visible_ids[0] == initial_ids[1]);

    const auto recovered_ids = stable_id_mapper.update({
        {0, 202, 476.0f, 304.0f, 9100.0f, 0.86f},
        {1, 303, 166.0f, 297.0f, 10400.0f, 0.91f},
    });
    assert(recovered_ids.size() == 2);
    assert(recovered_ids[0] == initial_ids[1]);
    assert(recovered_ids[1] == initial_ids[0]);

    const auto distinct_new_ids = stable_id_mapper.update({
        {0, 202, 474.0f, 305.0f, 9000.0f, 0.86f},
        {1, 303, 168.0f, 296.0f, 10200.0f, 0.90f},
        {2, 404, 320.0f, 80.0f, 2500.0f, 0.82f},
    });
    assert(distinct_new_ids.size() == 3);
    assert(distinct_new_ids[0] != distinct_new_ids[1]);
    assert(distinct_new_ids[2] != distinct_new_ids[0]);
    assert(distinct_new_ids[2] != distinct_new_ids[1]);

    aim_follow::LaserAimConfig laser_cfg;
    laser_cfg.frame_width = 640.0f;
    laser_cfg.frame_height = 480.0f;
    laser_cfg.center_yaw = 123;
    laser_cfg.center_pitch = 150;
    laser_cfg.min_yaw = 100;
    laser_cfg.max_yaw = 165;
    laser_cfg.min_pitch = 120;
    laser_cfg.max_pitch = 180;
    laser_cfg.centered_hold_frames = 2;
    laser_cfg.laser_confirm_frames = 2;
    laser_cfg.coarse_hold_frames = 1;
    laser_cfg.coarse_yaw_step = 5;
    laser_cfg.coarse_pitch_step = 5;
    laser_cfg.coarse_yaw_enable = true;
    laser_cfg.fine_max_step = 2;
    laser_cfg.fine_yaw_enable = true;
    laser_cfg.lock_hold_frames = 2;
    aim_follow::LaserAimController laser_controller(laser_cfg);

    aim_follow::LaserAimObservation laser_obs;
    laser_obs.target_valid = true;
    laser_obs.target_center_x = 320.0f;
    laser_obs.target_center_y = 240.0f;
    laser_obs.vehicle_stationary = true;
    auto laser_wait = laser_controller.update(laser_obs);
    assert(laser_wait.state == aim_follow::LaserAimState::WaitingForCenter);
    assert(!laser_wait.active);

    laser_obs.target_centered = true;
    laser_wait = laser_controller.update(laser_obs);
    assert(laser_wait.state == aim_follow::LaserAimState::WaitingForCenter);
    const auto coarse = laser_controller.update(laser_obs);
    assert(coarse.state == aim_follow::LaserAimState::CoarseSearch);
    assert(coarse.active);
    assert(coarse.yaw == laser_cfg.center_yaw - laser_cfg.coarse_yaw_step);

    laser_obs.laser_valid = true;
    laser_obs.laser_x = 100.0f;
    laser_obs.laser_y = 240.0f;
    const auto coarse_confirm = laser_controller.update(laser_obs);
    assert(coarse_confirm.state == aim_follow::LaserAimState::CoarseSearch);
    laser_obs.laser_x = 110.0f;
    const auto fine = laser_controller.update(laser_obs);
    assert(fine.state == aim_follow::LaserAimState::FineAim);
    assert(fine.yaw > coarse_confirm.yaw);
    assert(fine.yaw - coarse_confirm.yaw <= laser_cfg.fine_max_step);

    laser_obs.laser_x = laser_obs.target_center_x;
    laser_obs.laser_y = laser_obs.target_center_y;
    const auto almost_locked = laser_controller.update(laser_obs);
    assert(almost_locked.state == aim_follow::LaserAimState::FineAim);
    const auto locked = laser_controller.update(laser_obs);
    assert(locked.state == aim_follow::LaserAimState::Locked);

    laser_obs.vehicle_stationary = false;
    const auto motion_gate = laser_controller.update(laser_obs);
    assert(motion_gate.state == aim_follow::LaserAimState::WaitingForCenter);
    assert(!motion_gate.active);

    aim_follow::LaserAimConfig differenced_cfg = laser_cfg;
    differenced_cfg.centered_hold_frames = 1;
    differenced_cfg.laser_confirm_frames = 3;
    aim_follow::LaserAimController differenced_controller(differenced_cfg);
    aim_follow::LaserAimObservation differenced_obs;
    differenced_obs.target_valid = true;
    differenced_obs.target_centered = true;
    differenced_obs.vehicle_stationary = true;
    differenced_obs.target_center_x = 320.0f;
    differenced_obs.target_center_y = 240.0f;
    const auto differenced_coarse = differenced_controller.update(differenced_obs);
    assert(differenced_coarse.state == aim_follow::LaserAimState::CoarseSearch);
    differenced_obs.laser_valid = true;
    differenced_obs.laser_motion_confirmed = true;
    differenced_obs.laser_x = 120.0f;
    differenced_obs.laser_y = 220.0f;
    const auto differenced_fine = differenced_controller.update(differenced_obs);
    assert(differenced_fine.state == aim_follow::LaserAimState::FineAim);
    assert(differenced_fine.active);

    aim_follow::LaserAimConfig vertical_coarse_cfg = laser_cfg;
    vertical_coarse_cfg.centered_hold_frames = 1;
    vertical_coarse_cfg.coarse_hold_frames = 1;
    vertical_coarse_cfg.coarse_yaw_enable = false;
    vertical_coarse_cfg.fine_yaw_enable = true;
    vertical_coarse_cfg.laser_confirm_frames = 2;
    aim_follow::LaserAimController vertical_coarse_controller(vertical_coarse_cfg);
    aim_follow::LaserAimObservation vertical_coarse_obs;
    vertical_coarse_obs.target_valid = true;
    vertical_coarse_obs.target_centered = true;
    vertical_coarse_obs.vehicle_stationary = true;
    vertical_coarse_obs.target_center_x = 320.0f;
    vertical_coarse_obs.target_center_y = 240.0f;
    const auto vertical_coarse = vertical_coarse_controller.update(vertical_coarse_obs);
    assert(vertical_coarse.state == aim_follow::LaserAimState::CoarseSearch);
    assert(vertical_coarse.yaw == vertical_coarse_cfg.center_yaw);
    assert(vertical_coarse.pitch != vertical_coarse_cfg.center_pitch);
    vertical_coarse_obs.laser_valid = true;
    vertical_coarse_obs.laser_motion_confirmed = true;
    vertical_coarse_obs.laser_x = 80.0f;
    vertical_coarse_obs.laser_y = 200.0f;
    const auto two_axis_fine = vertical_coarse_controller.update(vertical_coarse_obs);
    assert(two_axis_fine.state == aim_follow::LaserAimState::FineAim);
    assert(two_axis_fine.yaw != vertical_coarse_cfg.center_yaw);
    assert(std::abs(two_axis_fine.yaw - vertical_coarse_cfg.center_yaw) <=
           vertical_coarse_cfg.fine_max_step);
    vertical_coarse_obs.laser_motion_confirmed = false;
    vertical_coarse_obs.laser_x = vertical_coarse_obs.target_center_x;
    vertical_coarse_obs.laser_y = vertical_coarse_obs.target_center_y;
    const auto vertical_coarse_almost_locked =
        vertical_coarse_controller.update(vertical_coarse_obs);
    const auto vertical_coarse_locked =
        vertical_coarse_controller.update(vertical_coarse_obs);
    assert(vertical_coarse_almost_locked.state == aim_follow::LaserAimState::FineAim);
    assert(vertical_coarse_locked.state == aim_follow::LaserAimState::Locked);

    aim_follow::DistanceEstimatorConfig distance_cfg;
    distance_cfg.target_real_width_m = 0.24f;
    distance_cfg.focal_length_px = 553.0f;
    distance_cfg.min_box_width_px = 1.0f;
    distance_cfg.filter_alpha = 0.30f;
    distance_cfg.median_window_size = 5;
    distance_cfg.stability_deadband_m = 0.03f;
    distance_cfg.max_filtered_step_m = 0.12f;
    aim_follow::MonocularDistanceEstimator distance_estimator(distance_cfg);

    const auto one_meter = distance_estimator.update(0.24f * 553.0f / 1.0f);
    assert(one_meter.valid);
    assert(near(one_meter.raw_distance_m, 1.0f));
    assert(near(one_meter.filtered_distance_m, 1.0f));

    const auto two_meter = distance_estimator.update(0.24f * 553.0f / 2.0f);
    assert(two_meter.valid);
    assert(near(two_meter.raw_distance_m, 2.0f));
    assert(two_meter.filtered_distance_m > 1.0f);
    assert(two_meter.filtered_distance_m <= 1.12f + 1e-3f);

    const auto invalid_box = distance_estimator.update(0.0f);
    assert(!invalid_box.valid);
    assert(near(invalid_box.filtered_distance_m, two_meter.filtered_distance_m));

    aim_follow::MonocularDistanceEstimator stable_estimator(distance_cfg);
    const float jitter_distances[] = {1.00f, 1.02f, 0.98f, 1.01f, 0.99f, 1.02f, 0.98f};
    float stable_value = -1.0f;
    for (float distance_m : jitter_distances) {
        stable_value = stable_estimator.update(0.24f * 553.0f / distance_m).filtered_distance_m;
    }
    assert(std::fabs(stable_value - 1.0f) <= distance_cfg.stability_deadband_m);

    aim_follow::ControlConfig cfg;
    cfg.frame_width = 1920.0f;
    cfg.frame_height = 1080.0f;
    cfg.target_distance_m = 1.0f;
    cfg.distance_deadband_m = 0.03f;
    cfg.distance_resume_deadband_m = 0.08f;
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

    aim_follow::AimFollowController hysteresis_controller(cfg);
    const auto hysteresis_enter = hysteresis_controller.update(centered);
    assert(hysteresis_enter.motor1_rpm == 0);
    aim_follow::TargetObservation within_resume = centered;
    within_resume.distance_m = 1.05f;
    within_resume.timestamp_s = 0.10f;
    const auto hysteresis_hold = hysteresis_controller.update(within_resume);
    assert(hysteresis_hold.motor1_rpm == 0);
    aim_follow::TargetObservation brief_dropout;
    brief_dropout.valid = false;
    brief_dropout.timestamp_s = 0.12f;
    const auto dropout_stop = hysteresis_controller.update(brief_dropout);
    assert(dropout_stop.motor1_rpm == 0);
    within_resume.timestamp_s = 0.14f;
    const auto reacquired_hold = hysteresis_controller.update(within_resume);
    assert(reacquired_hold.motor1_rpm == 0);
    aim_follow::TargetObservation beyond_resume = centered;
    beyond_resume.distance_m = 1.081f;
    beyond_resume.timestamp_s = 0.15f;
    const auto hysteresis_forward = hysteresis_controller.update(beyond_resume);
    assert(hysteresis_forward.motor1_rpm > 0);
    aim_follow::TargetObservation reenter = centered;
    reenter.distance_m = 1.029f;
    reenter.timestamp_s = 0.20f;
    const auto hysteresis_reenter = hysteresis_controller.update(reenter);
    assert(hysteresis_reenter.motor1_rpm == 0);
    aim_follow::TargetObservation within_reverse_resume = centered;
    within_reverse_resume.distance_m = 0.921f;
    within_reverse_resume.timestamp_s = 0.25f;
    const auto hysteresis_reverse_hold = hysteresis_controller.update(within_reverse_resume);
    assert(hysteresis_reverse_hold.motor1_rpm == 0);
    aim_follow::TargetObservation beyond_reverse_resume = centered;
    beyond_reverse_resume.distance_m = 0.919f;
    beyond_reverse_resume.timestamp_s = 0.30f;
    const auto hysteresis_backward = hysteresis_controller.update(beyond_reverse_resume);
    assert(hysteresis_backward.motor1_rpm < 0);

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

    aim_follow::ControlConfig steer_cfg = cfg;
    steer_cfg.distance_follow_enabled = false;
    steer_cfg.chassis_steer_enabled = true;
    steer_cfg.steer_deadzone_norm = 0.10f;
    steer_cfg.min_steer_rpm = 25;
    steer_cfg.max_steer_rpm = 45;
    steer_cfg.motor1_steer_sign = 1;
    steer_cfg.motor2_steer_sign = -1;
    aim_follow::AimFollowController steer_controller(steer_cfg);

    const auto steer_hold = steer_controller.update(centered);
    assert(steer_hold.motor1_rpm == 0);
    assert(steer_hold.motor2_rpm == 0);

    const auto steer_right = steer_controller.update(right);
    assert(steer_right.steer_rpm > 0);
    assert(steer_right.motor1_rpm > 0);
    assert(steer_right.motor2_rpm < 0);
    assert(std::abs(steer_right.motor1_rpm) <= steer_cfg.max_steer_rpm);
    assert(std::abs(steer_right.motor2_rpm) <= steer_cfg.max_steer_rpm);

    aim_follow::TargetObservation left = centered;
    left.center_x = 300.0f;
    left.timestamp_s = 0.22f;
    const auto steer_left = steer_controller.update(left);
    assert(steer_left.steer_rpm < 0);
    assert(steer_left.motor1_rpm < 0);
    assert(steer_left.motor2_rpm > 0);

    aim_follow::ControlConfig mixed_cfg = steer_cfg;
    mixed_cfg.distance_follow_enabled = true;
    mixed_cfg.min_follow_rpm = 40;
    mixed_cfg.max_follow_rpm = 40;
    mixed_cfg.min_steer_rpm = 40;
    mixed_cfg.max_steer_rpm = 40;
    mixed_cfg.motor_rpm_min = -40;
    mixed_cfg.motor_rpm_max = 40;
    aim_follow::AimFollowController mixed_controller(mixed_cfg);
    aim_follow::TargetObservation close_right = right;
    close_right.distance_m = 0.72f;
    close_right.timestamp_s = 0.30f;
    const auto backward_right = mixed_controller.update(close_right);
    assert(backward_right.forward_rpm < 0);
    assert(backward_right.steer_rpm > 0);
    assert(backward_right.motor1_rpm == 0);
    assert(backward_right.motor2_rpm == -40);

    aim_follow::TargetObservation far_right = right;
    far_right.distance_m = 1.30f;
    far_right.timestamp_s = 0.35f;
    const auto forward_right = mixed_controller.update(far_right);
    assert(forward_right.forward_rpm > 0);
    assert(forward_right.steer_rpm > 0);
    assert(forward_right.motor1_rpm == 40);
    assert(forward_right.motor2_rpm == 0);

    aim_follow::TargetObservation up = centered;
    up.center_y = 200.0f;
    up.timestamp_s = 0.25f;
    const auto aim_up = controller.update(up);
    assert(aim_up.pitch < cfg.center_pitch);

    aim_follow::TargetObservation lost;
    lost.valid = false;
    lost.timestamp_s = 0.30f;
    aim_follow::AimFollowController search_controller(steer_cfg);
    search_controller.update(left);
    for (int i = 0; i < steer_cfg.lost_hold_frames; ++i) {
        const auto dropout_hold = search_controller.update(lost);
        assert(dropout_hold.motor1_rpm == 0);
        assert(dropout_hold.motor2_rpm == 0);
    }
    const auto search_left = search_controller.update(lost);
    assert(search_left.searching);
    assert(search_left.motor1_rpm < 0);
    assert(search_left.motor2_rpm > 0);

    aim_follow::ControlConfig alternating_cfg = steer_cfg;
    alternating_cfg.search_sweep_frames = 3;
    aim_follow::AimFollowController alternating_search(alternating_cfg);
    for (int i = 0; i <= alternating_cfg.lost_hold_frames; ++i) {
        alternating_search.update(lost);
    }
    const auto initial_search = alternating_search.update(lost);
    const int initial_direction = initial_search.steer_rpm < 0 ? -1 : 1;
    aim_follow::ControlOutput reversed_search;
    for (int i = 0; i < alternating_cfg.search_sweep_frames; ++i) {
        reversed_search = alternating_search.update(lost);
    }
    assert(reversed_search.searching);
    assert((reversed_search.steer_rpm < 0 ? -1 : 1) == -initial_direction);

    std::cout << "aim_follow_controller_test passed" << std::endl;
    return 0;
}
