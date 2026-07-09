# 另一台电脑完整重新编译说明

本文档说明如何只依赖当前 GitHub 仓库，在另一台电脑上补齐源码、模型和 SDK/deps 依赖，用于重新编译当前增强版 `PLin+SingleNet+HDMI` 工程。

## 1. 当前仓库已经补齐的内容

当前仓库已经包含：

```text
examples/plin_yolov5_hdmi_integrated/
```

这是当前增强版 PLin 工程的轻量可编译示例，包含：

```text
CMakeLists.txt
build_30tai.sh
src/sdicamera+yolov5+hdmi.cpp
configs/
imodel/
names/
aim_follow_control/
tools/
```

当前仓库还包含完整 SDK/deps 压缩包分片：

```text
sdk/fpai_demo_package_26010502_deps_parts/
```

该压缩包来自本机：

```text
G:\UESTC\uav\fpai_demo_package_26010502\deps
```

压缩包内容包含：

```text
deps/modelzoo_utils/
deps/thirdparty/include/
deps/thirdparty/a/
deps/thirdparty/lib/
deps/thirdparty/dll/
deps/thirdparty/so/
```

其中 `modelzoo_utils` 里包含之前缺失的：

```text
ai_example/postprocesses.hpp
ai_example/yolov5_npu_actor.hpp
pipeline/actor/...
```

## 2. 克隆仓库

完整 SDK/deps 原始压缩包约 292MB。由于 GitHub 普通文件有 100MB 限制，仓库中保存为 4 个分片文件，每个分片小于 100MB。另一台电脑不需要 Git LFS，正常 clone 即可获得分片。

首次 clone：

```powershell
git clone https://github.com/robot-berry/jichuangsai30tai.git jichuangsai30tai_aim_follow
cd .\jichuangsai30tai_aim_follow
```

如果已经 clone 过：

```powershell
cd .\jichuangsai30tai_aim_follow
git pull origin main
```

安装脚本会自动把分片合并为本地 zip 文件，再解压到 CMake 期望位置。合并出来的 zip 被 `.gitignore` 忽略，不会再次提交。

## 3. 解压 SDK/deps 到 CMake 期望位置

从仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\install_full_sdk_deps.ps1
```

默认情况下，脚本会以：

```text
examples/plin_yolov5_hdmi_integrated
```

作为 PLin 工程目录，并把 SDK/deps 解压到该工程 `CMakeLists.txt` 默认查找的位置：

```text
../../../fpai_demo_package_26010502/deps
```

如果你的 PLin 工程目录不是默认示例目录，可以指定：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\install_full_sdk_deps.ps1 `
  -PlinProjectDir "G:\UESTC\uav\01\PLin+SingleNet+HDMI\PLin+SingleNet+HDMI"
```

如果目标位置已有旧 deps，想覆盖解压：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\install_full_sdk_deps.ps1 -Force
```

## 4. 编译当前增强版示例工程

进入示例工程：

```powershell
cd .\examples\plin_yolov5_hdmi_integrated
```

板端/Linux 方向的 ZG 构建命令参考：

```bash
cmake -S . -B build/ZG -DTARGET_CHIP=ZG -DCMAKE_PREFIX_PATH=/usr/cmake
cmake --build build/ZG -j1
```

如果是在 30TAI 板端低内存环境，建议使用工程内脚本或单线程构建：

```bash
./build_30tai.sh
```

Windows 方向的本地构建仍需要本机 Icraft/CLI、CMake 包和对应编译器环境可用。deps 中已经包含 `thirdparty/lib`、`thirdparty/dll` 和头文件，但 Icraft 后端包是否可被 `find_package()` 找到，仍取决于本机 Icraft 安装和 `CMAKE_PREFIX_PATH`。

## 5. 当前编译验证结论

本仓库中的瞄准与追踪控制模块可以在普通电脑上独立编译和单元测试，用来验证目标选择、单目距离滤波、云台瞄准输出、小车定距跟随输出等算法逻辑：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_local_aim_follow_checks.ps1
```

完整 `PLin+SingleNet+HDMI` 示例工程已经补齐源码、模型和 SDK/deps 文件，并修正了 ZG 后端包查找与 MSVC C++17 宏识别问题。需要注意的是，完整工程引用的 FPAI/modelzoo SDK 头文件包含 `unistd.h`、`usleep` 等 Linux/板端接口，因此该完整程序应优先在 30TAI 板端或 Linux 交叉编译环境中构建。Windows 本机更适合做算法模块编译、文档检查和文件完整性验证。

板端连接正常后，可在 30TAI 上进入示例目录运行：

```bash
./build_30tai.sh
```

如果只想确认当前集成源码中是否包含距离显示、瞄准追踪、小车跟随和 CAN dry-run/synthetic target 相关逻辑，可以在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_plin_integration.ps1 -ProjectDir .\examples\plin_yolov5_hdmi_integrated
```

## 6. 仍需注意的外部条件

当前 GitHub 仓库已经补齐源码、模型和 deps 文件，但以下内容仍属于机器/板卡环境：

1. CMake 和 C++ 编译器。
2. 板端 `/usr/cmake` 或本机 Icraft/CLI 后端包。
3. 30TAI 板卡系统、bitstream 和运行环境。
4. SSH、网口、CAN、SDI 摄像头等硬件连接。

也就是说：

```text
文件层面：已补齐到 GitHub。
环境层面：另一台电脑仍需要安装 CMake、编译器和 Icraft/30TAI 运行环境。
```
