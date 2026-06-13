# 30TAI Aim Follow Control

This is a clean, goal-focused project for the 30TAI target aiming and fixed-distance following task.

It is not a copy of the original PLin + SingleNet + HDMI demo project. Instead, it contains the reusable algorithm module, tests, integration notes, and board-side validation scripts needed to add:

- image-center based gimbal aiming
- monocular-distance based fixed-distance chassis following
- lightweight target continuity selection
- target-lost safety behavior
- 30TAI build, smoke-test, and log-analysis workflow

## Repository layout

```text
aim_follow_control/
  include/aim_follow_controller.hpp
  src/aim_follow_controller.cpp
  test/aim_follow_controller_test.cpp
  test/run_30tai_smoke_test.sh
  README.md
  BOARD_INTEGRATION.md
  ACCEPTANCE_CHECKLIST.md

tools/
  run_local_aim_follow_checks.ps1
  check_deploy_dry_run.ps1
  check_30tai_connection.ps1
  find_30tai_board.ps1
  deploy_30tai.ps1
  analyze_smoke_logs.ps1

integration/
  INTEGRATE_WITH_PLIN_PROJECT.md
```

## Local algorithm test

Run from this repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_local_aim_follow_checks.ps1
```

Expected evidence:

```text
aim_follow_controller_test passed
Smoke log analysis passed.
Local aim/follow checks passed.
```

## Integrating into the current PLin project

The current target application is still the original YOLOv5 + HDMI + CAN project. This repository provides the focused module and scripts that should be integrated into that application.

See:

```text
integration/INTEGRATE_WITH_PLIN_PROJECT.md
aim_follow_control/BOARD_INTEGRATION.md
aim_follow_control/ACCEPTANCE_CHECKLIST.md
```

## Board workflow

After this module has been integrated into the PLin application project, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1
powershell -ExecutionPolicy Bypass -File .\tools\check_deploy_dry_run.ps1 -ProjectDir <PLinProjectDir>
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -Build -SmokeTest -FetchLogs
powershell -ExecutionPolicy Bypass -File .\tools\analyze_smoke_logs.ps1 -LogDir <FetchedSmokeLogDir>
```

If the board IP may have changed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_30tai_board.ps1 -Subnet 192.168.125
```

## Current target defaults

The integrated PLin application should expose these tuning constants near the CAN/gimbal control constants:

```cpp
const bool AIM_FOLLOW_CONTROL_ENABLE = true;
const float AIM_FOLLOW_TARGET_DISTANCE_M = 1.0f;
const float AIM_FOLLOW_YAW_KP = 38.0f;
const float AIM_FOLLOW_YAW_KD = 8.0f;
const float AIM_FOLLOW_PITCH_KP = 42.0f;
const float AIM_FOLLOW_PITCH_KD = 8.0f;
const bool AIM_FOLLOW_INVERT_YAW = false;
const bool AIM_FOLLOW_INVERT_PITCH = false;
const float AIM_FOLLOW_DISTANCE_DEADBAND_M = 0.12f;
const float AIM_FOLLOW_FOLLOW_KP_RPM_PER_M = 180.0f;
const int AIM_FOLLOW_MAX_FOLLOW_RPM = 160;
const int AIM_FOLLOW_MOTOR1_FORWARD_SIGN = 1;
const int AIM_FOLLOW_MOTOR2_FORWARD_SIGN = 1;
```

First real-car tests should be done with the wheels lifted.
