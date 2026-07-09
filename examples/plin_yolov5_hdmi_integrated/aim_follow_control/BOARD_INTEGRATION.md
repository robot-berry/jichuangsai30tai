# 30TAI 上板集成说明

本说明对应当前工程新增的 `aim_follow_control` 文件夹，以及已经接入 `src/sdicamera+yolov5+hdmi.cpp` 的控制逻辑。

完整验收证据清单见：

```text
aim_follow_control/ACCEPTANCE_CHECKLIST.md
```

## 已完成的工程接入

主程序现在已经包含：

```cpp
#include "aim_follow_controller.hpp"
```

主工程 `CMakeLists.txt` 已经加入：

```cmake
aim_follow_control/src/aim_follow_controller.cpp
${CMAKE_CURRENT_SOURCE_DIR}/aim_follow_control/include
```

因此在 30TAI/Linux 环境中按原有方式编译主程序时，会自动编译新增算法模块。

## 运行链路

检测到 bicycle 目标后：

1. 原 YOLO 后处理得到检测框。
2. 检测框通过 `map_box_to_display` 回映到 HDMI 显示坐标。
3. 多个 bicycle 同时出现时，`TargetSelector` 优先保持上一帧附近的目标；连续丢失后再回到最大框策略。
4. 使用显示坐标下的中心点 `cx/cy` 输入云台瞄准算法。
5. 使用显示坐标下的检测框宽度估计距离，并输入固定距离跟随算法。
6. 控制器输出：
   - `pitch/yaw`：发送给 0x38A 云台 CAN 帧；
   - `motor1_rpm/motor2_rpm`：发送给 0x201 底盘 CAN 帧。

目标丢失后：

1. 底盘速度输出为 0。
2. 云台 yaw 逐步回中。
3. CAN 仍按周期发送，避免底盘/云台控制心跳超时。

## 关键参数位置

在 `src/sdicamera+yolov5+hdmi.cpp` 顶部控制参数区：

```cpp
const bool AIM_FOLLOW_CONTROL_ENABLE = true;
const float AIM_FOLLOW_TARGET_DISTANCE_M = 1.0f;
const float AIM_FOLLOW_YAW_KP = 38.0f;
const float AIM_FOLLOW_PITCH_KP = 42.0f;
const bool AIM_FOLLOW_INVERT_YAW = false;
const bool AIM_FOLLOW_INVERT_PITCH = false;
const float AIM_FOLLOW_DISTANCE_DEADBAND_M = 0.12f;
const float AIM_FOLLOW_FOLLOW_KP_RPM_PER_M = 180.0f;
const int AIM_FOLLOW_MAX_FOLLOW_RPM = 160;
const int AIM_FOLLOW_MOTOR1_FORWARD_SIGN = 1;
const int AIM_FOLLOW_MOTOR2_FORWARD_SIGN = 1;
const int AIM_FOLLOW_LOST_HOLD_FRAMES = 5;
```

含义：

- `AIM_FOLLOW_CONTROL_ENABLE`：是否启用新增瞄准/跟随控制。
- `AIM_FOLLOW_TARGET_DISTANCE_M`：小车希望保持的目标距离，默认 1.0 m。
- `AIM_FOLLOW_YAW_KP` / `AIM_FOLLOW_PITCH_KP`：云台图像误差到位置命令的比例增益，方向正确后再微调。
- `AIM_FOLLOW_INVERT_YAW` / `AIM_FOLLOW_INVERT_PITCH`：云台方向反了就切换为 `true`。
- `AIM_FOLLOW_DISTANCE_DEADBAND_M`：固定距离跟随死区，实车抖动时先适当增大。
- `AIM_FOLLOW_FOLLOW_KP_RPM_PER_M`：距离误差到电机转速的比例，实车起步建议从小到大调。
- `AIM_FOLLOW_MAX_FOLLOW_RPM`：跟随速度上限，第一次落地测试建议保持较小。
- `AIM_FOLLOW_MOTOR1_FORWARD_SIGN` / `AIM_FOLLOW_MOTOR2_FORWARD_SIGN`：电机前进方向符号。
- `AIM_FOLLOW_LOST_HOLD_FRAMES`：目标短暂丢失时保持上一状态的帧数，过小会频繁回中，过大则丢失后反应较慢。

如果实测时目标距离大于 1 m，但小车后退，把两个电机方向符号都改为 `-1`。

如果实测时小车原地旋转而不是前后移动，说明左右电机安装方向相反，只改其中一个符号，例如：

```cpp
const int AIM_FOLLOW_MOTOR1_FORWARD_SIGN = 1;
const int AIM_FOLLOW_MOTOR2_FORWARD_SIGN = -1;
```

## 推荐上板测试顺序

1. 先把小车架空，避免第一次调参时直接冲出。
2. 启动程序后观察日志是否出现 `[AIM FOLLOW]`。
3. 固定目标在画面中心、距离 1 m 附近，确认 `motor1/motor2` 接近 0。
4. 把目标放到 1.5 m 左右，确认小车输出前进速度。
5. 把目标放到 0.5 m 左右，确认小车输出后退速度。
6. 目标左右移动，确认云台 yaw 是否朝目标方向修正。
7. 目标上下移动，确认云台 pitch 是否朝目标方向修正。
8. 若云台方向反了，在主程序顶部设置 `AIM_FOLLOW_INVERT_YAW/AIM_FOLLOW_INVERT_PITCH`。

