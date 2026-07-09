# 30TAI Aim Follow Control

This is a clean, goal-focused project for the 30TAI target aiming and fixed-distance following task.

It is not a copy of the original PLin + SingleNet + HDMI demo project. Instead, it contains the reusable algorithm module, tests, integration notes, and board-side validation scripts needed to add:

- image-center based gimbal aiming
- monocular distance estimation from detection-box width
- fixed-distance chassis following
- lightweight target continuity selection
- target-lost safety behavior
- reusable distance-estimation and filtering module
- DetPost/DetPostZG reference model files for operator learning
- complete integrated PLin example project for rebuilding on another computer
- full compressed 30TAI/FPAI `deps` package for SDK headers and third-party libraries
- 30TAI build, smoke-test, and log-analysis workflow

Current completion and board-validation status is tracked in `STATUS.md`.

Algorithm selection, control logic, parameter tuning, and 30TAI deployment constraints are described in `docs/ALGORITHM_DESIGN.md`.

For rebuilding the same environment on another computer, see the Chinese setup guide:

```text
docs/SETUP_ANOTHER_PC_CN.md
```

## Repository layout

```text
aim_follow_control/
  include/aim_follow_controller.hpp
  src/aim_follow_controller.cpp
  test/aim_follow_controller_test.cpp
  test/aim_follow_synthetic_board_test.cpp
  test/run_30tai_smoke_test.sh
  README.md
  BOARD_INTEGRATION.md
  ACCEPTANCE_CHECKLIST.md

examples/
  plin_yolov5_hdmi_integrated/
    CMakeLists.txt
    src/
    configs/
    imodel/
    names/
    aim_follow_control/
    tools/
  detpost_reference_model/
    README.md
    configs/BY/sdicamera+yolov5+hdmi.yaml
    configs/ZG/sdicamera+yolov5+hdmi.yaml
    imodel/BY/yolov5s_plin_BY.json
    imodel/BY/yolov5s_plin_BY.raw
    imodel/ZG/yolov5s_plin_352x640_ZG.json
    imodel/ZG/yolov5s_plin_352x640_ZG.raw
    names/coco.names

third_party/
  modelzoo_utils/
    README_UPLOAD_CN.md
    include/
    pyrtutils/
    src/

sdk/
  fpai_demo_package_26010502_deps_parts/

tools/
  install_full_sdk_deps.ps1
  run_local_aim_follow_checks.ps1
  check_deploy_dry_run.ps1
  check_30tai_connection.ps1
  find_30tai_board.ps1
  deploy_30tai.ps1
  sync_to_plin_project.ps1
  verify_plin_integration.ps1
  run_board_vision_algorithm_test.ps1
  run_hdmi_synthetic_demo.ps1
  analyze_vision_algorithm_logs.ps1
  run_sdi_input_triage.ps1
  run_acceptance_preflight.ps1
  run_board_acceptance.ps1
  diagnose_30tai_can_bus.ps1
  diagnose_30tai_video_input.ps1
  diagnose_30tai_camera_path.ps1
  probe_30tai_sdi_modes.ps1
  dump_30tai_sdi_registers.ps1
  run_board_readiness_report.ps1
  run_board_synthetic_control_test.ps1
  write_acceptance_report.ps1
  analyze_smoke_logs.ps1

integration/
  INTEGRATE_WITH_PLIN_PROJECT.md
  PLIN_MAIN_HDMI_DRYRUN_PATCH.md

docs/
  ALGORITHM_DESIGN.md
  BOARD_ACCEPTANCE_RUNBOOK.md
  BUILD_FULL_PROJECT_ON_ANOTHER_PC_CN.md
  DETPOST_OPERATOR_LEARNING_NOTES_CN.md
  SETUP_ANOTHER_PC_CN.md
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

## Full Rebuild On Another Computer

For a complete rebuild, use:

```text
examples/plin_yolov5_hdmi_integrated/
sdk/fpai_demo_package_26010502_deps.zip
tools/install_full_sdk_deps.ps1
```

The SDK/deps archive is stored as split files under 100MB each. After cloning, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\install_full_sdk_deps.ps1
```

See `docs/BUILD_FULL_PROJECT_ON_ANOTHER_PC_CN.md` for the full Chinese rebuild guide.

## DetPost reference model

The repository includes a lightweight learning copy of the PLin DetPost reference model:

```text
examples/detpost_reference_model/
```

For the current 30TAI/ZG platform, the key files are:

```text
examples/detpost_reference_model/configs/ZG/sdicamera+yolov5+hdmi.yaml
examples/detpost_reference_model/imodel/ZG/yolov5s_plin_352x640_ZG.json
examples/detpost_reference_model/imodel/ZG/yolov5s_plin_352x640_ZG.raw
```

The ZG model contains:

```text
customop::DetPostZG
compile_target: @fpgat
```

See `docs/DETPOST_OPERATOR_LEARNING_NOTES_CN.md` for the Chinese learning notes. The current board may still need a matching DetPostZG bitstream; otherwise the runtime can report `No DetPost HardWare`.

## modelzoo_utils learning package

The repository also includes a lightweight copy of the 30TAI/FPAI `modelzoo_utils` helper package:

```text
third_party/modelzoo_utils/
```

It is intended for learning the example-project utility layer: C++ helper headers, `PicPre`, `NetInfo`, FPAI device helpers, pipeline actors, RTSP support, and Python runtime utilities. It does not replace the full 30TAI SDK or board-side dependency package.

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
The synthetic test uses the same monocular distance equation as the integrated app: `distance = target_real_width_m * focal_length_px / box_width_px`.

For the fastest HDMI-only demo before the real camera and CAN hardware are ready, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_hdmi_synthetic_demo.ps1 -ProjectDir <PLinProjectDir> -SshKey .\.ssh_board\id_ed25519_30tai
```

This enables VTC input, a synthetic bicycle target, and CAN dry-run. The HDMI panel should show target state, filtered distance, gimbal tracking, chassis tracking, and CAN output state without writing to `can0`.

Check CAN bus health before any command that may move hardware:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnose_30tai_can_bus.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

Do not send motion commands while `can0` is `ERROR-PASSIVE` or `BUS-OFF`.

To run the current board-readiness gates together and generate `readiness_report.md`:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_readiness_report.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

The report combines SSH, CAN, synthetic-controller, and real-video-input evidence.

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
