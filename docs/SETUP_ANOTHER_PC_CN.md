# 另一台电脑搭建 30TAI 目标工程环境说明

本文档用于在新电脑上复现当前 30TAI 目标跟随与距离显示工程。GitHub 仓库只保存针对当前目标任务新增和整理的算法模块、集成脚本、验证脚本、技术说明与板端测试流程，不直接复制完整的原始 PLin 示例工程和 30TAI SDK。

仓库地址：

```text
https://github.com/robot-berry/jichuangsai30tai.git
```

## 1. GitHub 上已经包含的内容

当前仓库已经上传以下可学习、可迁移的内容：

| 类别 | 路径 | 作用 |
| --- | --- | --- |
| 距离跟随与自动瞄准算法模块 | `aim_follow_control/` | 单目距离估计、距离低通滤波、目标连续选择、云台瞄准控制、小车定距跟随控制 |
| 本地单元测试 | `aim_follow_control/test/` | 验证距离估计、目标丢失保护、远近跟随、上下左右瞄准响应 |
| PLin 工程集成说明 | `integration/` | 说明如何把算法模块接入 YOLO 后处理、HDMI 显示和 CAN 输出链路 |
| DetPost 算子学习参考模型 | `examples/detpost_reference_model/` | 保存 ZG/30TAI 的 `customop::DetPostZG` 模型、BY 对照模型和对应 YAML |
| modelzoo_utils 工具包 | `third_party/modelzoo_utils/` | 保存 30TAI/FPAI 示例常用的 C++/Python 工具封装，方便学习 API 和 pipeline |
| 板端部署与测试脚本 | `tools/` | 检查 SSH、同步工程、验证集成、运行 HDMI dry-run、诊断 SDI 输入和 CAN 总线 |
| 算法与验收文档 | `docs/` | 算法设计、板端验收步骤、调参记录模板、迁移说明 |
| 当前状态记录 | `STATUS.md` | 记录已经完成的内容、30TAI 实测结果、当前剩余问题 |
| 项目入口说明 | `README.md` | 给出项目结构、常用命令和验证流程 |

其中最重要的板端验证脚本包括：

```text
tools/run_board_vision_algorithm_test.ps1
tools/run_hdmi_synthetic_demo.ps1
tools/run_sdi_input_triage.ps1
tools/run_board_synthetic_control_test.ps1
tools/diagnose_30tai_can_bus.ps1
tools/verify_plin_integration.ps1
tools/sync_to_plin_project.ps1
```

DetPost 算子学习可以先看：

```text
examples/detpost_reference_model/README.md
docs/DETPOST_OPERATOR_LEARNING_NOTES_CN.md
```

modelzoo_utils 工具包可以先看：

```text
third_party/modelzoo_utils/README_UPLOAD_CN.md
third_party/modelzoo_utils/C++_API_reference.md
third_party/modelzoo_utils/include/
```

## 2. GitHub 上没有也不建议上传的内容

为了避免仓库过大、泄露本地信息或复制厂商环境，以下内容没有放入 GitHub：

| 内容 | 原因 | 新电脑需要如何处理 |
| --- | --- | --- |
| 完整原始 `PLin+SingleNet+HDMI` 工程 | 当前仓库是针对目标任务的新项目，不是直接照搬原始工程 | 从你的压缩包、课程资料或官方示例重新准备 |
| 30TAI SDK、FPAI 运行环境、交叉编译工具链 | 属于板卡/厂商环境，体积大且依赖机器配置 | 按 30TAI 官方资料安装 |
| 板卡系统镜像、BOOT 文件、bitstream | 与具体板卡版本相关 | 使用当前板卡配套镜像或官方资料 |
| 大模型/板端原始模型文件 | 通常来自原始 PLin 工程或板卡示例 | 保留在原始 PLin 工程中 |
| SSH 私钥 `.ssh_board/` | 本地登录凭据，不能上传 | 新电脑重新生成或继续用密码登录 |
| 板端日志、临时包、编译产物 | 可重复生成，体积大 | 新电脑运行脚本后自动生成 |

`.gitignore` 已经忽略 `.ssh_board/`、板端日志目录、压缩包和编译产物，因此不会把这些本地敏感或临时文件传到 GitHub。

## 3. 新电脑准备条件

建议新电脑先准备：

1. Windows PowerShell。
2. Git。
3. 能访问 30TAI 板子的有线网卡，建议与板子同网段，例如电脑为 `192.168.125.1`，板子为 `192.168.125.171`。
4. 原始 `PLin+SingleNet+HDMI` 工程。
5. 30TAI 板端已经具备原始 FPAI/PLin 示例所需的编译和运行环境。
6. 可用 SSH 登录信息。当前板子常用信息为：

```text
IP: 192.168.125.171
user: root
password: fmsh
```

如果不想配置 SSH 密钥，可以先使用密码登录。需要自动化脚本免密时，再在新电脑上重新生成 SSH key，并把公钥追加到板子的 `/root/.ssh/authorized_keys`。

## 4. 克隆当前目标工程

在新电脑选择一个工作目录，例如：

```powershell
cd G:\UESTC\uav\01
git clone https://github.com/robot-berry/jichuangsai30tai.git jichuangsai30tai_aim_follow
cd .\jichuangsai30tai_aim_follow
```

确认远端状态：

```powershell
git status
git log --oneline -5
```

正常情况下应能看到 `main` 分支，并且最新提交与 GitHub 远端一致。

## 5. 准备原始 PLin 工程

把原始 `PLin+SingleNet+HDMI` 工程放到新电脑固定目录，例如：

```text
G:\UESTC\uav\01\PLin+SingleNet+HDMI\PLin+SingleNet+HDMI
```

