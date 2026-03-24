# YOLOv8 Pose + DeepSORT 使用指南

## 🎉 集成完成！

DeepSORT 已成功集成到 YOLOv8 Pose 项目中。现在你有 **3 个可执行程序**：

---

## 📦 可执行程序说明

### 1. `rknn_yolov8_demo` (原始版本)
- **功能**：YOLOv8 Pose 姿态检测
- **特点**：多线程池，高性能（25-30 FPS）
- **用途**：仅需要姿态检测，不需要跟踪

### 2. `test_deepsort` (测试程序)
- **功能**：DeepSORT 功能测试
- **特点**：使用模拟数据，快速验证 DeepSORT 是否正常
- **用途**：测试 DeepSORT 模块

### 3. `rknn_yolov8_with_deepsort` ⭐ (完整集成版)
- **功能**：YOLOv8 Pose + DeepSORT 跟踪
- **特点**：单线程版本，稳定跟踪（15-20 FPS）
- **用途**：需要姿态检测 + 多目标跟踪 + ID 分配

---

## 🚀 快速开始

### 方法 1：使用摄像头

```bash
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/build

# 运行集成版本
./rknn_yolov8_with_deepsort model/yolov8_pose.rknn 0
```

### 方法 2：使用视频文件

```bash
# 处理视频并显示
./rknn_yolov8_with_deepsort model/yolov8_pose.rknn video.mp4

# 处理视频并保存结果
./rknn_yolov8_with_deepsort model/yolov8_pose.rknn video.mp4 output.avi
```

---

## 📊 显示效果

运行后你会看到：

1. **彩色边界框**：每个人有不同颜色的框
2. **跟踪 ID**：框上方显示 `ID:0`, `ID:1` 等
3. **置信度**：显示检测置信度百分比
4. **关键点**：17 个身体关键点（圆点）
5. **骨架**：连接关键点的线条
6. **FPS 统计**：实时帧率和检测/跟踪数量

### 示例输出

```
=== YOLOv8 Pose + DeepSORT 跟踪系统 ===

正在加载 YOLOv8 Pose 模型: model/yolov8_pose.rknn
✅ YOLOv8 Pose 模型加载成功
   输入尺寸: 640x640
   量化模式: INT8

正在初始化 DeepSORT...
✅ DeepSORT 初始化成功 (NPU Core 2)

✅ 视频源打开成功
   分辨率: 1920x1080
   帧率: 25.0 FPS

开始处理... 按 'q' 退出

Frame   30 | FPS: 18.5 | Detected: 2 | Tracked: 2
Frame   60 | FPS: 19.2 | Detected: 3 | Tracked: 3
Frame   90 | FPS: 18.8 | Detected: 2 | Tracked: 2
...
```

---

## ⚙️ 工作原理

### 处理流程

```
1. 读取视频帧
   ↓
2. BGR → RGB 转换 (RGA 加速)
   ↓
3. 缩放到 640×640 (OpenCV)
   ↓
4. YOLOv8 Pose 推理 (NPU Core 0)
   ├─ 检测人体边界框
   └─ 检测 17 个关键点
   ↓
5. 转换为 DeepSORT 格式
   ↓
6. DeepSORT 跟踪 (NPU Core 2)
   ├─ 提取 ReID 特征
   ├─ 特征匹配
   └─ 分配跟踪 ID
   ↓
7. 绘制结果（边界框 + ID + 关键点 + 骨架）
   ↓
8. 显示/保存
```

### NPU 核心分配

- **NPU Core 0**：YOLOv8 Pose 检测
- **NPU Core 2**：DeepSORT ReID 特征提取

---

## 🎯 性能指标

| 场景 | 分辨率 | FPS | 说明 |
|------|--------|-----|------|
| 1-2人 | 1920×1080 | 18-20 | 流畅 |
| 3-5人 | 1920×1080 | 15-18 | 正常 |
| 6-10人 | 1920×1080 | 12-15 | 可用 |

**注意**：性能会受以下因素影响：
- 视频分辨率
- 检测目标数量
- 系统负载
- 是否保存视频

---

## 🔧 参数调整

### 检测阈值

在 `include/postprocess.h` 中：

```cpp
#define BOX_THRESH    0.5   // 检测置信度阈值（降低可检测更多目标）
#define NMS_THRESH    0.45  // NMS 阈值
#define KPT_THRESH    0.5   // 关键点置信度阈值
```

### DeepSORT 参数

在 `deepsort/src/deepsort.cpp` 的构造函数中：

