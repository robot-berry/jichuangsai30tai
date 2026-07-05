# DetPost 算子学习说明

本文档用于说明当前仓库中新增的 DetPost 参考模型如何学习，以及它和当前 YOLOv5 + 距离显示 + 云台/小车跟随工程之间的关系。

## 1. DetPost 是什么

在 YOLO 类目标检测模型中，神经网络主体通常输出多尺度特征图。后处理阶段需要完成解码、置信度筛选、类别筛选、NMS 或类似筛选逻辑，最终得到检测框、类别和置信度。

DetPost 算子的作用，就是把部分检测后处理逻辑下沉到 FPGA/硬件侧执行。这样做的好处是减少 CPU 后处理压力，并且让模型输出更接近最终检测结果。

在当前参考模型里，DetPost 不是普通 C++ 函数，而是编译进模型图的自定义硬算子。

## 2. 当前仓库里的参考模型

参考模型已放在：

```text
examples/detpost_reference_model/
```

重点文件如下：

```text
examples/detpost_reference_model/configs/ZG/sdicamera+yolov5+hdmi.yaml
examples/detpost_reference_model/imodel/ZG/yolov5s_plin_352x640_ZG.json
examples/detpost_reference_model/imodel/ZG/yolov5s_plin_352x640_ZG.raw
```

ZG/30TAI 模型中的 DetPost 节点信息为：

```text
op_id: 447
_type_key: customop::DetPostZG
compile_target: @fpgat
```

BY/100TAI 对照模型中的 DetPost 节点信息为：

```text
op_id: 296
_type_key: customop::DetPost
compile_target: @fpgat
```

## 3. 怎么在 JSON 中学习 DetPost

打开模型 JSON 后，可以重点搜索：

```text
customop::DetPostZG
customop::DetPost
compile_target
outputs
inputs
shape
dtype
```

建议重点观察：

1. DetPost 节点的输入来自哪些 YOLO 输出层。
2. DetPost 节点输出的 tensor 形状和类型。
3. DetPost 是否编译到 `@fpgat`。
4. 模型中 DetPost 前面的卷积、Concat、输出层如何连接。
5. YAML 中 `anchors`、`number_of_class`、`conf`、`iou_thresh` 是否与检测头配置一致。

## 4. YAML 配置中需要关注的字段

ZG/30TAI 配置：

```yaml
run_backend: zg330
fpga_nms: false
detpost: true
net_w: 640
net_h: 352
number_of_class: 80
conf: 0.2
iou_thresh: 0.5
```

BY/100TAI 配置：

```yaml
run_backend: buyi
fpga_nms: true
detpost: true
net_w: 640
net_h: 352
number_of_class: 80
conf: 0.2
iou_thresh: 0.5
```

这里最重要的是 `detpost: true`。如果改成 `detpost: false`，程序侧就需要按非 DetPost 模型的输出格式做后处理，两种输出格式不能随便混用。

## 5. 与当前目标工程的关系

当前目标工程的主链路是：

```text
SDI/HDMI 输入
  -> ImageMake
  -> YOLOv5 推理
  -> YOLO 后处理
  -> 自行车目标筛选
  -> 目标中心与框宽计算
  -> 单目距离估计
  -> 距离滤波
  -> 云台追踪和小车定距跟随
  -> HDMI 显示 / CAN 输出
```

DetPost 影响的是 “YOLOv5 推理 -> YOLO 后处理” 这一段。距离估计、目标选择、云台追踪和小车跟随是在检测框产生之后执行的，因此这些新增算法本身不依赖 DetPost 是否开启。

不过，DetPost 会影响检测框输出格式。如果换用 DetPost 模型，需要确认主程序的后处理代码与模型输出格式一致，否则可能出现检测框数量、坐标、类别或置信度解析错误。

## 6. 当前板端实测注意点

当前 30TAI 板端曾遇到默认 DetPost 模型启动失败：

```text
No DetPost HardWare
```

这说明当前板端 bitstream 或运行环境没有匹配 DetPostZG 硬件。为了先验证摄像头、HDMI、距离算法、云台追踪和小车跟随逻辑，当前工程使用过板端自带的 `detpost: false` 参考模型运行。

因此：

1. `examples/detpost_reference_model/` 适合用于学习 DetPost 模型结构和配置。
2. 是否能直接在当前 30TAI 上运行，要看板端 bitstream 是否支持 DetPostZG。
3. 后续如果更换 bitstream 或板端模型环境，可以再用该参考模型做 DetPost 路径验证。

## 7. 建议学习顺序

1. 先看 `configs/ZG/sdicamera+yolov5+hdmi.yaml`，理解模型路径和 `detpost: true`。
2. 再打开 `imodel/ZG/yolov5s_plin_352x640_ZG.json`，搜索 `customop::DetPostZG`。
3. 对照 BY 模型中的 `customop::DetPost`，理解 ZG 和 BY 后端命名差异。
4. 回到当前主工程 `src/sdicamera+yolov5+hdmi.cpp`，看 YOLO 后处理之后如何使用检测框。
5. 最后理解距离算法只依赖检测框宽度和中心点，不直接依赖 DetPost 内部实现。
