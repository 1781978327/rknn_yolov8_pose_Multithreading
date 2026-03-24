# 双摄像头 YOLOv8 Pose + DeepSORT 使用指南

## 🎯 功能特性

✅ **同时处理两个摄像头**  
✅ **独立的姿态检测和跟踪**（每个摄像头独立跟踪 ID）  
✅ **并行处理**（两个线程同时运行）  
✅ **NPU 核心分配**：
- Camera 0 → NPU Core 0
- Camera 1 → NPU Core 1
- DeepSORT → NPU Core 2（共享）

---

## 🚀 使用方法

### 基本用法

```bash
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/build

# 使用摄像头 0 和 2
./rknn_yolov8_dual_camera ../model/yolov8_pose.rknn 0 2

# 使用设备路径
./rknn_yolov8_dual_camera ../model/yolov8_pose.rknn /dev/video0 /dev/video2
```

---

## 📊 显示效果

程序会打开两个窗口：
- **Camera 0** - 第一个摄像头的实时检测和跟踪
- **Camera 1** - 第二个摄像头的实时检测和跟踪

每个窗口显示：
- 彩色边界框 + 跟踪 ID
- 17 个关键点
- 骨架连线
- FPS 和统计信息

---

## 💡 重要说明

### 1. 摄像头编号

查看可用摄像头：
```bash
ls /dev/video*
# 输出: /dev/video0  /dev/video2  /dev/video4 ...
```

常见配置：
- **单摄像头**：通常是 `/dev/video0`
- **双摄像头**：通常是 `/dev/video0` 和 `/dev/video2`（注意不是 video1）

### 2. 性能预期

- **单摄像头版本**：15-20 FPS
- **双摄像头版本**：每个摄像头 10-15 FPS（总共 20-30 FPS）

性能取决于：
- 检测目标数量
- 视频分辨率
- 系统负载

### 3. NPU 核心分配

```
┌─────────────────────────────────────┐
│         RK3588 NPU 架构             │
├─────────────────────────────────────┤
│ NPU Core 0 → Camera 0 检测          │
│ NPU Core 1 → Camera 1 检测          │
│ NPU Core 2 → DeepSORT (共享)        │
└─────────────────────────────────────┘
```

---

## 🎮 控制方式

- **退出程序**：按 `q` 或 `ESC`
- **窗口调整**：可以拖动和调整窗口大小

---

## 📝 输出示例

```
=== YOLOv8 Pose + DeepSORT 双摄像头跟踪系统 ===

正在加载模型...
✅ 两个 YOLOv8 Pose 模型加载成功 (NPU Core 0 & 1)

正在初始化 DeepSORT...
✅ 两个 DeepSORT 跟踪器初始化成功

✅ 两个摄像头打开成功
   Camera 0: 0
   Camera 1: 2

开始处理... 按 'q' 退出

Cam0 Frame 30 | FPS: 12.5 | Det: 2 | Track: 2
Cam1 Frame 30 | FPS: 13.1 | Det: 1 | Track: 1
Cam0 Frame 60 | FPS: 12.8 | Det: 2 | Track: 2
Cam1 Frame 60 | FPS: 13.3 | Det: 1 | Track: 1
...
```

---

## ⚠️ 常见问题

### Q1: 无法打开摄像头

**检查摄像头是否存在**：
```bash
ls /dev/video*
v4l2-ctl --list-devices
```

**确保摄像头未被占用**：
```bash
# 杀死其他占用摄像头的进程
killall rknn_yolov8_demo
killall rknn_yolov8_with_deepsort
```

### Q2: 性能太低

**解决方法**：
1. 降低视频分辨率
2. 限制跟踪目标数量
3. 使用隔帧跟踪

### Q3: 两个窗口不同步

这是正常的！两个摄像头独立处理，帧率可能略有差异。

### Q4: ID 在两个摄像头间不一致

这也是正常的！每个摄像头有独立的跟踪器，ID 是分别分配的。如果需要跨摄像头跟踪，需要额外的跨摄像头匹配逻辑。

---

## 🔧 高级配置

### 调整 DeepSORT 参数

在 `src/main_dual_camera.cc` 中修改：

```cpp
// 第 285 行附近
DeepSort* tracker0 = new DeepSort("../model/osnet_x0_25_market.rknn", 
                                  1,      // batch_size
                                  512,    // feature_dim
                                  6,      // cpu_id
                                  RKNN_NPU_CORE_2);
```

### 修改检测阈值

在 `include/postprocess.h` 中：
```cpp
#define BOX_THRESH    0.5   // 降低可检测更多目标
#define NMS_THRESH    0.45
```

---

## 📊 性能对比

| 版本 | 摄像头数 | 总 FPS | 单摄像头 FPS | NPU 利用率 |
|------|---------|--------|-------------|-----------|
| 单摄像头 | 1 | 15-20 | 15-20 | ~33% |
| 双摄像头 | 2 | 20-30 | 10-15 | ~66% |

---

## 🎯 应用场景

✅ **多角度监控**：同时监控不同角度  
✅ **入口/出口**：分别监控进出人员  
✅ **大范围覆盖**：扩大监控范围  
✅ **对比分析**：同时对比两个场景  

---

## 📚 相关文档

- `USAGE.md` - 单摄像头使用指南
- `DEEPSORT_README.md` - DeepSORT 详细说明
- `INTEGRATION_GUIDE.md` - 集成方案说明

---

## 🎉 总结

你现在有 **3 个可执行程序**：

1. **`rknn_yolov8_demo`** - 原始 YOLOv8 Pose（无跟踪）
2. **`rknn_yolov8_with_deepsort`** - 单摄像头 + DeepSORT ⭐
3. **`rknn_yolov8_dual_camera`** - 双摄像头 + DeepSORT ⭐⭐

根据需求选择合适的版本！