该目录下应至少包含：

```text
CMakeLists.txt
src/
configs/
imodel/
```

其中 `src/sdicamera+yolov5+hdmi.cpp` 是当前目标工程需要集成的主程序文件。

## 6. 同步算法模块和工具到 PLin 工程

先用 dry-run 检查将要复制的内容：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\sync_to_plin_project.ps1 -ProjectDir "G:\UESTC\uav\01\PLin+SingleNet+HDMI\PLin+SingleNet+HDMI" -DryRun
```

确认无误后正式同步：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\sync_to_plin_project.ps1 -ProjectDir "G:\UESTC\uav\01\PLin+SingleNet+HDMI\PLin+SingleNet+HDMI"
```

同步后，PLin 工程中应出现：

```text
aim_follow_control/
tools/
integration/
```

同时主工程 `CMakeLists.txt` 应包含：

```text
aim_follow_control/src/aim_follow_controller.cpp
aim_follow_control/include
```

## 7. 检查 PLin 集成是否完整

运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_plin_integration.ps1 -ProjectDir "G:\UESTC\uav\01\PLin+SingleNet+HDMI\PLin+SingleNet+HDMI"
```

该脚本会检查：

1. `aim_follow_control` 模块是否存在。
2. CMake 是否加入算法源文件和头文件路径。
3. 主程序是否包含距离算法、目标选择、HDMI 控制数据显示、CAN dry-run。
4. 板端测试脚本是否已经同步。
5. 关键字符串如 `[AIM FOLLOW]`、`[DISTANCE DEBUG]`、`AIM_FOLLOW_CAN_DRYRUN` 是否存在。

## 8. 先做不连接小车和云台的 HDMI 验证

如果新电脑已经能通过 SSH 连接板子，建议先跑 HDMI synthetic demo。这个测试不要求真实 SDI 摄像头可用，也不会向 CAN 总线写控制帧：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_hdmi_synthetic_demo.ps1 `
  -ProjectDir "G:\UESTC\uav\01\PLin+SingleNet+HDMI\PLin+SingleNet+HDMI" `
  -RunSeconds 12
```

如果已经配置了 SSH 私钥，可以增加：

```powershell
-SshKey .\.ssh_board\id_ed25519_30tai
```

该测试会启用：

```text
AIM_FOLLOW_CAN_DRYRUN=1
AIM_FOLLOW_SYNTHETIC_TARGET=1
camera.vtc=true
```

预期现象：

1. HDMI 上能看到目标锁定、距离、云台追踪、小车追踪和 CAN 输出状态。
2. 日志中出现 `[AIM FOLLOW]`。
3. 日志中出现 `[DISTANCE DEBUG]`。
4. 日志中出现 `DRYRUN id=0x...`。
5. 不应该出现 `ImageMake Timeout` 或 `accept 0 data`。

当前已经在 30TAI 上通过的最近一次结果为：

```text
[AIM FOLLOW]: 85
[DISTANCE DEBUG]: 85
CAN dry-run frames: 168
ImageMake Timeout: 0
accept 0 data: 0
```

## 9. 真实 SDI 摄像头路径验证

当前项目中算法和 HDMI dry-run 已经能在 VTC/synthetic 模式下跑通，但真实 `SDI_IN_0` 输入仍曾出现：

```text
ZG330 ImageMake Timeout after 1000 ms, data_in_num=0
Image size is 640 * 352, but accept 0 data
```

在新电脑上如果要继续查真实摄像头路径，运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_sdi_input_triage.ps1 `
  -ProjectDir "G:\UESTC\uav\01\PLin+SingleNet+HDMI\PLin+SingleNet+HDMI"
```

判断标准：

1. VTC/internal frame path 通过，说明应用、模型、HDMI 和算法链路能启动。
2. Real SDI path 失败，说明问题集中在外部 SDI 信号、线缆、输入口、bitstream 或板端采集路径。
3. 只有真实 SDI 有有效帧后，才能用真实摄像头目标验证 YOLO 检测、距离估计和跟随控制。

## 10. 连接小车和云台前的 CAN 验证

未连接小车和云台时，建议只使用 dry-run 或 CAN 健康检查：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\diagnose_30tai_can_bus.ps1
```

不要在 `ERROR-PASSIVE` 或 `BUS-OFF` 状态下做运动测试。第一次连接实车时，应架空轮子，并使用小速度参数。

## 11. 新电脑最小复现流程

如果只想确认 GitHub 内容可用，按以下顺序执行：

```powershell
git clone https://github.com/robot-berry/jichuangsai30tai.git jichuangsai30tai_aim_follow
cd .\jichuangsai30tai_aim_follow

powershell -ExecutionPolicy Bypass -File .\tools\run_local_aim_follow_checks.ps1

powershell -ExecutionPolicy Bypass -File .\tools\sync_to_plin_project.ps1 -ProjectDir "<PLinProjectDir>"

powershell -ExecutionPolicy Bypass -File .\tools\verify_plin_integration.ps1 -ProjectDir "<PLinProjectDir>"

powershell -ExecutionPolicy Bypass -File .\tools\run_hdmi_synthetic_demo.ps1 -ProjectDir "<PLinProjectDir>"
```

完成以上步骤后，说明：

1. GitHub 上的算法模块可以独立测试。
2. 算法模块可以同步进原始 PLin 工程。
3. PLin 工程能被检查出包含距离、瞄准、跟随、HDMI 显示和 CAN dry-run 相关改动。
4. 30TAI 板端可以在不连接小车和云台的情况下显示控制数据。

真实摄像头、真实 CAN、小车移动和云台运动属于后续硬件闭环实测，需要在 SDI 输入和 CAN 总线都确认正常后再进行。
