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

Actual 30TAI board build and runtime behavior are not yet verified.

Reason:

- The expected board IP `192.168.125.171` has not been reachable from the PC.
- TCP port `22` was not open during the latest checks.
- ARP did not show a reachable board entry.

Latest observed network evidence:

```text
No host with TCP port 22 open was found in 192.168.125.171-171.
```

This points to board power, Ethernet link, static IP, or network-segment state rather than a password problem.

## Next required board steps

After the board is reachable:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -Build -SmokeTest -FetchLogs
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
- Physical test confirms:
  - target farther than target distance -> forward command
  - target closer than target distance -> backward/stop command
  - target left/right/up/down -> gimbal command changes toward target
  - target lost -> chassis command returns to zero
