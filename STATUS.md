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
- `TargetSelector` implements lightweight target continuity.
- Local unit test covers:
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

## Next required board steps

After the board is reachable:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
powershell -ExecutionPolicy Bypass -File .\tools\run_board_acceptance.ps1 -ProjectDir <PLinProjectDir> -SshKey .\.ssh_board\id_ed25519_30tai
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
