# DetPost Reference Model

这个目录保存 PLin + YOLOv5 示例中的 DetPost 硬算子参考模型，方便在另一台电脑上学习 30TAI/ZG 模型配置、硬算子标记和运行配置。

## 文件结构

```text
configs/
  ZG/sdicamera+yolov5+hdmi.yaml
  BY/sdicamera+yolov5+hdmi.yaml
imodel/
  ZG/yolov5s_plin_352x640_ZG.json
  ZG/yolov5s_plin_352x640_ZG.raw
  BY/yolov5s_plin_BY.json
  BY/yolov5s_plin_BY.raw
names/
  coco.names
```

## ZG/30TAI 参考模型

当前 30TAI 重点看 ZG 模型：

```text
imodel/ZG/yolov5s_plin_352x640_ZG.json
imodel/ZG/yolov5s_plin_352x640_ZG.raw
configs/ZG/sdicamera+yolov5+hdmi.yaml
```

模型 JSON 中可以看到 DetPostZG 自定义硬算子：

```text
op_id: 447
_type_key: customop::DetPostZG
compile_target: @fpgat
```

对应 YAML 中的关键配置为：

```yaml
run_backend: zg330
fpga_nms: false
detpost: true
jsons:
  - ./imodel/ZG/yolov5s_plin_352x640_ZG.json
raws:
  - ./imodel/ZG/yolov5s_plin_352x640_ZG.raw
net_w: 640
net_h: 352
```

## BY/100TAI 对照模型

BY 模型用于对照学习普通 DetPost 写法：

```text
imodel/BY/yolov5s_plin_BY.json
imodel/BY/yolov5s_plin_BY.raw
configs/BY/sdicamera+yolov5+hdmi.yaml
```

模型 JSON 中可以看到：

```text
op_id: 296
_type_key: customop::DetPost
compile_target: @fpgat
```

对应 YAML 中的关键配置为：

```yaml
run_backend: buyi
fpga_nms: true
detpost: true
jsons:
  - ./imodel/BY/yolov5s_plin_BY.json
raws:
  - ./imodel/BY/yolov5s_plin_BY.raw
net_w: 640
net_h: 352
```

## 学习重点

1. `detpost: true` 表示 YOLO 后处理中的部分工作交给 FPGA DetPost 硬算子。
2. `customop::DetPostZG` 是 ZG/30TAI 平台使用的 DetPost 硬算子节点。
3. `compile_target: @fpgat` 表示该节点面向 FPGA 侧执行。
4. `.json` 描述网络图、算子、张量、量化和硬算子连接关系。
5. `.raw` 保存与 `.json` 配套的模型参数和编译数据，二者必须成对使用。
6. YAML 中的 `net_w`、`net_h`、`anchors`、`number_of_class`、`conf`、`iou_thresh` 必须与模型编译时配置一致。

## 注意事项

当前实测 30TAI 板卡如果 bitstream 或硬件环境不包含 DetPostZG，运行该模型可能出现：

```text
No DetPost HardWare
```

这不是模型文件缺失，而是板端硬件/位流不匹配。当前目标工程在板端验证时，曾使用板卡自带的 `detpost: false` 参考模型来绕开该问题。这里上传的 DetPost 模型主要用于学习和对照，不代表当前板卡一定能直接运行。

如果要在板端真正运行 DetPost 模型，需要同时满足：

1. 板端 bitstream 支持对应 DetPost/DetPostZG 硬算子。
2. YAML 中 `run_backend` 与板卡后端一致。
3. `.json` 和 `.raw` 路径正确。
4. 程序后处理逻辑与 `detpost: true` 输出格式匹配。
