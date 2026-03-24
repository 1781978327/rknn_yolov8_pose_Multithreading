// YOLOv8 Pose + DeepSORT Tracking (Simplified Single-Thread Version)
// Copyright (c) 2024

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

#include "rknn_api.h"
#include "rk_common.h"
#include "postprocess.h"
#include "deepsort.h"

using namespace std;
using namespace cv;

// 模型路径
string PROJECT_DIR = "/home/orangepi/Desktop/yolov8_pose/yolov8-rk3588-cpp-3-15";
string POSE_MODEL_PATH = PROJECT_DIR + "/model/yolov8_pose.rknn";
string REID_MODEL_PATH = PROJECT_DIR + "/model/osnet_x0_25_market.rknn";

// 骨架连接关系 (COCO 17 keypoints)
static const int skeleton[][2] = {
    {0, 1}, {0, 2}, {1, 3}, {2, 4},           // 头部
    {5, 6}, {5, 7}, {7, 9}, {6, 8}, {8, 10},  // 上半身
    {5, 11}, {6, 12}, {11, 12},               // 躯干
    {11, 13}, {13, 15}, {12, 14}, {14, 16}    // 下半身
};
static const int num_skeleton = sizeof(skeleton) / sizeof(skeleton[0]);

// 为每个跟踪 ID 分配颜色
Scalar get_color(int id) {
    int r = (id * 67) % 255;
    int g = (id * 113) % 255;
    int b = (id * 197) % 255;
    return Scalar(b, g, r);
}

// 绘制姿态和跟踪 ID
void draw_pose_with_tracking(Mat& img, object_detect_result_list* od_results, vector<DetectBox>& tracked_boxes)
{
    for (int i = 0; i < od_results->count; i++) {
        object_detect_result* result = &(od_results->results[i]);
        
        // 找到对应的跟踪 ID
        int track_id = -1;
        for (size_t j = 0; j < tracked_boxes.size(); j++) {
            // 通过边界框位置匹配
            if (abs(tracked_boxes[j].x1 - result->box.left) < 10 &&
                abs(tracked_boxes[j].y1 - result->box.top) < 10) {
                track_id = (int)tracked_boxes[j].trackID;
                break;
            }
        }

        Scalar color = (track_id >= 0) ? get_color(track_id) : Scalar(0, 255, 0);

        // 绘制边界框
        rectangle(img, Point(result->box.left, result->box.top),
                  Point(result->box.right, result->box.bottom), color, 2);

        // 绘制 ID
        if (track_id >= 0) {
            char id_text[32];
            sprintf(id_text, "ID:%d", track_id);
            putText(img, id_text, Point(result->box.left, result->box.top - 5),
                    FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
        }

        // 绘制关键点
        for (int k = 0; k < 17; k++) {
            float x = result->keypoints[k][0];
            float y = result->keypoints[k][1];
            float conf = result->keypoints[k][2];
            if (conf > 0.5) {
                circle(img, Point((int)x, (int)y), 3, color, -1);
            }
        }

        // 绘制骨架
        for (int k = 0; k < num_skeleton; k++) {
            int idx1 = skeleton[k][0];
            int idx2 = skeleton[k][1];
            float conf1 = result->keypoints[idx1][2];
            float conf2 = result->keypoints[idx2][2];
            if (conf1 > 0.5 && conf2 > 0.5) {
                Point pt1((int)result->keypoints[idx1][0], (int)result->keypoints[idx1][1]);
                Point pt2((int)result->keypoints[idx2][0], (int)result->keypoints[idx2][1]);
                line(img, pt1, pt2, color, 2);
            }
        }
    }
}

int main(int argc, char** argv)
{
    printf("\n=== YOLOv8 Pose + DeepSORT Tracking ===\n\n");
    
    if (argc < 2) {
        printf("Usage: %s <video_path or camera_id>\n", argv[0]);
        printf("  例如: %s 0                  # 使用摄像头 0\n", argv[0]);
        printf("  例如: %s video.mp4          # 使用视频文件\n", argv[0]);
        return -1;
    }

    // 初始化 DeepSORT 跟踪器
    printf("Loading DeepSORT ReID model: %s\n", REID_MODEL_PATH.c_str());
    DeepSort* tracker = new DeepSort(REID_MODEL_PATH, 1, 512, 6, RKNN_NPU_CORE_2);
    printf("DeepSORT initialized successfully!\n\n");

    // 打开视频源
    VideoCapture cap;
    string video_path = argv[1];
    if (video_path.length() == 1 && isdigit(video_path[0])) {
        int camera_id = atoi(argv[1]);
        cap.open(camera_id);
    } else {
        cap.open(video_path);
    }

    if (!cap.isOpened()) {
        printf("Failed to open video source: %s\n", argv[1]);
        return -1;
    }

    printf("Video opened successfully. Press 'q' to quit.\n\n");

    // 注意：这是一个简化示例，实际需要完整的 YOLOv8 Pose 推理流程
    // 这里仅演示 DeepSORT 的使用方式
    
    Mat frame;
    int frame_count = 0;

    printf("WARNING: This is a simplified demo.\n");
    printf("Full YOLOv8 Pose inference is not implemented in this version.\n");
    printf("Please use the original rknn_yolov8_demo for actual pose detection.\n\n");

    while (true) {
        if (!cap.read(frame)) {
            printf("End of video or failed to read frame\n");
            break;
        }

        frame_count++;

        // TODO: 这里需要添加完整的 YOLOv8 Pose 推理流程
        // 1. 图像预处理（letterbox resize）
        // 2. RKNN 推理
        // 3. 后处理得到 object_detect_result_list
        
        // 模拟检测结果（实际应该从 YOLOv8 Pose 推理获得）
        vector<DetectBox> detections;
        object_detect_result_list od_results;
        memset(&od_results, 0, sizeof(od_results));
        
        // DeepSORT 跟踪
        if (detections.size() > 0) {
            tracker->sort(frame, detections);
        }

        // 绘制结果
        draw_pose_with_tracking(frame, &od_results, detections);

        // 显示 FPS
        char fps_text[64];
        sprintf(fps_text, "Frame: %d", frame_count);
        putText(frame, fps_text, Point(10, 30), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 255, 0), 2);

        imshow("YOLOv8 Pose + DeepSORT (Demo)", frame);
        
        char key = waitKey(1);
        if (key == 'q' || key == 27) {
            break;
        }
    }

    printf("\nTotal frames processed: %d\n", frame_count);

    // 清理资源
    cap.release();
    destroyAllWindows();
    
    if (tracker) delete tracker;

    return 0;
}
