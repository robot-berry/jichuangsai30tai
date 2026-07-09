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
../../sdk/fpai_demo_package_26010502_deps.zip
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
