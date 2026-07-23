# UAV 项目资料补充说明

本文档对应本机资料目录：

```text
G:\UESTC\uav
```

该目录不是单一源码工程，而是 30TAI / Icraft / YOLOv5 / ByteTrack / PLin 示例工程相关资料包根目录，包含手册、驱动、SDK、安装包、镜像、示例工程和压缩包。为了避免 GitHub 普通仓库被超大二进制文件撑爆，本次没有把整个 `G:\UESTC\uav` 原样塞进 Git 历史，而是按项目可复现和学习价值做了整理。

## 1. 已上传到 GitHub 的资料

### 1.1 参考手册与驱动

已上传到：

```text
supplementary_materials/uav_reference_docs/
```

包含：

```text
30TAI智能应用开发指导手册.pdf
AI_Mate_DetPost硬算子技术手册_v1.0.pdf
FMSH下载站使用说明V1.3.pdf
Icraft_modelzoo访问地址.pdf
v3.7_tutorial_v3.pdf
悟空开发板入门教程_20250412.pdf
CP210x_Universal_Windows_Driver.zip
```

这些文件主要用于另一台电脑搭建开发环境、理解 30TAI 板卡、DetPost 硬算子、Icraft modelzoo 访问方式和串口驱动安装。

### 1.2 当前目标工程

当前增强版目标工程已经上传到：

```text
examples/plin_yolov5_hdmi_integrated/
```

该工程包含：

```text
YOLOv5 检测
HDMI 显示
距离显示
距离滤波
目标连续选择
云台瞄准
小车定距跟随
CAN dry-run
synthetic target 验证
```

### 1.3 ByteTrack 参考工程

ByteTrack 工程已经从：

```text
G:\UESTC\uav\bytetrack.zip
```

整理上传到：

```text
examples/bytetrack_icraft/
```

上传时去掉了 zip 内部自带的 `.git/` 历史和 LFS 缓存，保留源码、Icraft 编译配置、C++ 部署代码、示例图片、演示视频、`bytetrack_s_608x1088_traced.pt` 和 `bytetrack_s_mot17.pth.tar`。

该工程用于学习多目标跟踪算法，后续可把其中的 `BYTETracker` C++ 逻辑移植到当前 YOLOv5 HDMI 工程中，用于改善多目标乱切、短暂遮挡和检测框抖动问题。

### 1.4 SDK / deps 依赖

完整 30TAI/FPAI `deps` 依赖已经以分片形式上传到：

```text
sdk/fpai_demo_package_26010502_deps_parts/
```

另一台电脑 clone 仓库后，可运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\install_full_sdk_deps.ps1
```

脚本会自动合并分片并解压到 CMake 期望的位置。详细说明见：

```text
docs/BUILD_FULL_PROJECT_ON_ANOTHER_PC_CN.md
```

### 1.5 modelzoo_utils 学习资料

`modelzoo_utils` 工具包已经上传到：

```text
third_party/modelzoo_utils/
```

它用于学习 Icraft 示例工程中常见的模型加载、图片预处理、运行时工具和 pipeline actor 结构。

## 2. 未原样上传的大文件

`G:\UESTC\uav` 中存在多个超大安装包、镜像和压缩包，不适合直接进入 GitHub 普通仓库。

典型文件包括：

```text
Icraft_Setup_v3.33.1.exe                 约 1837.56 MB
wk_icraft3.31_unbuntu_sd_Image.rar       约 2879.03 MB
Icraft_3.33.1_amd64.deb                  约 926.56 MB
yolov5_7.0.rar                           约 407.03 MB
bytetrack.zip                            约 360.58 MB
fpai_demo_package_26010502.zip           约 331.63 MB
yolov5_demo_for_icraft_zg.zip            约 110.54 MB
```

原因：

1. GitHub 普通仓库单文件硬限制约 100MB。
2. 大型安装包和镜像会显著拖慢 clone、pull 和 push。
3. 这些文件更适合放在网盘、Release 附件、Git LFS 或校内文件服务器中。
4. 当前仓库已经对真正需要参与编译的 `deps` 做了分片上传，避免另一台电脑缺少核心头文件和库。

## 3. 建议的资料管理方式

建议后续按以下方式管理：

```text
源码、配置、脚本、小型文档：放入 GitHub 仓库
必要 SDK/deps：分片上传或使用 Git LFS
GB 级安装包、系统镜像、完整压缩包：放到 Release、网盘或实验室共享盘
本地 build、日志、临时抓图：不提交，只保留关键结果和说明
```

如果后续确实需要把某个大文件纳入 GitHub，应优先使用 GitHub Release 或 Git LFS，而不是直接提交到 `main` 分支。
