# PLin Main HDMI Dry-Run Integration Patch

This note records the main-program integration that is currently applied in the
working PLin project:

```text
PLin+SingleNet+HDMI/src/sdicamera+yolov5+hdmi.cpp
```

It is intentionally kept as a patch checklist instead of copying the whole PLin
application into this focused repository.

## 1. CAN dry-run switch

Add this constant near the CAN and aim/follow constants:

```cpp
const char *AIM_FOLLOW_CAN_DRYRUN_ENV = "AIM_FOLLOW_CAN_DRYRUN";
```

Add this helper before `configure_can0()`:

```cpp
bool is_can_dry_run_enabled() {
    const char *value = std::getenv(AIM_FOLLOW_CAN_DRYRUN_ENV);
    if (value == nullptr) {
        return false;
    }
    return std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 ||
           std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}
```

At the start of `can_init()`:

```cpp
if (is_can_dry_run_enabled()) {
    std::cout << "[CAN DRYRUN] AIM_FOLLOW_CAN_DRYRUN=1, skip can0 configure/open/write."
              << std::endl;
    return;
}
```

At the start of `send_can_frame(...)`:

```cpp
if (is_can_dry_run_enabled()) {
    std::cout << tag << " DRYRUN id=0x" << std::hex << frame.can_id
              << std::dec << " dlc=" << static_cast<int>(frame.can_dlc)
              << std::endl;
    return true;
}
```

With this switch, run the board app as:

```bash
AIM_FOLLOW_CAN_DRYRUN=1 ./build/ZG/sdicamera+yolov5+hdmi configs/ZG/sdicamera+yolov5+hdmi_vision_sdi.yaml
```

The controller still computes chassis/gimbal commands, but no CAN frame is
written to `can0`.

## 2. Control values for HDMI display

For camera-independent HDMI verification, add this constant near
`AIM_FOLLOW_CAN_DRYRUN_ENV`:

```cpp
const char *AIM_FOLLOW_SYNTHETIC_TARGET_ENV = "AIM_FOLLOW_SYNTHETIC_TARGET";
```

Add a helper:

```cpp
bool is_synthetic_target_enabled() {
    return is_env_enabled(AIM_FOLLOW_SYNTHETIC_TARGET_ENV);
}
```

In the current implementation `is_can_dry_run_enabled()` and
`is_synthetic_target_enabled()` both use a shared `is_env_enabled(...)` helper
that accepts `1`, `true`, `TRUE`, `yes`, and `YES`.

Inside the YOLO post-process lambda, after:

```cpp
int filtered_target_index = -1;
float target_raw_distance_m = -1.0f;
float target_filtered_distance_m = -1.0f;
```

add:

```cpp
bool control_target_valid = false;
float control_distance_error_m = 0.0f;
float control_ex = 0.0f;
float control_ey = 0.0f;
int control_motor1 = last_motor1;
int control_motor2 = last_motor2;
int control_pitch = last_pitch;
int control_yaw = last_yaw;
```

After `map_box_to_display(...)`, add the inverse mapping helper and synthetic
target injection:

```cpp
auto display_box_to_model = [&](const cv::Rect2f &display_box) -> cv::Rect2f {
    if (is_hw_resize) {
        return cv::Rect2f(
            (display_box.tl().x - BIAS_W) / RATIO_W,
            (display_box.tl().y - BIAS_H) / RATIO_H,
            display_box.width / RATIO_W,
            display_box.height / RATIO_H
        );
    }

    return cv::Rect2f(
        display_box.tl().x * RATIO_W + BIAS_W,
        display_box.tl().y * RATIO_H + BIAS_H,
        display_box.width * RATIO_W,
        display_box.height * RATIO_H
    );
};

if (is_synthetic_target_enabled()) {
    // The implementation cycles through far, right, up, and close target states.
    // It replaces the filtered bicycle list only in synthetic-test mode.
}
```

The synthetic target mode is only for board bring-up:

```bash
AIM_FOLLOW_CAN_DRYRUN=1 AIM_FOLLOW_SYNTHETIC_TARGET=1 ./build/ZG/sdicamera+yolov5+hdmi configs/ZG/sdicamera+yolov5+hdmi_vision_vtc.yaml
```

It lets the VTC/internal frame source exercise the same distance, target
selection, aim/follow, HDMI drawing, and CAN dry-run code path while the real
SDI camera input is still unavailable.

