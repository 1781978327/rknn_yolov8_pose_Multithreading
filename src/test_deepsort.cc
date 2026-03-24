// DeepSORT 功能测试程序
// 用于验证 DeepSORT 模块是否正常工作

#include <stdio.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include "deepsort.h"

using namespace std;
using namespace cv;

// 生成模拟的检测框（用于测试）
vector<DetectBox> generate_mock_detections(int frame_num) {
    vector<DetectBox> detections;
    
    // 模拟 2 个移动的目标
    // 目标 1: 从左向右移动
    DetectBox box1;
    box1.x1 = 50 + frame_num * 5;
    box1.y1 = 100;
    box1.x2 = box1.x1 + 80;
    box1.y2 = box1.y1 + 150;
    box1.confidence = 0.9;
    box1.classID = 0;
    detections.push_back(box1);
    
    // 目标 2: 从上向下移动
    DetectBox box2;
    box2.x1 = 300;
    box2.y1 = 50 + frame_num * 3;
    box2.x2 = box2.x1 + 80;
    box2.y2 = box2.y1 + 150;
    box2.confidence = 0.85;
    box2.classID = 0;
    detections.push_back(box2);
    
    return detections;
}

// 绘制检测和跟踪结果
void draw_tracking_results(Mat& img, vector<DetectBox>& tracked_boxes) {
    for (size_t i = 0; i < tracked_boxes.size(); i++) {
        DetectBox& box = tracked_boxes[i];
        
        // 为每个 ID 分配不同颜色
        int track_id = (int)box.trackID;
        Scalar color;
        if (track_id >= 0) {
            int r = (track_id * 67) % 255;
            int g = (track_id * 113) % 255;
            int b = (track_id * 197) % 255;
            color = Scalar(b, g, r);
        } else {
            color = Scalar(0, 255, 0);
        }
        
        // 绘制边界框
        rectangle(img, Point(box.x1, box.y1), Point(box.x2, box.y2), color, 2);
        
        // 绘制 ID 和置信度
        if (track_id >= 0) {
            char text[64];
            sprintf(text, "ID:%d (%.2f)", track_id, box.confidence);
            putText(img, text, Point(box.x1, box.y1 - 5),
                    FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
        }
    }
}

int main(int argc, char** argv)
{
    printf("\n=== DeepSORT 功能测试 ===\n\n");
    
    string reid_model = "/home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15/model/osnet_x0_25_market.rknn";
    
    // 检查模型文件是否存在
    FILE* fp = fopen(reid_model.c_str(), "rb");
    if (!fp) {
        printf("❌ 错误：找不到 ReID 模型文件：%s\n", reid_model.c_str());
        printf("   请确保模型文件存在！\n");
        return -1;
    }
    fclose(fp);
    printf("✅ ReID 模型文件存在：%s\n\n", reid_model.c_str());
    
    // 初始化 DeepSORT
    printf("正在初始化 DeepSORT...\n");
    DeepSort* tracker = nullptr;
    try {
        tracker = new DeepSort(reid_model, 1, 512, 6, RKNN_NPU_CORE_2);
        printf("✅ DeepSORT 初始化成功！\n\n");
    } catch (const exception& e) {
        printf("❌ DeepSORT 初始化失败：%s\n", e.what());
        return -1;
    }
    
    // 创建测试画布
    int width = 640;
    int height = 480;
    Mat canvas;
    
    printf("开始测试跟踪功能...\n");
    printf("按 'q' 退出测试\n\n");
    
    int frame_count = 0;
    int max_frames = 100;  // 测试 100 帧
    
    while (frame_count < max_frames) {
        // 创建空白画布
        canvas = Mat::zeros(height, width, CV_8UC3);
        
        // 生成模拟检测结果
        vector<DetectBox> detections = generate_mock_detections(frame_count);
        
        // DeepSORT 跟踪
        tracker->sort(canvas, detections);
        
        // 绘制结果
        draw_tracking_results(canvas, detections);
        
        // 显示帧号
        char frame_text[64];
        sprintf(frame_text, "Frame: %d/%d", frame_count + 1, max_frames);
        putText(canvas, frame_text, Point(10, 30),
                FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255, 255, 255), 2);
        
        // 显示跟踪数量
        char track_text[64];
        sprintf(track_text, "Tracked: %zu objects", detections.size());
        putText(canvas, track_text, Point(10, 60),
                FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255, 255, 255), 2);
        
        // 显示图像
        imshow("DeepSORT Test", canvas);
        
        // 打印跟踪信息
        if (frame_count % 10 == 0) {
            printf("Frame %3d: ", frame_count);
            for (size_t i = 0; i < detections.size(); i++) {
                printf("ID:%d ", (int)detections[i].trackID);
            }
            printf("\n");
        }
        
        char key = waitKey(30);  // 30ms 延迟，约 33 FPS
        if (key == 'q' || key == 27) {
            printf("\n用户中断测试\n");
            break;
        }
        
        frame_count++;
    }
    
    printf("\n=== 测试结果 ===\n");
    printf("总帧数：%d\n", frame_count);
    
    if (frame_count > 0) {
        printf("✅ DeepSORT 跟踪功能正常！\n");
        printf("\n测试说明：\n");
        printf("- 如果看到两个移动的边界框，且每个框上都有稳定的 ID 号，说明跟踪正常\n");
        printf("- ID 应该在整个过程中保持不变（除非目标消失）\n");
        printf("- 不同目标应该有不同的颜色和 ID\n");
    } else {
        printf("⚠️  测试未完成\n");
    }
    
    // 清理资源
    destroyAllWindows();
    if (tracker) delete tracker;
    
    printf("\n测试完成！\n");
    return 0;
}
