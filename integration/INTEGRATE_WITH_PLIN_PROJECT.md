# Integrate Aim/Follow Module With the PLin YOLOv5 HDMI Project

This document describes how to integrate this focused project into the current PLin + SingleNet + HDMI application.

The goal is not to replace the original application. The goal is to add the aim/follow algorithm module after YOLO post-processing and before CAN output.

## 0. Recommended sync command

From this focused repository, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\sync_to_plin_project.ps1 -ProjectDir <PLinProjectDir>
```

Use `-DryRun` before copying:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\sync_to_plin_project.ps1 -ProjectDir <PLinProjectDir> -DryRun
```

The sync script copies `aim_follow_control/` and the board/log tools, then checks whether the PLin `CMakeLists.txt` already references the module source and include path.

## 1. Copy module into the PLin project

Copy:

```text
aim_follow_control/
```

into the PLin application root, next to:

```text
CMakeLists.txt
src/
configs/
imodel/
```

Optional but recommended: copy the tools:

```text
tools/
```

## 2. Update CMake

In the PLin application `CMakeLists.txt`, add the source file to the main target:

```cmake
aim_follow_control/src/aim_follow_controller.cpp
```

Add the include directory:

```cmake
${CMAKE_CURRENT_SOURCE_DIR}/aim_follow_control/include
```

## 3. Include the controller header

In:

```text
src/sdicamera+yolov5+hdmi.cpp
```

add:

```cpp
#include "aim_follow_controller.hpp"
```

## 4. Add tuning constants

Near the existing CAN/gimbal constants, add:

```cpp
const bool AIM_FOLLOW_CONTROL_ENABLE = true;
const float AIM_FOLLOW_TARGET_DISTANCE_M = 1.0f;
const float AIM_FOLLOW_YAW_KP = 38.0f;
const float AIM_FOLLOW_YAW_KD = 8.0f;
const float AIM_FOLLOW_PITCH_KP = 42.0f;
const float AIM_FOLLOW_PITCH_KD = 8.0f;
const bool AIM_FOLLOW_INVERT_YAW = false;
const bool AIM_FOLLOW_INVERT_PITCH = false;
const float AIM_FOLLOW_AIM_DEADZONE_NORM = 0.035f;
const float AIM_FOLLOW_MAX_CMD_STEP = 8.0f;
const float AIM_FOLLOW_DISTANCE_DEADBAND_M = 0.12f;
const float AIM_FOLLOW_FOLLOW_KP_RPM_PER_M = 180.0f;
const int AIM_FOLLOW_MIN_FOLLOW_RPM = 35;
const int AIM_FOLLOW_MAX_FOLLOW_RPM = 160;
const int AIM_FOLLOW_MOTOR1_FORWARD_SIGN = 1;
const int AIM_FOLLOW_MOTOR2_FORWARD_SIGN = 1;
const int AIM_FOLLOW_LOST_HOLD_FRAMES = 5;
const float AIM_FOLLOW_SELECTOR_MAX_CENTER_JUMP_NORM = 0.25f;
const float AIM_FOLLOW_SELECTOR_AREA_SWITCH_RATIO = 1.8f;
const int AIM_FOLLOW_SELECTOR_MAX_LOST_FRAMES = 5;
```

## 5. Build the post-processing chain

The runtime chain should be:

```text
YOLO post-process
  -> filter bicycle detections
  -> map boxes to HDMI/display coordinates
  -> TargetSelector
  -> distance estimation and distance filter
  -> AimFollowController
  -> send_chassis_can_mode + send_gimbal_can_mode
```

Important rule:

The same display-mapped target box should be used for drawing, distance estimation, and control. Do not estimate distance from the unmapped raw model box while drawing a mapped HDMI box.

## 6. Target continuity

Create:

```cpp
static aim_follow::TargetSelector target_selector;
```

Feed it `TargetCandidate` records built from display-mapped boxes:

```cpp
candidate.index = i;
candidate.center_x = display_box.x + display_box.width * 0.5f;
candidate.center_y = display_box.y + display_box.height * 0.5f;
candidate.area = display_box.width * display_box.height;
candidate.score = score_list[i];
```

Then:

```cpp
target_index = target_selector.select(target_candidates);
```

This avoids switching targets every frame when multiple bicycle boxes are visible.

## 7. Aim/follow control

Create:

```cpp
static aim_follow::AimFollowController follow_controller;
```

Recommended distance estimator:

```cpp
static aim_follow::MonocularDistanceEstimator distance_estimator;
```

Configure it with the same measured parameters used for distance display:

```cpp
aim_follow::DistanceEstimatorConfig distance_cfg;
distance_cfg.target_real_width_m = DISTANCE_TARGET_REAL_WIDTH_M;
distance_cfg.focal_length_px = DISTANCE_CAMERA_FOCAL_PX;
distance_cfg.min_box_width_px = DISTANCE_MIN_BOX_WIDTH_PX;
distance_cfg.filter_alpha = DISTANCE_FILTER_ALPHA;
distance_estimator.setConfig(distance_cfg);
```

For a valid target:

```cpp
const auto distance = distance_estimator.update(w);

aim_follow::TargetObservation obs;
obs.valid = true;
obs.center_x = cx;
obs.center_y = cy;
obs.box_width = w;
obs.distance_m = distance.filtered_distance_m;
obs.timestamp_s = std::chrono::duration<float>(
    std::chrono::steady_clock::now().time_since_epoch()).count();

const auto cmd = follow_controller.update(obs);
```

Send:

```cpp
send_chassis_can_mode(cmd.motor1_rpm, cmd.motor2_rpm, cmd.pitch, cmd.yaw, TRIGGER_STOP, CAN_CONTROL_ENABLE);
send_gimbal_can_mode(cmd.pitch, cmd.yaw, TRIGGER_STOP);
```

For lost target:

```cpp
aim_follow::TargetObservation obs;
obs.valid = false;
const auto cmd = follow_controller.update(obs);
```

The chassis command should return to zero after the configured lost hold frames.

## 8. Board validation

Use the acceptance checklist:

```text
aim_follow_control/ACCEPTANCE_CHECKLIST.md
```

Minimum evidence:

- local `aim_follow_controller_test passed`
- `MonocularDistanceEstimator` exists in the integrated `aim_follow_control` module
- 30TAI build succeeds
- `[AIM FOLLOW CONFIG]` appears in board `app.log`
- `[AIM FOLLOW]` appears when a bicycle target is visible
- `[DISTANCE DEBUG]` appears for distance display
- CAN `0x201` chassis frames and `0x38A` gimbal frames appear in `candump.log`
