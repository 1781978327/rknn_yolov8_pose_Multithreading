# YOLOv8 Pose + DeepSORT 跟踪系统

## 📋 功能说明

本项目已成功集成 **DeepSORT 多目标跟踪**功能，实现：
- ✅ YOLOv8 Pose 人体姿态检测（17个关键点）
- ✅ DeepSORT 目标跟踪（为每个人分配唯一 ID）
- ✅ 带 ID 的姿态可视化（不同 ID 显示不同颜色）
- ✅ 实时视频/摄像头处理

## 🎯 ReID 模型说明

### `osnet_x0_25_market.rknn` 是什么？

这是 **DeepSORT 的 ReID（Re-Identification，重识别）模型**，用于：

1. **提取外观特征**：为每个检测到的人提取 512 维特征向量
2. **目标匹配**：通过特征相似度在不同帧之间匹配同一个人
3. **ID 分配**：为持续出现的目标分配稳定的跟踪 ID

**模型详情**：
- 名称：OSNet x0.25
- 来源：TorchReID (https://github.com/KaiyangZhou/deep-person-reid)
- 输入尺寸：256×128（人体裁剪图）
- 输出：512 维特征向量
- 训练数据集：Market-1501（行人重识别数据集）
- 推理时间：~3ms/人（RK3588 NPU Core 2）

## 🏗️ 项目结构

```
yolov8-rk3588-cpp-3-15/
├── deepsort/                    # DeepSORT 模块（已移植）
│   ├── include/                 # 头文件
│   │   ├── deepsort.h          # DeepSORT 主类
│   │   ├── featuretensor.h     # ReID 特征提取
│   │   ├── tracker.h           # 卡尔曼滤波 + 匈牙利匹配
│   │   └── ...
│   ├── src/                     # 源文件
│   └── CMakeLists.txt
├── model/
│   ├── yolov8_pose.rknn        # YOLOv8 Pose 检测模型
│   └── osnet_x0_25_market.rknn # DeepSORT ReID 模型
├── src/
│   ├── main.cc                  # 原始 YOLOv8 Pose 程序
│   ├── main_pose_track.cc      # YOLOv8 Pose + DeepSORT（新增）
│   └── ...
└── CMakeLists.txt               # 已更新支持 DeepSORT
```

## 🔧 编译

### 依赖检查

确保已安装 Eigen3：
```bash
sudo apt install libeigen3-dev
```

### 编译项目

```bash
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15
./build-linux_RK3588.sh
```

编译成功后会生成两个可执行文件：
- `rknn_yolov8_demo` - 原始姿态检测程序
- `rknn_yolov8_pose_track` - **姿态检测 + 跟踪程序（新）**

## 🚀 运行

### 使用摄像头

```bash
cd install/rknn_yolov8_demo_Linux
./rknn_yolov8_pose_track 0
```

### 使用视频文件

```bash
./rknn_yolov8_pose_track /path/to/video.mp4
```

## 🎨 显示效果

- **边界框**：每个人用不同颜色的框标识
- **跟踪 ID**：框上方显示 "ID:X"
- **关键点**：17 个身体关键点（鼻子、眼睛、肩膀、手肘等）
- **骨架**：连接关键点的线条
- **FPS 显示**：左上角显示实时帧率

**颜色规则**：
- 每个跟踪 ID 分配唯一颜色（基于 ID 哈希）
- 同一个人在不同帧保持相同颜色和 ID
- 新出现的人分配新的 ID

## ⚙️ 工作原理

### 流程图

```
视频帧
  ↓
YOLOv8 Pose 检测
  ↓
检测结果（边界框 + 17关键点）
  ↓
裁剪人体区域
  ↓
ReID 特征提取（osnet_x0_25）
  ↓
DeepSORT 跟踪
  ├─ 卡尔曼滤波预测
  ├─ 特征匹配（余弦距离）
  └─ 匈牙利算法分配
  ↓
带 ID 的跟踪结果
  ↓
可视化显示
```

### NPU 核心分配

- **NPU Core 0-1**：YOLOv8 Pose 检测（多线程池）
- **NPU Core 2**：DeepSORT ReID 特征提取

## 📊 性能参数

### 单帧处理时间（RK3588）

| 组件 | 时间 | 说明 |
|------|------|------|
| YOLOv8 Pose 检测 | ~30ms | 640×640 输入 |
| ReID 特征提取 | ~3ms/人 | 受检测人数影响 |
| 跟踪算法 | ~1-2ms | 卡尔曼滤波 + 匹配 |
| **总计** | ~35-50ms | 取决于人数 |

### 预期 FPS

- **1-2 人**：~25-30 FPS
- **3-5 人**：~20-25 FPS
- **6-10 人**：~15-20 FPS

## 🔍 DeepSORT 参数调整

在 `deepsort/include/deepsort.h` 中可调整：

```cpp
const int track_interval = 1;  // 跟踪间隔（帧）
```

在 `deepsort/src/deepsort.cpp` 中可调整：
- `maxCosineDist`：特征匹配阈值（默认 0.2）
- `maxBudget`：特征库大小（默认 100）
- `confThres`：检测置信度阈值

## 🆚 与原版对比

| 特性 | 原版 YOLOv8 Pose | + DeepSORT |
|------|-----------------|------------|
| 检测功能 | ✅ 姿态检测 | ✅ 姿态检测 |
| 目标跟踪 | ❌ 无 | ✅ 有 |
| ID 分配 | ❌ 无 | ✅ 唯一 ID |
| 跨帧匹配 | ❌ 无 | ✅ 稳定跟踪 |
| 应用场景 | 单帧分析 | 轨迹分析、计数 |
| FPS | ~30 | ~20-25 |

## 📝 应用场景

1. **人流统计**：统计进出人数，分配唯一 ID
2. **轨迹分析**：记录每个人的运动轨迹
3. **动作识别**：基于姿态序列识别动作
4. **异常检测**：检测摔倒、打架等异常姿态
5. **健身辅助**：跟踪运动姿态，计数重复次数

## ⚠️ 注意事项

1. **模型文件**：确保 `model/` 目录下有两个模型文件
   - `yolov8_pose.rknn`
   - `osnet_x0_25_market.rknn`

2. **内存占用**：DeepSORT 会缓存特征，长时间运行可能占用较多内存

3. **ID 切换**：在遮挡严重或人员密集时可能出现 ID 切换

4. **性能优化**：
   - 减少检测人数可提高 FPS
   - 调整 `track_interval` 可隔帧跟踪加速
   - 使用更小的 ReID 模型可降低延迟

## 🐛 故障排除

### 编译错误：找不到 Eigen

```bash
sudo apt install libeigen3-dev
```

### 运行错误：找不到 ReID 模型

确保模型文件存在：
```bash
ls -lh model/osnet_x0_25_market.rknn
```

### FPS 过低

1. 检查是否有多个程序占用 NPU
2. 尝试降低视频分辨率
3. 调整 `track_interval` 隔帧跟踪

## 📚 参考资料

- **YOLOv8**: https://github.com/ultralytics/ultralytics
- **DeepSORT**: https://github.com/nwojke/deep_sort
- **OSNet**: https://github.com/KaiyangZhou/deep-person-reid
- **RKNN Model Zoo**: https://github.com/airockchip/rknn_model_zoo

## 🎉 总结

DeepSORT 已成功移植到 YOLOv8 Pose 项目！现在你可以：
- ✅ 实时检测人体姿态
- ✅ 为每个人分配稳定的跟踪 ID
- ✅ 跨帧跟踪目标轨迹
- ✅ 应用于人流统计、轨迹分析等场景

**快速开始**：
```bash
cd install/rknn_yolov8_demo_Linux
./rknn_yolov8_pose_track 0  # 使用摄像头 0
```

按 `q` 退出程序。