## 30TAI 编译命令

在板端或 30TAI 对应交叉编译环境中执行：

```bash
cd /path/to/PLin+SingleNet+HDMI
./build_30tai.sh
```

生成 bundle 后，按脚本提示运行：

```bash
cd build/ZG/deploy/ZG
chmod a+x sdicamera+yolov5+hdmi
./sdicamera+yolov5+hdmi configs/ZG/sdicamera+yolov5+hdmi.yaml
```

## 本机已验证内容

当前环境缺少完整 30TAI 后端运行库，无法在 Windows 本机完整链接主程序。但新增的独立控制模块已经通过本地 CMake 编译和单元测试：

```bash
cd aim_follow_control
mkdir -p build
cd build
cmake ..
cmake --build . --config Release
./Release/aim_follow_controller_test.exe
```

测试覆盖：

- 目标居中且距离正确时，小车不动；
- 目标太远时，小车前进；
- 目标太近时，小车后退；
- 目标偏右时，yaw 增大；
- 目标偏上时，pitch 减小；
- 目标丢失后，底盘停止并逐步回中。

## 板端冒烟测试脚本

新增脚本位置：

```bash
aim_follow_control/test/run_30tai_smoke_test.sh
```

部署 runtime bundle 到 30TAI 后，可以在板端执行：

```bash
chmod +x aim_follow_control/test/run_30tai_smoke_test.sh
RUN_SECONDS=20 ./aim_follow_control/test/run_30tai_smoke_test.sh /home/fmsh/fpai_demo sdicamera+yolov5+hdmi configs/ZG/sdicamera+yolov5+hdmi.yaml
```

脚本会保存三类日志：

- `app.log`：主程序输出，重点查看 `[AIM FOLLOW]` 和 `[DISTANCE DEBUG]`。
- `candump.log`：如果板端安装了 `candump`，会记录 CAN 总线帧。
- `summary.txt`：自动提取关键控制和距离日志，方便快速判断是否检测到目标、是否输出云台角度和底盘转速。

当前距离跟随使用的是 HDMI 显示坐标系下的目标框宽度；同一个目标框的滤波距离会同时用于底盘跟随和距离显示，避免一帧内重复更新距离滤波器。

## Windows 端上传与板端编译脚本

新增脚本位置：

```powershell
tools/deploy_30tai.ps1
tools/check_30tai_connection.ps1
tools/find_30tai_board.ps1
tools/analyze_smoke_logs.ps1
```

如果板子连不上，先运行连接预检脚本：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_30tai_connection.ps1
```

该脚本会输出本机 IPv4、路由候选、ARP、ping、TCP 22 端口和 SSH 批处理测试结果，便于判断是网段/IP 问题，还是 SSH 服务或密码交互问题。

如果怀疑板端 IP 已经变化，可以扫描当前板端网段：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\find_30tai_board.ps1 -Subnet 192.168.125
```

如果扫描发现其他开放 22 端口的 IP，可以用 `-BoardIp` 指定新地址重新部署。

在 Windows PowerShell 中可以先做 dry run，检查部署命令是否正确：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_deploy_dry_run.ps1 -ProjectDir <PLinProjectDir>
```

板子网络和 SSH 正常后，执行上传和板端编译：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -Build
```

如果希望编译后直接跑 20 秒冒烟测试：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -Build -SmokeTest
```

如果希望冒烟测试后把板端日志拉回本机：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -Build -SmokeTest -FetchLogs
```

默认会把 `/tmp/aim_follow_smoke` 中的 `app.log`、`candump.log`、`summary.txt` 等文件保存到本机工程目录下的 `board_smoke_logs` 文件夹中。`summary.txt` 中应重点检查：

- 是否出现 `[AIM FOLLOW CONFIG]`，确认参数已经加载；
- 是否出现 `[AIM FOLLOW]`，确认目标进入瞄准/跟随控制；
- 是否出现 `[DISTANCE DEBUG]`，确认距离显示和距离估计有效；
- `candump.log` 中是否有 0x201、0x38A 等控制帧。

日志拉回本机后，可以自动分析最近一次冒烟测试结果：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\analyze_smoke_logs.ps1
```

也可以指定某一次日志目录：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\analyze_smoke_logs.ps1 -LogDir .\board_smoke_logs\smoke_YYYYMMDD_HHMMSS
```

默认参数：

- 板端 IP：`192.168.125.171`
- 用户：`root`
- 板端源码目录：`/home/fmsh/fpai_demo_src`

如果 IP 或目录变化，可以通过参数覆盖，例如：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_30tai.ps1 -ProjectDir <PLinProjectDir> -BoardIp 192.168.125.171 -User root -RemoteDir /home/fmsh/fpai_demo_src -Build
```
