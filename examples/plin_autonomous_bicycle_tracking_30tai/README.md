# PLin 自行车检测与小车自动追踪工程

本目录是可独立编译、部署和运行的 30 台板卡工程。当前链路包含：

- 真实摄像头输入与 HDMI/网络画面输出
- YOLOv5 自行车检测
- ByteTrack 多目标编号、遮挡保持和目标重关联
- 基于检测框宽度的单目距离估计与滤波
- 小车差速转向，使锁定目标保持在画面中心
- 前后距离控制，使锁定目标保持在约 1 米
- 单写入器安全 CAN 会话、反馈检查和失联归零
- systemd 开机自动运行与异常重启
- 电脑端单路实时网页预览

## 当前实车参数

| 参数 | 当前值 |
| --- | --- |
| 板卡 SDK / Icraft | `3.33.1` |
| 板卡地址 | `192.168.125.171` |
| 板端目录 | `/home/fmsh/plin_pHdmi/examples/codex/plin_main_current` |
| CAN 速率 | `250000` |
| CAN 自动模式 | `0xAA` |
| 目标类别 | `bicycle` |
| 目标距离 | `1.00 m` |
| 距离停止区 | `+-0.03 m` |
| 距离重新动作区 | 超过 `+-0.08 m` |
| 前后速度 | `50 rpm` |
| 转向速度 | `60 rpm` |
| 方向死区 | `0.08` |
| 目标丢失搜索 | 默认关闭 |
| 云台 | 默认关闭 |

## 目录说明

```text
aim_follow_control/             距离、转向和跟随控制器
bytetrack/                      ByteTrack 跟踪实现
configs/ZG/                     3.33.1 板端运行配置
imodel/ZG/                      YOLOv5 模型
names/                          COCO 类别名称
prebuilt/ZG/                    当前验证通过的板端二进制
src/                            板端主程序源码
systemd/                        开机自启服务
tools/                          部署、CAN、安全桥接和网络预览工具
build_cross_3331.sh             3.33.1 交叉编译入口
start_vision_dryrun.sh          无真实 CAN 写入的检测启动脚本
start_chassis_tracking_test.sh  有限时长实车测试脚本
stop_all.sh                     板端安全停止脚本
```

## 一键部署

在电脑 PowerShell 中进入本目录，然后运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_and_start.ps1 `
  -BoardIp 192.168.125.171
```

脚本会提示输入板卡 SSH 密码，并自动完成：

1. 等待网口和 SSH 可用。
2. 停止旧服务并将电机命令归零。
3. 删除板端旧目录并上传当前完整工程。
4. 安装并启用 `plin-autonomous-tracking.service`。
5. 启动摄像头、YOLO、ByteTrack、安全 CAN 和底盘追踪。
6. 启动电脑端网络取帧和 HTTP 服务。
7. 打开实时预览网页。

只进行无动作画面验证时使用：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\deploy_and_start.ps1 `
  -BoardIp 192.168.125.171 `
  -PreviewOnly
```

## 板卡开机自动运行

完成一次标准部署后，板卡每次上电会等待约 10 秒，然后自动启动完整追踪链路，不需要再单独开启 CAN。

通过 MobaXterm 连接：

```text
Remote host: 192.168.125.171
Port:        22
Username:    root
```

连接后常用命令：

```bash
systemctl status plin-autonomous-tracking.service --no-pager
systemctl restart plin-autonomous-tracking.service
systemctl stop plin-autonomous-tracking.service
systemctl start plin-autonomous-tracking.service
journalctl -u plin-autonomous-tracking.service -f
```

彻底取消开机启动：

```bash
cd /home/fmsh/plin_pHdmi/examples/codex/plin_main_current
./tools/uninstall_boot_service.sh
```

重新安装开机启动：

```bash
cd /home/fmsh/plin_pHdmi/examples/codex/plin_main_current
REMOTE_DIR=$PWD START_NOW=1 ./tools/install_boot_service.sh
```

## 实时网页

标准部署会在电脑上启动取帧和网页服务：

```text
http://127.0.0.1:8765/live_preview.html
```

网页只显示板端最新检测与追踪画面，清单文件采用无缓存刷新。页面状态栏中的帧编号和接收时间应持续变化。

板卡能够脱离电脑自主运行；网页依赖电脑端取帧进程，因此电脑重启后需要重新执行部署脚本或单独启动预览工具。

## 安全停止

电脑端停止：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\stop_all.ps1 `
  -BoardIp 192.168.125.171
