# 30TAI Board Acceptance Runbook

This runbook describes the final board-side acceptance steps for the 30TAI aim/follow project.

Use it after the `aim_follow_control` module has been integrated into the PLin application project.

## 1. Preconditions

Required hardware:

- 30TAI board
- SDI camera
- HDMI display or capture path used by the PLin project
- CAN bus connected to the chassis and gimbal controller
- Target object with known width
- Wheels lifted for the first motion test

Required software state:

- The PLin application project contains `aim_follow_control/`.
- `CMakeLists.txt` includes `aim_follow_control/src/aim_follow_controller.cpp`.
- `CMakeLists.txt` includes `${CMAKE_CURRENT_SOURCE_DIR}/aim_follow_control/include`.
- The main program includes `aim_follow_controller.hpp`.
- The main program emits `[AIM FOLLOW CONFIG]`, `[AIM FOLLOW]`, and `[DISTANCE DEBUG]` logs.
- The board uses the expected CAN bitrate and interface name.

Run the host-side preflight first:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_acceptance_preflight.ps1 -ProjectDir <PLinProjectDir>
```

## 2. Board Network Check

Default board address:

```text
192.168.125.171
```

Check the board connection:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1 -BoardIp 192.168.125.171
```

If a temporary SSH key has been installed on the board, use:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1 -BoardIp 192.168.125.171 -SshKey .\.ssh_board\id_ed25519_30tai
```

Expected evidence:

- PC has an IPv4 address in `192.168.125.0/24`.
- Ping is successful or ARP shows the board.
- TCP port `22` is reachable.
- SSH can connect as `root`.

If the board cannot be found, scan the subnet:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_30tai_board.ps1 -Subnet 192.168.125
```

Do not run the real-board acceptance script until SSH is reachable.

## 3. Board Readiness Report

Before full closed-loop testing, run the combined readiness report:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_readiness_report.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

The report combines:

- SSH reachability,
- CAN bus health,
- controller-only synthetic target behavior,
- real SDI/video input diagnosis.

The vehicle is ready for lifted-wheel target-visible tests only when the report says:

```text
Overall ready for full closed-loop test: YES
```

If the report says `NO`, follow its interpretation section before sending CAN motion commands.

## 4. Real-Board Automated Acceptance

Run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_acceptance.ps1 -ProjectDir <PLinProjectDir> -SshKey .\.ssh_board\id_ed25519_30tai
```

This command performs:

1. Host-side preflight with board connection check.
2. Archive and upload of the integrated PLin project.
3. Board-side `build_30tai.sh`.
4. Board-side smoke test.
5. Log fetch from the board.
6. Smoke-log analysis.
7. Generation of `acceptance_report.md`.

On the current 30TAI board, use the low-memory build and board reference model path. The wrapper enables these options by default. For lower-level manual deployment, pass:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -Build -LowMemoryBuild -UseBoardReferenceModel -SmokeTest -FetchLogs -SshKey .\.ssh_board\id_ed25519_30tai
```

The low-memory build enables `/swapfile` and compiles with `-O0 -g0 -j1`. The board reference model avoids the DetPost hardware path on boards whose bitstream does not expose DetPostZG.
Omit `-SshKey` if password login is preferred.
The deploy scripts touch unpacked source files on the board to avoid clock-skew rebuild loops when the PC file timestamps are newer than the board clock.

Required generated evidence:

| Evidence file | Required content |
| --- | --- |
| `app.log` | `[AIM FOLLOW CONFIG]` |
| `app.log` | `[AIM FOLLOW]` when the target is visible |
| `app.log` | `[DISTANCE DEBUG]` when distance is displayed |
| `candump.log` | CAN ID `0x201` chassis command |
| `candump.log` | CAN ID `0x38A` gimbal command |
| `acceptance_report.md` | Result `PASS` |

If the app starts but logs `ImageMake Timeout` and `accept 0 data`, troubleshoot camera/SDI/HDMI input before judging the aim/follow module. In that state the binary is running, but no valid frame reaches YOLO post-processing.

To collect a repeatable video-input diagnosis, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnose_30tai_video_input.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

The diagnostic runs three cases:

| Case | Purpose | Expected interpretation |
| --- | --- | --- |
| `original_plin` | Unmodified board PLin demo | If this also reports `accept 0 data`, the fault is before the added aim/follow module |
| `integrated_board_model` | Deploy-style integrated app path, when a deploy bundle exists | Confirms whether a staged deploy bundle starts |
| `integrated_direct` | Current direct board build at `build/ZG/sdicamera+yolov5+hdmi` | Confirms whether the rebuilt aim/follow binary starts and reaches the same input path |
| `integrated_vtc` | Same app with `camera.vtc: true` test-pattern input | Separates real SDI input loss from ImageMake/test-pattern behavior |

Current observed board evidence:

- `original_plin` and `integrated_direct` both start actors but report `ImageMake Timeout` and `accept 0 data`.
- `integrated_direct` emits `[AIM FOLLOW CONFIG]`, proving the deployed binary contains the aim/follow module.
- `integrated_board_model` can be skipped on board images that build directly into `build/ZG/` instead of producing a `deploy/ZG/` bundle.
- `integrated_vtc` runs without `ImageMake Timeout`, but it does not provide a real target frame, so it cannot prove final target following.
- `/dev/video0` is not a normal V4L2 capture device on this board image; the PLin demo depends on the board-specific PL camera path.

## 5. Controller-Only Board Test

Before sending any CAN frame, check the CAN bus:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnose_30tai_can_bus.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

Current observed board evidence:

