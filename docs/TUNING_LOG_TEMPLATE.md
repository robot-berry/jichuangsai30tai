# 30TAI Aim/Follow Tuning Log Template

Copy this file for each real-car tuning session.

Suggested filename:

```text
tuning_log_YYYYMMDD_HHMM.md
```

## 1. Session Info

| Item | Value |
| --- | --- |
| Date/time |  |
| Operator |  |
| Board IP | `192.168.125.171` |
| Git commit |  |
| Integrated PLin project path/archive |  |
| Target type |  |
| Target real width |  |
| Camera/lens state |  |
| Test site |  |

## 2. Build And Deployment Evidence

| Item | Result / Path |
| --- | --- |
| `run_acceptance_preflight.ps1` result |  |
| `run_board_acceptance.ps1` result |  |
| Fetched smoke-log directory |  |
| `acceptance_report.md` path |  |
| Saved HDMI frame path |  |
| Notes |  |

## 3. Final Parameters

Record the parameter values used for this test.

```cpp
const bool AIM_FOLLOW_CONTROL_ENABLE = true;
const float AIM_FOLLOW_TARGET_DISTANCE_M = ;
const float AIM_FOLLOW_YAW_KP = ;
const float AIM_FOLLOW_YAW_KD = ;
const float AIM_FOLLOW_PITCH_KP = ;
const float AIM_FOLLOW_PITCH_KD = ;
const bool AIM_FOLLOW_INVERT_YAW = ;
const bool AIM_FOLLOW_INVERT_PITCH = ;
const float AIM_FOLLOW_AIM_DEADZONE_NORM = ;
const float AIM_FOLLOW_MAX_CMD_STEP = ;
const float AIM_FOLLOW_DISTANCE_DEADBAND_M = ;
const float AIM_FOLLOW_FOLLOW_KP_RPM_PER_M = ;
const int AIM_FOLLOW_MIN_FOLLOW_RPM = ;
const int AIM_FOLLOW_MAX_FOLLOW_RPM = ;
const int AIM_FOLLOW_MOTOR1_FORWARD_SIGN = ;
const int AIM_FOLLOW_MOTOR2_FORWARD_SIGN = ;
const int AIM_FOLLOW_LOST_HOLD_FRAMES = ;
```

Distance-estimation parameters:

| Parameter | Value |
| --- | ---: |
| Target real width in meters |  |
| Focal length in pixels |  |
| Distance filter alpha |  |
| Nominal target distance in meters |  |

## 4. Lifted-Wheel Test

### 4.1 Gimbal Direction

| Test | Target position | Expected behavior | Result | Notes |
| --- | --- | --- | --- | --- |
| G1 | Right of center | yaw moves target toward center |  |  |
| G2 | Left of center | yaw moves target toward center |  |  |
| G3 | Above center | pitch moves target toward center |  |  |
| G4 | Below center | pitch moves target toward center |  |  |

Direction fixes applied:

| Parameter | Old value | New value | Reason |
| --- | --- | --- | --- |
| `AIM_FOLLOW_INVERT_YAW` |  |  |  |
| `AIM_FOLLOW_INVERT_PITCH` |  |  |  |

### 4.2 Chassis Direction

| Test | Target distance/state | Expected command | Result | Notes |
| --- | --- | --- | --- | --- |
| C1 | Farther than target distance | forward RPM |  |  |
| C2 | Within deadband | zero or near-zero RPM |  |  |
| C3 | Closer than target distance | backward RPM |  |  |
| C4 | Target removed | RPM returns to zero |  |  |

Motor direction fixes applied:

| Parameter | Old value | New value | Reason |
| --- | --- | --- | --- |
| `AIM_FOLLOW_MOTOR1_FORWARD_SIGN` |  |  |  |
| `AIM_FOLLOW_MOTOR2_FORWARD_SIGN` |  |  |  |

## 5. Distance Test

Use the same target and camera setup for all rows.

| Test | True distance (m) | Displayed distance (m) | Error (m) | Stable? | Notes |
| --- | ---: | ---: | ---: | --- | --- |
| D1 | 0.5 |  |  |  |  |
| D2 | 1.0 |  |  |  |  |
| D3 | 1.5 |  |  |  |  |
| D4 | 2.0 |  |  |  |  |

Distance decision:

- Keep current focal length:
- Recalibrate focal length:
- Adjust distance filter alpha:
- Adjust target width:

## 6. Ground Following Test

| Test | Target motion | Expected behavior | Result | Notes |
| --- | --- | --- | --- | --- |
| R1 | Static at target distance | vehicle stays still |  |  |
| R2 | Target moves away slowly | vehicle follows forward |  |  |
| R3 | Target moves closer slowly | vehicle slows, stops, or backs away |  |  |
| R4 | Target shifts left/right | gimbal corrects; chassis remains distance-driven |  |  |
| R5 | Target disappears | chassis stops |  |  |

## 7. Observed Issues

| Issue | Evidence | Cause | Fix | Retest result |
| --- | --- | --- | --- | --- |
|  |  |  |  |  |

## 8. Final Acceptance

| Requirement | Evidence | Result |
| --- | --- | --- |
| Module builds locally |  |  |
| Integrated PLin project passes verification |  |  |
| PLin project builds on 30TAI |  |  |
| Aim/follow logs appear |  |  |
| Distance debug logs appear |  |  |
| CAN 0x201 appears |  |  |
| CAN 0x38A appears |  |  |
| Lifted-wheel tests pass |  |  |
| Ground tests pass |  |  |

Final conclusion:

```text
PASS / FAIL:
Reason:
Remaining risk:
```

