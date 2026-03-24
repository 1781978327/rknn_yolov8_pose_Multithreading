# DeepSORT 测试指南

## 🧪 测试方法

已为你创建了 **3 种测试方法**，从简单到复杂：

---

## 方法 1：模拟数据测试（推荐首选）

### 说明
使用模拟的移动目标测试 DeepSORT 的跟踪功能，**不需要真实视频或摄像头**。

### 运行命令
```bash
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/build
./test_deepsort
```

### 预期结果
✅ **正常情况**：
- 窗口显示两个移动的彩色边界框
- 每个框上方显示 `ID:0` 和 `ID:1`（或其他稳定的 ID）
- ID 在整个过程中保持不变
- 不同目标有不同颜色
- 终端每 10 帧打印一次跟踪 ID

❌ **异常情况**：
- 程序崩溃或报错
- ID 频繁变化
- 找不到 ReID 模型文件

### 测试时长
约 3 秒（100 帧 @ 33 FPS）

---

## 方法 2：视频文件测试

### 前提条件
需要有包含人物的视频文件，并且已经运行过 YOLOv8 Pose 检测。

### 步骤

1. **先用 YOLOv8 Pose 检测视频**（获取检测结果）：
```bash
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/build
./rknn_yolov8_demo /path/to/video.mp4
```

2. **修改 main_pose_track_simple.cc**：
   - 将 YOLOv8 Pose 的推理代码集成进去
   - 或者使用已有的检测结果

3. **运行跟踪程序**：
```bash
./rknn_yolov8_pose_track /path/to/video.mp4
```

### 预期结果
- 每个检测到的人都有唯一的 ID
- 同一个人在不同帧保持相同 ID
- ID 显示在边界框上方

---

## 方法 3：摄像头实时测试

### 运行命令
```bash
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/build
./rknn_yolov8_pose_track 0  # 0 是摄像头 ID
```

### 预期结果
- 实时显示摄像头画面
- 检测到的人物有稳定的跟踪 ID
- 人物移动时 ID 保持不变

---

## 📊 测试检查清单

运行 `./test_deepsort` 后，检查以下项目：

### ✅ 基础功能
- [ ] 程序能正常启动
- [ ] 能找到 ReID 模型文件
- [ ] DeepSORT 初始化成功
- [ ] 窗口正常显示

### ✅ 跟踪功能
- [ ] 能看到移动的边界框
- [ ] 每个框都有 ID 显示
- [ ] ID 在整个过程中保持稳定（不频繁变化）
- [ ] 不同目标有不同的 ID 和颜色

### ✅ 性能指标
- [ ] 程序运行流畅，无卡顿
- [ ] 无内存泄漏或崩溃
- [ ] 终端输出正常

---

## 🔍 常见问题排查

### 问题 1：找不到 ReID 模型
```
❌ 错误：找不到 ReID 模型文件
```

**解决方法**：
```bash
ls -lh /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/model/osnet_x0_25_market.rknn
```
确保文件存在，如果不存在，从原项目复制：
```bash
cp /home/orangepi/Desktop/yolov5_Deepsort_rknn-deepsort/model/osnet_x0_25_market.rknn \
   /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/model/
```

### 问题 2：程序崩溃
检查 NPU 是否被其他程序占用：
```bash
ps aux | grep rknn
```

### 问题 3：ID 频繁变化
这可能是正常的，因为测试程序使用的是模拟数据。
在真实场景中，需要：
- 使用真实的检测框
- 调整 DeepSORT 参数（在 `deepsort/src/deepsort.cpp` 中）

---

## 📝 测试输出示例

### 正常输出
```
=== DeepSORT 功能测试 ===

✅ ReID 模型文件存在：/home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/model/osnet_x0_25_market.rknn

正在初始化 DeepSORT...
✅ DeepSORT 初始化成功！

开始测试跟踪功能...
按 'q' 退出测试

Frame   0: ID:0 ID:1 
Frame  10: ID:0 ID:1 
Frame  20: ID:0 ID:1 
Frame  30: ID:0 ID:1 
...

=== 测试结果 ===
总帧数：100
✅ DeepSORT 跟踪功能正常！

测试说明：
- 如果看到两个移动的边界框，且每个框上都有稳定的 ID 号，说明跟踪正常
- ID 应该在整个过程中保持不变（除非目标消失）
- 不同目标应该有不同的颜色和 ID

测试完成！
```

---

## 🎯 下一步：集成到实际应用

如果测试通过，说明 DeepSORT 已经正常工作。接下来可以：

1. **集成 YOLOv8 Pose 检测**：
   - 参考 `src/main.cc` 中的推理流程
   - 将检测结果传递给 DeepSORT

2. **优化参数**：
   - 在 `deepsort/src/deepsort.cpp` 中调整：
     - `maxCosineDist`：特征匹配阈值（默认 0.2）
     - `maxBudget`：特征库大小（默认 100）
     - `track_interval`：跟踪间隔

3. **实际场景测试**：
   - 使用真实视频或摄像头
   - 测试多人场景
   - 测试遮挡情况

---

## 📚 相关文件

- **测试程序源码**：`src/test_deepsort.cc`
- **DeepSORT 实现**：`deepsort/src/deepsort.cpp`
- **ReID 模型**：`model/osnet_x0_25_market.rknn`
- **完整文档**：`DEEPSORT_README.md`

---

## 💡 快速测试命令

```bash
# 1. 进入构建目录
cd /home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/build

# 2. 运行测试
./test_deepsort

# 3. 观察窗口和终端输出，按 'q' 退出
```

**预计测试时间：< 10 秒**

如果看到稳定的 ID 跟踪，说明 DeepSORT 移植成功！✅