- `can0` bitrate is `250000`.
- `can0` state is `ERROR-PASSIVE`.
- tx error counter is `128`, rx error counter is `0`.
- Do not send motion commands in this state.

Typical checks for `ERROR-PASSIVE`:

- vehicle controller and gimbal controller are powered,
- CANH and CANL are not reversed,
- board and controller share ground,
- both ends have correct 120-ohm termination,
- the controller has entered CAN bus control mode,
- bitrate matches the actual bus speed.

If real SDI input is not available, run the synthetic controller test before any motion test:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_synthetic_control_test.ps1 -SshKey .\.ssh_board\id_ed25519_30tai
```

This test uploads only `aim_follow_control`, builds it on 30TAI, and feeds synthetic target observations to the controller.

Expected evidence in `synthetic_control.log`:

| Evidence | Meaning |
| --- | --- |
| `[SYNTH SUMMARY] ... result=PASS` | Controller produced all required behavior classes |
| `target_far_forward` with positive motor RPM | Distance-following forward command works |
| `target_close_backward` with negative motor RPM | Distance-following backward command works |
| `target_right_yaw` with yaw different from center | Aiming yaw command responds to image error |
| `target_up_pitch` with pitch different from center | Aiming pitch command responds to image error |
| `[SYNTH CAN 0x201]` | Chassis CAN payload bytes are generated |
| `[SYNTH CAN 0x38A]` | Gimbal CAN payload bytes are generated |

By default this test does not send CAN frames. To send frames, use:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_board_synthetic_control_test.ps1 -SshKey .\.ssh_board\id_ed25519_30tai -ConfigureCan -SendCan
```

Only use `-SendCan` when:

- wheels are lifted,
- the CAN bus is wired to the correct controller,
- `ip -details link show can0` is `ERROR-ACTIVE`,
- the vehicle can be stopped immediately.

If `can0` is `ERROR-PASSIVE` or bus errors increase, fix CAN wiring, termination, bitrate, and controller mode before sending motion commands.

## 6. Safe Motion Test

The first real motion test must be done with wheels lifted.

### 6.1 Gimbal Direction Test

Place the target in different image regions and observe the gimbal command.

| Test | Target position | Expected result | Parameter to change if wrong |
| --- | --- | --- | --- |
| G1 | Target right of image center | yaw command moves target toward center | `AIM_FOLLOW_INVERT_YAW` |
| G2 | Target left of image center | yaw command moves target toward center | `AIM_FOLLOW_INVERT_YAW` |
| G3 | Target above image center | pitch command moves target toward center | `AIM_FOLLOW_INVERT_PITCH` |
| G4 | Target below image center | pitch command moves target toward center | `AIM_FOLLOW_INVERT_PITCH` |

Pass condition:

- The target center converges toward the image center.
- The gimbal does not oscillate violently near the center.
- `[AIM FOLLOW]` logs show nonzero `ex` or `ey` when the target is off center.

### 6.2 Chassis Direction Test

Keep the wheels lifted. Use a known-width target and move it to known distances.

| Test | Target distance | Expected command |
| --- | ---: | --- |
| C1 | Farther than target distance + deadband | forward RPM command |
| C2 | Within target distance deadband | zero or near-zero RPM command |
| C3 | Closer than target distance - deadband | backward RPM command |
| C4 | Target removed | chassis RPM returns to zero after lost hold frames |

Pass condition:

- `motor1` and `motor2` in `[AIM FOLLOW]` logs match the expected direction.
- `candump.log` contains changing `0x201` frames during distance changes.
- Target loss does not keep the chassis moving.

If only one motor moves or the vehicle rotates in place:

- Check `AIM_FOLLOW_MOTOR1_FORWARD_SIGN`.
- Check `AIM_FOLLOW_MOTOR2_FORWARD_SIGN`.
- Check the chassis CAN wiring and motor controller mode.

## 7. Ground Test

Only run this after the lifted-wheel test passes.

Start with low speed limits:

```cpp
const int AIM_FOLLOW_MAX_FOLLOW_RPM = 100;
const float AIM_FOLLOW_FOLLOW_KP_RPM_PER_M = 120.0f;
```

Then increase gradually if the motion is too slow.

Ground-test matrix:

| Test | Target motion | Expected behavior |
| --- | --- | --- |
| R1 | Static target at target distance | chassis stays still; gimbal keeps target centered |
| R2 | Target moves away slowly | chassis follows forward |
| R3 | Target moves closer slowly | chassis slows, stops, or backs away depending on distance |
| R4 | Target shifts left/right | gimbal corrects first; chassis command stays distance-driven |
| R5 | Target disappears | chassis stops; gimbal returns or holds safely according to config |

Acceptance requires all five tests to pass.

## 8. Evidence Archive

After a successful run, keep the fetched smoke-log directory.

It should contain:

```text
app.log
candump.log
summary.txt
acceptance_report.md
```

Recommended final evidence package:

- Latest Git commit hash of `jichuangsai30tai_aim_follow`.
- Integrated PLin project commit or archive name.
- Latest `readiness_report.md`.
- `acceptance_report.md`.
- Synthetic controller log with `[SYNTH SUMMARY] ... result=PASS`.
- At least one saved HDMI frame showing the detection box and distance text.
- Filled tuning log copied from `docs/TUNING_LOG_TEMPLATE.md`.

## 9. Completion Criteria

The project can be treated as accepted only when:

- The module builds locally.
- The integrated PLin project passes `verify_plin_integration.ps1`.
- The PLin project builds on 30TAI.
- The board smoke test produces all required logs.
- `acceptance_report.md` reports `PASS`.
- Lifted-wheel gimbal and chassis tests pass.
- Ground following tests pass at the selected target distance.