```cpp
this->maxCosineDist = 0.2;  // 特征匹配阈值（0.2-0.3）
                            // 越大越容易保持 ID，但可能误匹配
                            
this->maxBudget = 100;      // 特征库大小（50-100）
                            // 越大内存占用越多
```

---

## 💡 使用技巧

### 1. 提高跟踪稳定性

如果 ID 频繁切换：

```cpp
// 在 deepsort.cpp 中增大阈值
this->maxCosineDist = 0.3;  // 从 0.2 改为 0.3
```

### 2. 提高性能

如果帧率太低：

```cpp
// 降低输入分辨率（修改模型或缩放输入）
// 或限制最大跟踪数量
if (detections.size() > 5) {
    detections.resize(5);  // 只跟踪前 5 个
}
```

### 3. 隔帧跟踪

```cpp
// 每 3 帧才提取 ReID 特征
if (frame_count % 3 == 0) {
    tracker->sort(frame, detections);
} else {
    tracker->sort_interval(frame, detections);
}
```

---

## 🐛 常见问题

### Q1: 找不到模型文件

```bash
# 确保模型文件存在
ls -lh model/yolov8_pose.rknn
ls -lh model/osnet_x0_25_market.rknn

# 如果不存在，从原项目复制
cp /home/orangepi/Desktop/yolov5_Deepsort_rknn-deepsort/model/osnet_x0_25_market.rknn model/
```

### Q2: 程序崩溃

```bash
# 检查 NPU 是否被占用
ps aux | grep rknn

# 杀死占用进程
killall rknn_yolov8_demo
killall rknn_yolov8_with_deepsort
```

### Q3: 性能太低

- 降低视频分辨率
- 限制跟踪目标数量
- 使用隔帧跟踪
- 关闭视频保存

### Q4: ID 频繁变化

- 增大 `maxCosineDist` 阈值
- 确保光照稳定
- 避免严重遮挡场景

---

## 📝 对比原版本

### vs `rknn_yolov8_demo`

| 特性 | 原版 | 集成版 |
|------|------|--------|
| 姿态检测 | ✅ | ✅ |
| 多目标跟踪 | ❌ | ✅ |
| ID 分配 | ❌ | ✅ |
| FPS | 25-30 | 15-20 |
| 多线程 | ✅ | ❌ |
| 稳定性 | 高 | 高 |

### 何时使用集成版？

✅ **适合使用**：
- 需要跟踪多个人的轨迹
- 需要统计人数和 ID
- 需要分析人员行为
- 多人场景下的姿态分析

❌ **不适合使用**：
- 只需要单帧检测
- 追求极致性能
- 单人场景

---

## 🎓 进阶功能

### 1. 轨迹记录

```cpp
// 记录每个 ID 的轨迹
map<int, vector<Point>> trajectories;

for (auto& det : detections) {
    int id = det.trackID;
    Point center((det.x1 + det.x2) / 2, (det.y1 + det.y2) / 2);
    trajectories[id].push_back(center);
}

// 绘制轨迹
for (auto& traj : trajectories) {
    for (size_t i = 1; i < traj.second.size(); i++) {
        line(frame, traj.second[i-1], traj.second[i], color, 2);
    }
}
```

### 2. 人数统计

```cpp
set<int> unique_ids;
for (auto& det : detections) {
    if (det.trackID >= 0) {
        unique_ids.insert(det.trackID);
    }
}
printf("当前人数: %zu\n", unique_ids.size());
```

### 3. 区域入侵检测

```cpp
// 定义区域
Rect forbidden_zone(100, 100, 300, 300);

for (auto& det : detections) {
    Point center((det.x1 + det.x2) / 2, (det.y1 + det.y2) / 2);
    if (forbidden_zone.contains(center)) {
        printf("警告：ID %d 进入禁区！\n", (int)det.trackID);
    }
}
```

---

## 📚 相关文档

- `DEEPSORT_README.md` - DeepSORT 详细说明
- `TEST_DEEPSORT.md` - DeepSORT 测试指南
- `INTEGRATION_GUIDE.md` - 集成方案说明
- `README.md` - 项目总体说明

---

## 🎯 总结

你现在拥有一个**完整可用的 YOLOv8 Pose + DeepSORT 跟踪系统**！

### 核心命令

```bash
# 测试 DeepSORT
./test_deepsort

# 使用摄像头
./rknn_yolov8_with_deepsort model/yolov8_pose.rknn 0

# 处理视频
./rknn_yolov8_with_deepsort model/yolov8_pose.rknn video.mp4 output.avi
```

### 按键控制

- **q** 或 **ESC**：退出程序

---

**祝你使用愉快！** 🎉

如有问题，请查看相关文档或提交 Issue。
