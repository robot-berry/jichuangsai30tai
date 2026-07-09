# 当前增强版 PLin 工程示例

本目录是当前 `PLin+SingleNet+HDMI` 工程的轻量可编译示例，已包含距离显示、距离滤波、目标连续选择、云台追踪、小车定距跟随、HDMI 控制数据显示、CAN dry-run 和 synthetic target 验证相关代码。

本目录不包含 `build/`、板端运行日志和本地临时文件。

## 目录内容

```text
CMakeLists.txt
build_30tai.sh
src/
configs/
imodel/
names/
aim_follow_control/
tools/
```

## 依赖安装

该工程的 `CMakeLists.txt` 默认查找：

```text
../../../deps
../../../fpai_demo_package_26010502/deps
```

当前仓库已经把完整 deps 打包到：

```text
../../sdk/fpai_demo_package_26010502_deps_parts/
```

请回到仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\install_full_sdk_deps.ps1
```

然后再进入本目录构建。

## 构建参考

```bash
cmake -S . -B build/ZG -DTARGET_CHIP=ZG -DCMAKE_PREFIX_PATH=/usr/cmake
cmake --build build/ZG -j1
```

如果在 30TAI 板端构建，可以参考：

```bash
./build_30tai.sh
```

## 瞄准与追踪验证

本示例已经集成：

1. `TargetSelector`：多目标时优先保持上一帧附近的自行车目标，减少目标乱切。
2. `MonocularDistanceEstimator`：根据检测框宽度估算距离，并对距离结果做低通滤波。
3. `AimFollowController`：根据目标中心误差输出云台 `pitch/yaw` 位置命令，根据距离误差输出小车左右电机转速命令。
4. HDMI 控制数据显示：画面上显示目标状态、距离、云台命令和小车命令，便于不接小车时先验证算法输出。
5. CAN dry-run 与 synthetic target：不连接小车或摄像头时，可先用日志验证控制链路。

在普通电脑上可以先验证独立控制模块：

```powershell
powershell -ExecutionPolicy Bypass -File ..\..\tools\run_local_aim_follow_checks.ps1
```

也可以从仓库根目录检查 PLin 主程序是否包含完整集成点：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_plin_integration.ps1 -ProjectDir .\examples\plin_yolov5_hdmi_integrated
```

完整 PLin 程序依赖 30TAI/FPAI SDK，其中部分头文件使用 `unistd.h`、`usleep` 等 Linux/板端接口。Windows 本机主要用于算法模块编译和集成静态检查；最终完整程序建议在 30TAI 板端或 Linux 交叉编译环境中构建。
