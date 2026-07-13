# 距离跟随与自动瞄准控制模块

该文件夹是为当前 30TAI 工程新增的独立控制算法模块，目标是把论文和开源工程中常见的“图像视觉伺服 + 固定距离跟随”思想落到现有项目中。

## 模块作用

- 云台自动瞄准：根据 YOLO 检测框中心点和 HDMI 画面中心点的误差，输出 `pitch/yaw` 位置命令。
- 小车固定距离跟随：根据单目测距结果和期望距离的误差，输出 `motor1/motor2` 转速命令。
- 轻量目标连续选择：多目标时优先保持上一帧附近的目标，避免每帧简单选择最大框导致控制方向跳变。
- 目标丢失保护：目标丢失若干帧后，底盘转速归零，云台 yaw 缓慢回中。

## 输入与输出

输入结构体：`aim_follow::TargetObservation`

- `valid`：是否检测到目标。
- `center_x / center_y`：目标检测框中心点，使用 HDMI 显示坐标。
- `box_width`：检测框宽度，可保留给后续调试。
- `distance_m`：滤波后的目标距离，单位 m。
- `timestamp_s`：当前时间戳，单位 s，用于 PD 微分项。

输出结构体：`aim_follow::ControlOutput`

- `motor1_rpm / motor2_rpm`：底盘两路电机转速。
- `pitch / yaw`：云台位置命令，默认范围 100 到 200，中心值 150。
- `norm_error_x / norm_error_y`：归一化图像误差，便于串口/日志调试。
- `distance_error_m`：当前距离与期望距离的误差。

## 30TAI 主程序接入方式

在 `src/sdicamera+yolov5+hdmi.cpp` 中加入头文件：

```cpp
#include "aim_follow_controller.hpp"
```

在后处理 lambda 内部或文件静态区创建控制器：

```cpp
static aim_follow::ControlConfig follow_cfg;
static aim_follow::AimFollowController follow_controller(follow_cfg);
```

建议在第一次使用前设置参数：

```cpp
follow_cfg.frame_width = yolov5_cfg.FRAME_W;
follow_cfg.frame_height = yolov5_cfg.FRAME_H;
follow_cfg.center_yaw = CENTER_YAW;
follow_cfg.center_pitch = CENTER_PITCH;
follow_cfg.min_yaw = CMD_YAW_MIN;
follow_cfg.max_yaw = CMD_YAW_MAX;
follow_cfg.min_pitch = CMD_PITCH_MIN;
follow_cfg.max_pitch = CMD_PITCH_MAX;
follow_cfg.target_distance_m = 1.0f;
follow_cfg.yaw_kp = AIM_FOLLOW_YAW_KP;
follow_cfg.pitch_kp = AIM_FOLLOW_PITCH_KP;
follow_cfg.invert_yaw = AIM_FOLLOW_INVERT_YAW;
follow_cfg.invert_pitch = AIM_FOLLOW_INVERT_PITCH;
follow_cfg.distance_deadband_m = AIM_FOLLOW_DISTANCE_DEADBAND_M;
follow_cfg.follow_kp_rpm_per_m = AIM_FOLLOW_FOLLOW_KP_RPM_PER_M;
follow_cfg.max_follow_rpm = AIM_FOLLOW_MAX_FOLLOW_RPM;
follow_cfg.motor1_forward_sign = 1;
follow_cfg.motor2_forward_sign = 1;
follow_controller.setConfig(follow_cfg);
```

检测到目标后，构造输入并发送 CAN：

```cpp
aim_follow::TargetObservation obs;
obs.valid = true;
obs.center_x = cx;
obs.center_y = cy;
obs.box_width = w;
obs.distance_m = filtered_distance_m;
obs.timestamp_s = std::chrono::duration<float>(
    std::chrono::steady_clock::now().time_since_epoch()).count();

const auto cmd = follow_controller.update(obs);
send_chassis_can_mode(cmd.motor1_rpm, cmd.motor2_rpm, cmd.pitch, cmd.yaw, TRIGGER_STOP, CAN_CONTROL_ENABLE);
send_gimbal_can_mode(cmd.pitch, cmd.yaw, TRIGGER_STOP);
```

目标丢失时：

```cpp
aim_follow::TargetObservation obs;
obs.valid = false;
obs.timestamp_s = std::chrono::duration<float>(
    std::chrono::steady_clock::now().time_since_epoch()).count();

const auto cmd = follow_controller.update(obs);
send_chassis_can_mode(cmd.motor1_rpm, cmd.motor2_rpm, cmd.pitch, cmd.yaw, TRIGGER_STOP, CAN_CONTROL_ENABLE);
send_gimbal_can_mode(cmd.pitch, cmd.yaw, TRIGGER_STOP);
```

## 上板调参顺序

1. 先让云台自动瞄准工作，暂时把 `follow_kp_rpm_per_m` 设为 0，确认 `pitch/yaw` 方向正确。
2. 如果目标向右但云台向左，修改主程序顶部的 `AIM_FOLLOW_INVERT_YAW`。
3. 如果目标向上但云台向下，修改主程序顶部的 `AIM_FOLLOW_INVERT_PITCH`。
4. 再打开固定距离跟随，期望距离先设为 `1.0f`。
5. 如果目标远于 1m 时小车后退，交换 `motor1_forward_sign/motor2_forward_sign` 或把两个符号都改为 `-1`。
6. 如果小车不是前后移动而是原地旋转，说明左右电机安装方向相反，需要将其中一个 `motor*_forward_sign` 改为 `-1`。
7. 逐步调大 `follow_kp_rpm_per_m` 和 `max_follow_rpm`，不要一开始给很大速度。

## 独立编译测试

在 PC 或 30TAI 板端都可以单独编译该模块：

```bash
cd aim_follow_control
mkdir -p build
cd build
cmake ..
make -j2
./aim_follow_controller_test
```

该测试不依赖 OpenCV、NPU、HDMI 或 CAN，只验证控制算法输入输出是否符合预期。
