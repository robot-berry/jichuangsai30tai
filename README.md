# 30TAI Aim Follow Control

This is a clean, goal-focused project for the 30TAI target aiming and fixed-distance following task.

It is not a copy of the original PLin + SingleNet + HDMI demo project. Instead, it contains the reusable algorithm module, tests, integration notes, and board-side validation scripts needed to add:

- image-center based gimbal aiming
- monocular-distance based fixed-distance chassis following
- lightweight target continuity selection
- target-lost safety behavior
- 30TAI build, smoke-test, and log-analysis workflow

Current completion and board-validation status is tracked in `STATUS.md`.

Algorithm selection, control logic, parameter tuning, and 30TAI deployment constraints are described in `docs/ALGORITHM_DESIGN.md`.

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
  sync_to_plin_project.ps1
  verify_plin_integration.ps1
  run_acceptance_preflight.ps1
  run_board_acceptance.ps1
  diagnose_30tai_video_input.ps1
  run_board_synthetic_control_test.ps1
  write_acceptance_report.ps1
  analyze_smoke_logs.ps1

integration/
  INTEGRATE_WITH_PLIN_PROJECT.md

docs/
  ALGORITHM_DESIGN.md
  BOARD_ACCEPTANCE_RUNBOOK.md
  TUNING_LOG_TEMPLATE.md

STATUS.md
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

To sync this module into an existing PLin application project:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\sync_to_plin_project.ps1 -ProjectDir <PLinProjectDir>
```

Use `-DryRun` first to inspect the copy and CMake checks:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\sync_to_plin_project.ps1 -ProjectDir <PLinProjectDir> -DryRun
```

After syncing, run the integration verifier:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_plin_integration.ps1 -ProjectDir <PLinProjectDir>
```

See:

```text
integration/INTEGRATE_WITH_PLIN_PROJECT.md
aim_follow_control/BOARD_INTEGRATION.md
aim_follow_control/ACCEPTANCE_CHECKLIST.md
```

## Board workflow

Before connecting the board, run the complete preflight against the integrated PLin project:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_acceptance_preflight.ps1 -ProjectDir <PLinProjectDir>
```

After the board is powered and SSH is reachable, include the board connection check:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_acceptance_preflight.ps1 -ProjectDir <PLinProjectDir> -CheckBoard
```

After this module has been integrated into the PLin application project, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1
powershell -ExecutionPolicy Bypass -File .\tools\check_deploy_dry_run.ps1 -ProjectDir <PLinProjectDir>
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -Build -LowMemoryBuild -UseBoardReferenceModel -SmokeTest -FetchLogs
powershell -ExecutionPolicy Bypass -File .\tools\analyze_smoke_logs.ps1 -LogDir <FetchedSmokeLogDir>
```

Or run the real-board acceptance wrapper after SSH is reachable:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_acceptance.ps1 -ProjectDir <PLinProjectDir> -SshKey .\.ssh_board\id_ed25519_30tai
```

The real-board wrapper writes `acceptance_report.md` into the fetched smoke-log directory after log analysis passes.
If no `-SshKey` is provided, the scripts keep the normal SSH password-login behavior.

If the app starts but no `[AIM FOLLOW]` or `[DISTANCE DEBUG]` logs appear, isolate the image input path:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnose_30tai_video_input.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

This compares the unmodified board PLin demo, the integrated board-model config, and a `vtc:true` test-pattern config.

When the real SDI input is unavailable, the controller itself can still be verified on 30TAI with synthetic target observations:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_synthetic_control_test.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

This builds `aim_follow_control` on the board and verifies forward/backward distance following, yaw/pitch aiming, lost-target stop behavior, and the generated `0x201` / `0x38A` CAN payload bytes. It does not send CAN by default. Use `-SendCan -ConfigureCan` only with the wheels lifted and the CAN bus confirmed healthy.

For the final lifted-wheel and ground tests, follow `docs/BOARD_ACCEPTANCE_RUNBOOK.md`.
Use `docs/TUNING_LOG_TEMPLATE.md` to record final parameters and physical-test evidence.

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
