# Project Status

Last updated: 2026-06-13

## What this repository is

This repository is a clean, goal-focused 30TAI aim/follow control project.

It is not a full copy of the original PLin + SingleNet + HDMI demo application. It provides:

- reusable C++ aim/follow control module
- target continuity selector
- local unit test
- PLin integration instructions
- 30TAI connection, deploy, smoke-test, and log-analysis scripts

## Completed

- `aim_follow_control` module created as an independent C++ library.
- `AimFollowController` implements:
  - image-error based gimbal yaw/pitch command
  - fixed-distance chassis RPM command
  - target-lost safety behavior
  - command deadzone and step limiting
- `MonocularDistanceEstimator` implements:
  - known-width pinhole-camera distance estimation
  - first-order low-pass distance filtering
  - invalid-box handling that preserves the last filtered value for display/debug continuity
- `TargetSelector` implements lightweight target continuity.
- Local unit test covers:
  - monocular distance estimation from detection-box width
  - distance low-pass filtering and invalid-box behavior
  - centered target hold
  - target too far -> forward command
  - target too close -> backward command
  - target right/up -> gimbal command change
  - target lost -> chassis stop behavior
  - target continuity selection
- Integration documentation is available under `integration/`.
- Board-side helper scripts are available under `tools/`.
- Final board acceptance runbook is available under `docs/BOARD_ACCEPTANCE_RUNBOOK.md`.
- Real-car tuning log template is available under `docs/TUNING_LOG_TEMPLATE.md`.
- One-command acceptance preflight is available:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_acceptance_preflight.ps1 -ProjectDir <PLinProjectDir>
```

- Sync workflow is available:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\sync_to_plin_project.ps1 -ProjectDir <PLinProjectDir>
```

## Verified locally

Local checks have passed:

```text
aim_follow_controller_test passed
Smoke log analysis passed.
Local aim/follow checks passed.
```

The deploy dry run against the currently integrated PLin project also passed and printed the expected upload, build, smoke-test, and log-fetch commands.

## Not yet fully verified

Actual 30TAI board build and startup behavior have been partially verified.

Verified on 30TAI:

- SSH reached `192.168.125.171` after using the correct board Ethernet interface.
- The PLin project built on board after enabling `/swapfile` and using single-thread low-memory compile flags.
- The runtime bundle was staged successfully.
- The app started on board and emitted `[AIM FOLLOW CONFIG] startup ...`, proving the integrated aim/follow module was present in the deployed binary.
- After switching the PLin main program to use `MonocularDistanceEstimator`, the app rebuilt on 30TAI and produced `build/ZG/sdicamera+yolov5+hdmi`.
- Direct board startup of that rebuilt binary emitted `[AIM FOLLOW CONFIG]` and reached `All actors started...`.
- A board clock skew issue caused uploaded source files to appear from the future and made `stage_bundle` repeat compilation; deploy scripts now touch unpacked files on the board after extraction.

Current remaining blocker:

```text
ZG330 ImageMake Timeout after 1000 ms, data_in_num=0
Image size is 640 * 352, but accept 0 data
```

This indicates that the application is running, but valid camera/image input is not reaching ImageMake, so YOLO post-processing and target-driven aim/follow commands have not yet been exercised.

Board-specific notes from the current 30TAI:

- The default `yolov5s_plin_352x640_ZG` model expects DetPost hardware and aborts on this board with `No DetPost HardWare`.
- The board's reference `yolov5s_352x640_ZG` model with `detpost: false` starts successfully.
- The board does not provide `candump`, so CAN evidence currently comes from app logs unless CAN utilities are installed.
- `tools/diagnose_30tai_video_input.ps1` compares the unmodified PLin demo, the integrated board-model config, and a `camera.vtc: true` test-pattern config.
- `tools/diagnose_30tai_camera_path.ps1` collects lower-level device-node, V4L2, dmesg, and config evidence for the camera/PL input path.
- Latest video-input diagnosis showed:
  - `original_plin`: actors start, then `ImageMake Timeout` / `accept 0 data`
  - `integrated_board_model`: deploy-style executable path was not present on the current board image
  - `integrated_direct`: actors start, `[AIM FOLLOW CONFIG]` appears, then the same `ImageMake Timeout` / `accept 0 data`
  - `integrated_direct_vtc`: actors start, `[AIM FOLLOW CONFIG]` appears, and no `ImageMake Timeout` / `accept 0 data` occurs during the short test
  - `integrated_vtc`: deploy-style VTC path can be skipped on board images without a `deploy/ZG/` bundle
