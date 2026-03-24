# RK3588 YOLOv8 Pose + DeepSORT 双摄像头跟踪系统

## 功能特性

- **双摄像头实时推理**：同时处理两个摄像头输入，独立 YOLOv8 Pose 模型
- **DeepSORT 目标跟踪**：基于 ReID 特征的目标跟踪，支持多目标追踪
- **硬件加速解码**：利用 RK3588 MPP 硬件进行 H.264/H.265 视频解码
- **RTSP 推流支持**：支持 RTSP 输入源，可接收网络摄像头推流
- **MediaMTX 流媒体服务**：内置 RTSP 服务器，支持多客户端拉流

## 支持平台

RK3588, RK3588S, RK3576

## 项目结构

```
yolov8-rk3588-cpp-3-15/
├── src/                    # 主程序源码
│   ├── main_dual_camera.cc # 双摄像头主程序
│   └── main_with_deepsort.cc
├── deepsort/              # DeepSORT 跟踪模块
│   ├── src/
│   └── include/
├── include/               # 头文件
├── model/                 # 模型文件
│   ├── yolov8_pose.rknn   # YOLOv8 Pose 模型
│   └── osnet_x0_25_market.rknn  # ReID 模型
├── mediamtx_bin/           # RTSP 流媒体服务器
│   ├── mediamtx           # 可执行文件
│   └── mediamtx.yml       # 配置文件
├── build/                 # 编译输出
└── video/                 # 测试视频
```

## 快速开始

### 1. 编译项目

```bash
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15
mkdir -p build && cd build
cmake .. && make -j4
```

### 2. 启动 MediaMTX 流媒体服务

```bash
# 在后台启动 RTSP 服务器
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15
./mediamtx_bin/mediamtx &

# 验证服务启动
# RTSP 监听端口: 8554
# HLS 监听端口: 8888
# WebRTC 监听端口: 8889
```

### 3. 运行程序

**双本地摄像头模式：**
```bash
./build/yolov8_pose_dual_cam ../model/yolov8_pose.rknn 0 2
```

**RTSP 推流模式：**
```bash
# camera0 和 camera1 为本地摄像头设备号
# --rtsp0/--rtsp1 指定 RTSP 输入源
./build/yolov8_pose_dual_cam ../model/yolov8_pose.rknn 0 2 \
    --rtsp0 rtsp://192.168.1.100:8554/stream0 \
    --rtsp1 rtsp://192.168.1.100:8554/stream1
```

**无窗口模式（服务器部署）：**
```bash
./build/yolov8_pose_dual_cam ../model/yolov8_pose.rknn 0 2 --no-display
```

### 4. 查看 RTSP 流

使用 VLC 或 FFmpeg 拉流：
```bash
# VLC 打开 URL
rtsp://localhost:8554/stream0
rtsp://localhost:8554/stream1

# FFmpeg 录制
ffmpeg -i rtsp://localhost:8554/stream0 -c copy output.mp4
```

## 功能说明

### 硬件加速

- **NPU 加速**：YOLOv8 Pose 和 ReID 模型在 NPU 上运行
  - Core 0/1：YOLOv8 Pose 推理
  - Core 2：ReID 特征提取
- **MPP 硬解**：视频解码使用 RK3588 硬件解码器
- **多线程 ReID**：双线程并行处理检测目标的特征提取

### MediaMTX 配置

默认配置监听端口：
- `rtspAddress: :8554` - RTSP 推拉流
- `hlsAddress: :8888` - HLS 低延迟播放
- `webrtcAddress: :8889` - WebRTC

### DeepSORT 跟踪

- 基于 OSNet ReID 模型提取目标特征
- 卡尔曼滤波预测目标运动
- 匈牙利算法进行特征匹配
- 支持跨摄像头目标关联

## 性能指标

典型配置（双 1080P 摄像头）：
- Pose 检测：约 15-20ms/帧
- ReID 特征提取：约 7-10ms/帧
- 整体帧率：约 20-25 FPS

## 依赖环境

- RK3588 SDK
- rockchip_mpp 库
- OpenCV 4.x
- librkkernel.so

## 目录

- [运行说明](运行说明.md) - 详细运行指南
- [双摄像头使用](DUAL_CAMERA_USAGE.md) - 双摄配置说明
- [DeepSORT 说明](DEEPSORT_README.md) - 跟踪模块说明
- [集成指南](INTEGRATION_GUIDE.md) - 系统集成说明