After computing `follow_cmd` for a valid target, copy the values for drawing:

```cpp
control_target_valid = true;
control_distance_error_m = follow_cmd.distance_error_m;
control_ex = follow_cmd.norm_error_x;
control_ey = follow_cmd.norm_error_y;
control_motor1 = motor1_rpm;
control_motor2 = motor2_rpm;
control_pitch = send_pitch;
control_yaw = send_yaw;
```

After computing `lost_cmd` for a lost target:

```cpp
control_target_valid = false;
control_distance_error_m = lost_cmd.distance_error_m;
control_ex = lost_cmd.norm_error_x;
control_ey = lost_cmd.norm_error_y;
control_motor1 = lost_cmd.motor1_rpm;
control_motor2 = lost_cmd.motor2_rpm;
control_pitch = lost_cmd.pitch;
control_yaw = lost_cmd.yaw;
```

## 3. HDMI panel

Before drawing detection boxes, draw the control panel:

```cpp
const int panel_x = 18;
const int panel_y = 56;
const int panel_w = 690;
const int panel_h = 168;
cv::rectangle(cvmat_to_draw,
              cv::Rect(panel_x - 10, panel_y - 34, panel_w, panel_h),
              cv::Scalar(0, 0, 0), cv::FILLED);

auto draw_control_text = [&](const std::string &text, int row, const cv::Scalar &color) {
    const cv::Point org(panel_x, panel_y + row * 34);
    cv::putText(cvmat_to_draw, text, org, cv::FONT_HERSHEY_DUPLEX, 0.8,
                cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
    cv::putText(cvmat_to_draw, text, org, cv::FONT_HERSHEY_DUPLEX, 0.8,
                color, 1, cv::LINE_AA);
};

const std::string target_state = control_target_valid ? "LOCK" : "LOST";
const std::string distance_text = target_filtered_distance_m > 0.0f
    ? fmt::format("{:.2f}m", target_filtered_distance_m)
    : "--";
draw_control_text(fmt::format("Target:{}  Distance:{}  Error:{:+.2f}m",
                              target_state, distance_text, control_distance_error_m),
                  0, control_target_valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255));
draw_control_text(fmt::format("Gimbal tracking: pitch={} yaw={}  ex={:+.2f} ey={:+.2f}",
                              control_pitch, control_yaw, control_ex, control_ey),
                  1, cv::Scalar(255, 255, 255));
draw_control_text(fmt::format("Chassis tracking: motor1={}rpm motor2={}rpm",
                              control_motor1, control_motor2),
                  2, cv::Scalar(255, 255, 255));
draw_control_text(fmt::format("CAN output: {}",
                              is_can_dry_run_enabled() ? "DRYRUN(no write)" : "ACTIVE(write can0)"),
                  3, is_can_dry_run_enabled() ? cv::Scalar(0, 255, 255) : cv::Scalar(0, 0, 255));
```

OpenCV's built-in font is used with English labels so the panel renders
reliably on the board without Chinese font assets.

## 4. Verification

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_plin_integration.ps1 -ProjectDir <PLinProjectDir>
```

The verifier checks these exact integration markers:

- `AIM_FOLLOW_CAN_DRYRUN`
- `AIM_FOLLOW_SYNTHETIC_TARGET`
- `DRYRUN id=0x`
- `[SYNTHETIC TARGET]`
- `Target:{}  Distance:{}  Error:{:+.2f}m`
- `Gimbal tracking: pitch={} yaw={}`
- `Chassis tracking: motor1={}rpm motor2={}rpm`
- `CAN output: {}`

Run the HDMI/no-CAN board test:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_vision_algorithm_test.ps1 -ProjectDir <PLinProjectDir> -SshKey .\.ssh_board\id_ed25519_30tai -SkipUpload -SkipBuild
```

Run the HDMI/no-CAN board test with VTC and synthetic target:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_vision_algorithm_test.ps1 -ProjectDir <PLinProjectDir> -SshKey .\.ssh_board\id_ed25519_30tai -UseVtc -SyntheticTarget
```

Expected behavior after the physical SDI input is fixed:

- the panel appears on HDMI
- `Target` changes between `LOCK` and `LOST`
- `Distance` and `Error` update with target size
- `Gimbal tracking` pitch/yaw change with target offset
- `Chassis tracking` motor RPM changes with distance error
- `CAN output` remains `DRYRUN(no write)` until real hardware tests are ready