- This points to real SDI/camera input state as the current blocker before closed-loop target following can be verified.
- Latest camera-path diagnosis showed:
  - `/dev/video0`: `mvx / Linlon Video device`
  - `/dev/video0` default format: `2x2`
  - media nodes: none
  - I2C nodes: none
  - active config camera type: `hdmi`
- This means `/dev/video0` should not be treated as the physical camera input for this project; the camera must feed the PLin HDMI/SDI path expected by the current bitstream and YAML config.
- The camera is physically connected to `SDI_IN_0`, which matches the current single-input code path using `camera_id = 0` and the first SDICamera base address. The remaining camera blocker is therefore signal/path validity on `SDI_IN_0`, not an intentional software switch to another camera index.
- Because `integrated_direct_vtc` runs without ImageMake zero-data errors, the deployed app and downstream ImageMake/model/display pipeline are not the primary blocker. The failing path is the real external signal entering `SDI_IN_0`.
- `tools/probe_30tai_sdi_modes.ps1` was added to run short tests with temporary camera width/height/fps configs. Latest probe result:
  - `1920x1080@60`: actors start, but `ImageMake Timeout` / `accept 0 data`
  - `1920x1080@30`: actors start, but `ImageMake Timeout` / `accept 0 data`
  - `1280x720@60`: actors start, but `ImageMake Timeout` / `accept 0 data`
  - `1280x720@30`: actors start, but `ImageMake Timeout` / `accept 0 data`
- This makes a simple YAML resolution/fps mismatch less likely; continue checking SDI signal lock, cable/source output, and bitstream support for `SDI_IN_0`.
- `tools/run_board_synthetic_control_test.ps1` was added for controller-only board validation when the camera path is unavailable.
- Latest synthetic control test built `aim_follow_control` directly on 30TAI and passed:
  - synthetic box width -> monocular distance estimate -> follow controller path
  - far synthetic target -> positive chassis RPM
  - close synthetic target -> negative chassis RPM
  - right/up synthetic target -> yaw/pitch command changes
  - lost synthetic target -> chassis stop
  - generated `0x201` and `0x38A` CAN payload bytes
- The synthetic test did not send CAN frames by default.
- `tools/diagnose_30tai_can_bus.ps1` was added for repeatable CAN health checks.
- Latest CAN diagnosis showed:
  - before state: `ERROR-ACTIVE`
  - after state: `ERROR-ACTIVE`
  - bitrate: `250000`
  - tx error counter: `0`
  - rx error counter: `0`
  - tx packets: `2`, rx packets: `6`
- This indicates that the CAN controller is currently healthy enough for lifted-wheel neutral-frame or carefully limited motion tests.
- `tools/run_board_readiness_report.ps1` was added to combine SSH, CAN, synthetic-controller, and video-input gates into one `readiness_report.md`.
- Latest full readiness report result was still `NO` until the camera input path is fixed:
  - SSH connection: PASS
  - CAN bus healthy: PASS in the latest standalone CAN diagnosis
  - controller synthetic behavior: PASS
  - real video input: FAIL

## Next required board steps

After the board is reachable:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
powershell -ExecutionPolicy Bypass -File .\tools\run_board_acceptance.ps1 -ProjectDir <PLinProjectDir> -SshKey .\.ssh_board\id_ed25519_30tai
```

If the app starts but no target logs appear, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnose_30tai_video_input.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

To verify the aim/follow controller on 30TAI without using the camera path:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_synthetic_control_test.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

To check whether CAN is safe for lifted-wheel motion tests:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnose_30tai_can_bus.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

To generate the combined readiness report:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_readiness_report.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

The lower-level manual sequence remains:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -Build -LowMemoryBuild -UseBoardReferenceModel -SmokeTest -FetchLogs -SshKey .\.ssh_board\id_ed25519_30tai
powershell -ExecutionPolicy Bypass -File .\tools\analyze_smoke_logs.ps1 -LogDir <FetchedSmokeLogDir>
```

If the board IP may have changed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_30tai_board.ps1 -Subnet 192.168.125
```

## Completion criteria

The goal is complete only after all of these are true:

- PLin project builds successfully on 30TAI.
- Runtime starts with the integrated aim/follow module.
- `[AIM FOLLOW CONFIG]` appears in board `app.log`.
- `[AIM FOLLOW]` appears when the target is visible.
- `[DISTANCE DEBUG]` appears when distance is displayed.
- `candump.log` contains chassis CAN ID `0x201`.
- `candump.log` contains gimbal CAN ID `0x38A`.
- `acceptance_report.md` is generated from the fetched board logs.
- Physical test confirms:
  - target farther than target distance -> forward command
  - target closer than target distance -> backward/stop command
  - target left/right/up/down -> gimbal command changes toward target
  - target lost -> chassis command returns to zero
