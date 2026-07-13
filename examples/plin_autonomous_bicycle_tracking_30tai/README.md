# 30TAI 自行车自动追踪完整工程

这是已经在 30TAI/ZG330、Icraft 3.33.1 环境实车验证的独立工程。它从真实摄像头检测自行车，自动控制小车完成目标搜索、画面居中和 1 米定距跟随，同时将处理画面实时传到电脑浏览器。

> 当前版本没有集成 ByteTrack。它使用 YOLOv5 检测、连续目标选择和闭环状态机完成单目标追踪。ByteTrack 工程仍是另一个多人多目标 ID 跟踪示例，不要把两者混用。

## 已验证功能

- 真实摄像头输入和 HDMI 图像处理链路
- YOLOv5 `bicycle` 检测
- 目标可见时自动小幅转向居中
- 连续丢失后按最后目标方向搜索，并周期性反向扫描
- 目标重新出现后立即退出搜索并恢复小幅追踪
- 单目距离估计、稳定滤波和 `1.00 m ± 0.05 m` 定距
- 差速控制：前进 `(+,+)`、后退 `(-,-)`、左转 `(-,+)`、右转 `(+,-)`
- 单一 CAN 写入器、模式 `0xAA`、反馈超时归零和命令脉冲限幅
- 云台控制关闭，避免底盘与云台 CAN 命令互相干扰
- 电脑端实时网页预览

最终实车验收值：目标中心约 `x=943`（画面中心 `x=960`），滤波距离约 `1.03 m`，稳定后电机命令自动回到 `0/0`。

## 工作流程

```mermaid
flowchart LR
    A[真实摄像头] --> B[YOLOv5 自行车检测]
    B --> C[连续目标选择]
    C --> D[居中 / 搜索 / 1米定距状态机]
    D --> E[安全桥接器]
    E --> F[唯一 CAN 写入器]
    F --> G[小车差速底盘]
    B --> H[HDMI 处理帧]
    H --> I[电脑实时预览]
```

主程序始终使用 `AIM_FOLLOW_CAN_DRYRUN=1`，只计算控制量，不直接写 CAN。真正的 CAN 输出只经过 `safe_can_control_session.py`，因此不会出现两路 CAN 写入器互相抢占。

## 目录

```text
src/                         PLin + YOLOv5 + HDMI 主程序
aim_follow_control/          距离滤波、目标选择和控制状态机
configs/ZG/                  30TAI/ZG330 配置
imodel/ZG/                   已验证的 ZG 模型
prebuilt/ZG/                 板上实际运行过的 3.33.1 二进制
tools/deploy_and_start.ps1    一键部署、启动和打开预览
tools/stop_all.ps1            一键归零并停止全部进程
tools/safe_*.py               CAN 唯一写入与安全桥接
start_vision_dryrun.sh        板端检测进程启动脚本
build_30tai.sh                3.33.1 低内存编译脚本
```

## 最快使用方式

### 1. 准备

- 板子运行 Icraft/SDK `3.33.1`
- 板子默认地址为 `192.168.125.171`
- 摄像头和 CAN 已连接
- 测试区域没有人员和障碍物
- 遥控器关闭，避免遥控模式和 CAN 模式竞争
- Windows 已安装 `ssh`、`scp`、`tar` 和 Python 3；Python 需要 Pillow：`pip install pillow`

### 2. 一键部署并启动

在本目录打开 PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_and_start.ps1 `
  -BoardIp 192.168.125.171 `
  -BoardPassword "<板端密码>"
```

脚本会执行：

1. 停止旧的独立追踪进程并让电机归零。
2. 将预编译程序、模型、配置和工具部署到：
   `/home/fmsh/plin_pHdmi/examples/codex/plin_autonomous_bicycle_tracking`
3. 启动检测、CAN 安全会话和自动追踪。
4. 启动电脑端取帧与网页服务。
5. 打开 `http://127.0.0.1:8765/live_preview.html`。

## 停止

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\stop_all.ps1 `
  -BoardIp 192.168.125.171 `
  -BoardPassword "<板端密码>"
```

停止顺序为：追踪桥接归零、检测停止、CAN 发送禁用帧、`can0` 关闭、电脑预览关闭。

## 重新编译

预编译版本来自实际验收板，SHA-256 记录在 `SHA256SUMS.txt`。需要修改 C++ 时，在已经配置好 Icraft 3.33.1、`aarch64-linux-gnu-g++`、`/usr/cmake` 和 FPAI 依赖的 Linux 环境运行：

```bash
chmod +x build_30tai.sh
./build_30tai.sh
```

输出文件：

```text
build/ZG/sdicamera+yolov5+hdmi
```

板端内存约 1 GB 时，脚本会建立 2 GB 交换文件并使用 `-j1` 编译。编译完成后，将新文件替换到 `prebuilt/ZG/sdicamera+yolov5+hdmi`，再运行一键部署脚本。

## 当前控制参数

| 参数 | 值 |
|---|---:|
| 目标距离 | `1.00 m` |
| 距离停止范围 | `0.95–1.05 m` |
| 可见目标转速上限 | `35 rpm` |
| 丢失搜索转速 | `40 rpm` |
| 搜索确认延迟 | `0.35 s` |
| 搜索换向周期 | `60` 检测帧 |
| 距离焦距标定 | `544 px`（640 宽模型坐标） |
| 目标实际宽度 | `0.24 m` |
| CAN 波特率 | `250000` |

## 安全约束

- 第一次在新板上运行时应架空车轮或保证四周空旷。
- 不要同时运行其他 CAN 控制程序。
- 检测日志超过 `0.35 s` 没有更新，桥接器自动归零。
- CAN 反馈超过 `0.30 s` 没有更新，唯一写入器自动归零。
- 搜索与追踪命令分别硬限制为 `40 rpm` 和 `35 rpm`。
- `stop_all.ps1` 是正常停止入口；紧急情况应同时断开底盘动力。

## 测试

桥接器逻辑测试：

```bash
python3 -m unittest tools/test_safe_tracking_bridge.py
```

控制器测试：

```bash
cmake -S aim_follow_control -B aim_follow_control/build
cmake --build aim_follow_control/build
./aim_follow_control/build/aim_follow_controller_test
```

## ByteTrack 的关系

ByteTrack 适合多目标场景，为每个行人或物体维持跨帧 ID。当前小车任务只需要选择一个自行车并闭环控制，因此采用更轻量的连续目标选择器。后续若需要同时区分多个自行车，可在“YOLOv5 检测”和“目标选择”之间接入 ByteTrack，控制层与 CAN 安全层无需重写。
