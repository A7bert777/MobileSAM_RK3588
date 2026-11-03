# MobileSAM_RK3588

基于瑞芯微 `rknn_model_zoo-2.5.0` 的 MobileSAM C++ 示例，按照 YOLO26 项目的目录结构和目录批处理流程实现。程序使用两个独立的 RKNN context，依次运行 Encoder 和 Decoder，并将预测 IoU 最高的 Mask 叠加到原图。

当前项目使用已有的 FP16/非量化 RKNN 模型，不包含 INT8 校准或量化流程；推理采用普通 RKNN API，不照搬 YOLO26 的 zero-copy、检测后处理或 benchmark。

**请在 RK3588/aarch64 开发板上编译运行，不要在 x86 主机上直接运行该可执行文件。**

## 1. 项目代码与推理流程

- [src/main.cc](src/main.cc)：读取提示文件，加载一次双模型，逐张处理目录中的 JPG/JPEG/PNG，并保存 Mask 叠加图。
- [src/mobilesam.cc](src/mobilesam.cc)：管理 Encoder、Decoder 的初始化、推理和资源释放。
- [src/rknn_mobilesam_utils.cc](src/rknn_mobilesam_utils.cc)：普通 `rknn_inputs_set`、`rknn_run`、`rknn_outputs_get` 推理接口。
- [src/preprocess.cc](src/preprocess.cc)：RGB 图像长边等比缩放到 448，在右侧和下侧补零，同时转换原图提示坐标。
- [src/postprocess.cc](src/postprocess.cc)：选择最高 IoU 的候选 Mask，放大、裁掉 padding、恢复原图尺寸并叠加显示。
- [include/](include/)：双模型、预处理后处理和 RKNN API 头文件。

完整流程为：

1. Encoder 将 `448×448` RGB 图像编码为 image embedding；
2. 将 Encoder 的 NCHW 输出实际重排为 Decoder 所需的 NHWC 数据；
3. Decoder 接收 embedding、提示坐标、提示标签以及全零的可选 Mask 输入；
4. 从四个候选 Mask 中选择预测 IoU 最高者，恢复原图尺寸后以 `>0` 二值化；
5. 将青色 Mask 和绿色提示框绘制到原图并保存 PNG。

当前模型实际张量形状：

- Encoder 输入：`[1,448,448,3]`，NHWC，FP16；输出：`[1,256,28,28]`，NCHW，FP16。
- Decoder embedding 输入：`[1,28,28,256]`，NHWC，FP16。
- Decoder 提示输入：`point_coords [1,2,2]`、`point_labels [1,2]`。
- Decoder 输出：`iou_predictions [1,4]`、`low_res_masks [1,4,112,112]`。

## 2. 目录说明

- [model/](model/)：用户准备的 Encoder、Decoder FP16 RKNN 模型及提示文件。
- [3rdparty/](3rdparty/)：完整复制自 `rknn_model_zoo-2.5.0` 的第三方依赖，包含本项目使用的 ARM64 OpenCV。
- [rknn_lib/](rknn_lib/)：与模型仓库 2.5.0 配套的 `librknnrt.so`。
- [inputimage/](inputimage/)：支持 `.jpg`、`.jpeg`、`.png`，扩展名不区分大小写，按文件名排序处理。
- [outputimage/](outputimage/)：通常输出为 `主文件名_out.png`；同批重名时使用完整源文件名和数字后缀避免覆盖。
- [build/](build/)：编译目录、可执行文件和运行日志。
- [python_script/jpg2png.py](python_script/jpg2png.py)：可选格式转换工具，正常运行不需要先把 JPG 转成 PNG。

## 3. 默认绝对路径

与 YOLO26 项目模板一致，[src/main.cc](src/main.cc) 中 `*****` 注释下方配置了六个绝对路径：

1. Encoder RKNN；
2. Decoder RKNN；
3. 提示坐标文件；
4. 提示标签文件；
5. 输入图片目录；
6. 输出图片目录。

如果移动项目，需要同步修改这六个路径。运行前确保输出目录存在：

```bash
mkdir -p /data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/outputimage
```

## 4. 提示文件

[model/coords.txt](model/coords.txt) 每行一个 `x y`，坐标基于每张输入图片的原始像素尺寸，不是 448 画布坐标。[model/labels.txt](model/labels.txt) 每行一个对应标签：

| 标签 | 含义 |
|---:|---|
| `0` | 负点 |
| `1` | 正点 |
| `2` | 框左上角 |
| `3` | 框右下角 |
| `-1` | padding 点 |

坐标点数量必须与标签数量一致，并且必须与 Decoder 的固定提示点数一致；当前 Decoder 要求两个提示点。批处理时，同一组原图坐标会按每张图片自己的宽高分别转换。

默认文件表示框 `(190,70)-(460,280)`。对于当前 `056.jpg`，该提示选中的是左上方球网区域，不是画面中的鸟；如需分割鸟，请按鸟在原图中的位置修改提示坐标和标签。

## 5. 编译和运行

```bash
cd /data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588
cmake -S . -B build \
  -DTARGET_SOC=rk3588 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/rknn_mobilesam_demo
```

构建 RPATH 指向项目内 [rknn_lib/](rknn_lib/)，从 build 目录直接运行时无需额外设置 `LD_LIBRARY_PATH`。

成功处理全部图片后，程序输出：

```text
Batch finished: 1 succeeded, 0 failed
```

默认结果保存为 [outputimage/056_out.png](outputimage/056_out.png)。

## 6. 本次验证

最终源码已在 RK3588 上重新编译，并连续完成两次独立进程回归：

- 两次均为 `1 succeeded, 0 failed`；
- 两次默认提示的 Mask score 均为 `0.833496`；
- 已检查输出 PNG，提示框和 Mask 均位于左上方球网区域；
- 运行日志保存在 [build/run.log](build/run.log) 和 [build/run-second.log](build/run-second.log)。
