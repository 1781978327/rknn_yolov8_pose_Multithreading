# YOLOv8 Pose 多线程检测 (RK3588)

基于 RK3588 NPU 的 YOLOv8 人体姿态检测多线程实现，支持实时摄像头检测和视频处理。

## 主要特性

* ✅ **YOLOv8-Pose 支持**：17 个关键点人体姿态检测
* ✅ **多线程优化**：使用线程池异步操作 RKNN 模型，提高 NPU 利用率
* ✅ **实时检测**：支持摄像头实时检测，FPS ~30
* ✅ **RTSP 推流**：支持推送到 RTSP 服务器
* ✅ **双摄像头**：支持双路摄像头同时检测
* ✅ **优化阈值**：修复误检测问题（BOX_THRESH: 0.25 → 0.5）

## 最新更新 (2024-03-24)

* 🔧 **修复误检测问题**：调整检测阈值 `BOX_THRESH` 从 0.25 提升到 0.5，有效过滤低置信度误检
* 🔧 **优化 NMS 阈值**：`NMS_THRESH` 从 0.45 调整到 0.4，与 rknn_model_zoo 官方版本保持一致
* 📝 添加详细的运行说明文档

## 技术栈

* C++ 实现，改自 [rknpu2](https://github.com/rockchip-linux/rknpu2)
* 使用 [线程池](https://github.com/senlinzhan/dpool) 异步操作 RKNN 模型
* Python 快速部署见 [rknn-multi-threaded](https://github.com/leafqycc/rknn-multi-threaded)
* **RK3568 等平台**：请自行修改 `include/rknnPool.hpp` 下的 `rknn_lite` 类和 `rknnPool` 构造函数

# 使用说明

## 编译

系统需安装 **OpenCV**：
```bash
sudo apt-get install libopencv-dev
```

编译项目：
```bash
./build-linux_RK3588.sh
```

可选：切换至 root 用户运行 `performance.sh` 定频提高性能和稳定性

## 运行

### 1. 摄像头实时检测
```bash
cd install/rknn_yolov8_demo_Linux
./rknn_yolov8_demo ./model/yolov8_pose.rknn 0
```

### 2. 视频文件检测
```bash
./rknn_yolov8_demo ./model/yolov8_pose.rknn 视频路径.mp4 输出.mp4
```

### 3. RTSP 推流
```bash
./rknn_yolov8_demo ./model/yolov8_pose.rknn 0 rtsp://127.0.0.1:8554/cam0
```

### 4. 双摄像头检测
```bash
./rknn_yolov8_demo ./model/yolov8_pose.rknn 0,1 rtsp://ip:8554/cam0,rtsp://ip:8554/cam1
```

## 检测阈值配置

在 `include/postprocess.h` 中可调整：
```cpp
#define NMS_THRESH 0.4   // NMS 阈值
#define BOX_THRESH 0.5   // 检测置信度阈值（推荐 0.5-0.7）
```

## 部署应用
  * 修改 `include/rknnPool.hpp` 中的 `rknn_lite` 类
  * 修改 `include/rknnPool.hpp` 中的 `rknnPool` 类的构造函数

# 多线程模型帧率测试
* 使用performance.sh进行CPU/NPU定频尽量减少误差
* 测试模型来源: 
* [yolov5s-silu](https://github.com/rockchip-linux/rknn-toolkit2/tree/master/examples/onnx/yolov5) 
* [yolov5s-relu](https://github.com/rockchip-linux/rknpu2/tree/master/examples/rknn_yolov5_demo/model/RK3588)
* 测试视频可见于 [bilibili](https://www.bilibili.com/video/BV1zo4y1x7aE/?spm_id_from=333.999.0.0)

|  模型\线程数   | 1    |  2   | 3  |  4  | 5  | 6  | 12  |
|  ----  | ----  |  ----  | ----  |  ----  | ----  | ----  | ----  |
| Yolov5s - silu  | 15.9269  | 32.9192 | 52.8330  | 46.6782 | 58.2921 | 71.8070 |  |
| Yolov5s - relu  | 26.8601 | 58.0305 | 77.6904 | 80.7144 | 93.9126 | 101.1400 | 122.7334 |

# 补充
* 异常处理尚未完善, 目前仅支持rk3588/rk3588s下的运行
* relu版本相较于silu有着较大性能提升, 以及存在一些精度损失, 详情见于[rknn_model_zoo](https://github.com/airockchip/rknn_model_zoo/tree/main/models/CV/object_detection/yolo)

# Acknowledgements
* https://github.com/rockchip-linux/rknpu2
* https://github.com/senlinzhan/dpool
* https://github.com/ultralytics/yolov5
* https://github.com/airockchip/rknn_model_zoo
* https://github.com/rockchip-linux/rknn-toolkit2