```

板端停止：

```bash
systemctl stop plin-autonomous-tracking.service
```

停止流程会先发送零速命令，再结束安全 CAN 会话并关闭 `can0`。不要直接杀死单个 CAN 进程，也不要同时运行两套 CAN 写入程序。

## 3.33.1 交叉编译

电脑必须使用与板卡一致的 Icraft `3.33.1` 环境。编译入口：

```bash
./build_cross_3331.sh
```

成功后将生成的板端程序复制到：

```text
prebuilt/ZG/sdicamera+yolov5+hdmi
```

标准部署脚本只上传 `prebuilt/ZG` 中已经验证的二进制，不会在部署时临时改变板卡版本。

## 控制链路

```text
Camera
  -> YOLOv5 bicycle detections
  -> ByteTrack IDs
  -> locked target selection
  -> filtered monocular distance
  -> center and distance controller
  -> safe tracking bridge
  -> single CAN control session
  -> chassis motors
```

视觉主程序使用 `AIM_FOLLOW_CAN_DRYRUN=1`，只输出计算结果。真正的 CAN 写入全部由 `safe_can_control_session.py` 完成，避免视觉程序、安全脚本和遥控链路同时写入。

## 关键运行参数

| 环境变量 | 默认部署值 | 作用 |
| --- | --- | --- |
| `AIM_FOLLOW_BYTETRACK_ENABLE` | `1` | 开启 ByteTrack |
| `AIM_FOLLOW_DISTANCE_FOCAL_PX` | `600` | 模型坐标系焦距标定 |
| `AIM_FOLLOW_TARGET_DISTANCE_M` | `1.0` | 目标距离 |
| `AIM_FOLLOW_DISTANCE_DEADBAND_M` | `0.03` | 距离停止区 |
| `AIM_FOLLOW_DISTANCE_RESUME_DEADBAND_M` | `0.08` | 距离动作恢复阈值 |
| `AIM_FOLLOW_MIN_FOLLOW_RPM` | `50` | 前后最低速度 |
| `AIM_FOLLOW_MAX_FOLLOW_RPM` | `50` | 前后最高速度 |
| `AIM_FOLLOW_STEER_DEADZONE_NORM` | `0.08` | 画面中心方向死区 |
| `AIM_FOLLOW_MIN_STEER_RPM` | `60` | 转向最低速度 |
| `AIM_FOLLOW_MAX_STEER_RPM` | `60` | 转向最高速度 |
| `AIM_FOLLOW_SEARCH_ENABLE` | `0` | 目标丢失后不主动搜索 |
| `AIM_FOLLOW_GIMBAL_ENABLE` | `0` | 默认不控制云台 |

## 检查与测试

控制器测试：

```bash
./aim_follow_controller_test
```

安全桥接测试：

```bash
python3 -m unittest tools/test_safe_tracking_bridge.py
```

查看板端最后一次目标控制结果：

```bash
grep -a '\[AIM FOLLOW\]' logs/plin_live.log | tail -1
grep -a '\[DISTANCE DEBUG\]' logs/plin_live.log | tail -1
cat /tmp/plin_safe_can_status.json
```

正常停车状态应看到目标处于中心、距离位于停止区、电机命令为 `0/0`，并且 CAN 模式为 `0xAA`。
