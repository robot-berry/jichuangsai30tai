# Aim/Follow 30TAI Acceptance Checklist

This checklist defines the evidence needed before the aim/follow goal can be considered complete on the 30TAI board.

## 1. Local engineering checks

Run from the project root on Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_local_aim_follow_checks.ps1
```

Required evidence:

- `aim_follow_controller_test passed`
- smoke log analyzer self-check passes

After this module is integrated into a PLin application project, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_deploy_dry_run.ps1 -ProjectDir <PLinProjectDir>
```

Required evidence:

- deploy dry run prints build, smoke test, and log fetch commands
- deploy archive excludes local build artifacts and fetched smoke logs, including `build`, `aim_follow_control/build*`, and `board_smoke_logs`

## 2. Board connectivity

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1
```

Required evidence:

- PC has an IPv4 address on the same board network, for example `192.168.125.1/24`
- ping or TCP port 22 reaches the board IP
- SSH reaches `root@192.168.125.171`

If ping, TCP 22, and ARP all fail, the likely issue is board power, Ethernet link, IP address, or network segment rather than password.

If the board IP may have changed, scan the local board subnet:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_30tai_board.ps1 -Subnet 192.168.125
```

Any host reported as `OPEN <ip>:22` is an SSH candidate. Re-run the deploy command with `-BoardIp <ip>` if the board is found at a different address.

## 3. Board build and smoke test

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -Build -SmokeTest -FetchLogs
```

Required evidence:

- `build_30tai.sh` finishes without CMake or compile errors
- the runtime bundle is generated under `build/ZG/deploy/ZG`
- the smoke test runs the deployed `sdicamera+yolov5+hdmi`
- logs are fetched into `board_smoke_logs/smoke_YYYYMMDD_HHMMSS`

## 4. Smoke log analysis

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\analyze_smoke_logs.ps1
```

Required evidence:

- `[AIM FOLLOW CONFIG]` appears in `app.log`
- `[AIM FOLLOW]` appears when a bicycle target is visible
- `[DISTANCE DEBUG]` appears when distance is displayed
- `candump.log` contains CAN ID `0x201` for chassis control
- `candump.log` contains CAN ID `0x38A` for gimbal control

## 5. Physical behavior checks

Perform the first motion test with the car lifted or wheels off the ground.

Required evidence:

- target near the configured distance, default 1.0 m: motor speed is near zero
- target farther than the configured distance: chassis command moves forward
- target closer than the configured distance: chassis command moves backward or stops according to tuning
- target moves right/left: gimbal yaw command changes toward the target
- target moves up/down: gimbal pitch command changes toward the target
- target is lost: chassis command returns to zero

## 6. Current status

The local module, deploy dry run, and log analyzer are ready. The remaining unverified item is actual 30TAI board build/runtime behavior, because the board IP has not been reachable from the PC during the latest checks.
