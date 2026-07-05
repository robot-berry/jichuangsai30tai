# modelzoo_utils 工具包说明

本目录保存从本机 30TAI/FPAI 依赖包中整理出的 `modelzoo_utils` 工具包，便于在另一台电脑上学习示例工程的 C++/Python 工具接口和 pipeline 封装方式。

本次来源路径：

```text
G:\UESTC\uav\fpai_demo_package_26010502\deps\modelzoo_utils
```

上传时已排除：

```text
__pycache__/
*.pyc
```

## 主要内容

```text
include/
  modelzoo_utils.hpp
  icraft_utils.hpp
  et_device.hpp
  NetInfo.hpp
  PicPre.hpp
  ai_example/
  pipeline/
pyrtutils/
  modelzoo_utils.py
  icraft_utils.py
  icraft_launcher.py
  analyze_time_utils.py
src/
  rtsp/live_rtsp_server.cpp
C++_API_reference.md
readme.md
setup.py
LICENSE
version.log
```

## 学习建议

1. 先看 `readme.md` 和 `C++_API_reference.md`，了解工具包提供的 C++ API。
2. 再看 `include/modelzoo_utils.hpp`、`include/icraft_utils.hpp`、`include/et_device.hpp`，理解模型加载、设备打开和运行封装。
3. 结合当前 PLin 工程的 `CMakeLists.txt`，理解示例工程如何引用 deps 中的头文件和库。
4. 如果学习图像输入/输出链路，可以重点看 `include/pipeline/` 下的 actor、io、memory、vpu 相关封装。
5. 如果学习 Python 辅助工具，可以看 `pyrtutils/` 下的启动、耗时分析和模型工具脚本。

## 与当前工程的关系

当前目标工程的主体代码仍在原始 `PLin+SingleNet+HDMI` 工程中构建运行，`modelzoo_utils` 是 30TAI/FPAI 示例常用的辅助工具包。上传该目录的目的主要是：

1. 方便另一台电脑学习原始示例工程依赖的工具层结构。
2. 方便查阅 `PicPre`、`NetInfo`、设备打开、pipeline actor 等封装。
3. 作为理解 30TAI 示例工程 C++ API 的参考资料。

注意：这个目录只提供工具包源码/头文件/说明，不等同于完整 30TAI SDK。真正编译运行仍需要板卡配套 deps、库文件、CMake 环境和板端运行环境。
