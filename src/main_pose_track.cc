// YOLOv8 Pose + DeepSORT Tracking
// Copyright (c) 2024

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <thread>
#include <queue>
#include <mutex>
#include <vector>
#include <string>

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
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

// 全局变量
rknn_app_context_t app_ctx;
DeepSort* tracker = nullptr;
bool running = true;

// 初始化 RKNN 模型
int init_yolov8_pose_model(const char* model_path, rknn_app_context_t* app_ctx)
{
    int ret;
    memset(app_ctx, 0, sizeof(rknn_app_context_t));

    // 加载模型
    FILE* fp = fopen(model_path, "rb");
    if (fp == NULL) {
        printf("fopen %s fail!\n", model_path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    int model_len = ftell(fp);
    void* model = malloc(model_len);
    fseek(fp, 0, SEEK_SET);
    if (model_len != fread(model, 1, model_len, fp)) {
        printf("fread %s fail!\n", model_path);
        free(model);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    ret = rknn_init(&app_ctx->rknn_ctx, model, model_len, 0, NULL);
    free(model);
    if (ret < 0) {
        printf("rknn_init fail! ret=%d\n", ret);
        return -1;
    }

    // 获取模型输入输出信息
    rknn_input_output_num io_num;
    ret = rknn_query(app_ctx->rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret != RKNN_SUCC) {
        printf("rknn_query fail! ret=%d\n", ret);
        return -1;
    }
    printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    app_ctx->io_num = io_num;
    app_ctx->input_attrs = (rknn_tensor_attr*)malloc(io_num.n_input * sizeof(rknn_tensor_attr));
    app_ctx->output_attrs = (rknn_tensor_attr*)malloc(io_num.n_output * sizeof(rknn_tensor_attr));

    for (int i = 0; i < io_num.n_input; i++) {
        app_ctx->input_attrs[i].index = i;
        ret = rknn_query(app_ctx->rknn_ctx, RKNN_QUERY_INPUT_ATTR, &(app_ctx->input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            printf("rknn_query input attr fail! ret=%d\n", ret);
            return -1;
        }
    }

    for (int i = 0; i < io_num.n_output; i++) {
        app_ctx->output_attrs[i].index = i;
        ret = rknn_query(app_ctx->rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &(app_ctx->output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret != RKNN_SUCC) {
            printf("rknn_query output attr fail! ret=%d\n", ret);
            return -1;
        }
    }

    // 设置模型尺寸
    if (app_ctx->input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        app_ctx->model_channel = app_ctx->input_attrs[0].dims[1];
        app_ctx->model_height = app_ctx->input_attrs[0].dims[2];
        app_ctx->model_width = app_ctx->input_attrs[0].dims[3];
    } else {
        app_ctx->model_height = app_ctx->input_attrs[0].dims[1];
        app_ctx->model_width = app_ctx->input_attrs[0].dims[2];
        app_ctx->model_channel = app_ctx->input_attrs[0].dims[3];
    }
    printf("model input: width=%d, height=%d, channel=%d\n",
           app_ctx->model_width, app_ctx->model_height, app_ctx->model_channel);

    app_ctx->is_quant = (app_ctx->output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC);

    return 0;
}

// 绘制姿态和跟踪 ID
void draw_pose_with_id(Mat& img, object_detect_result_list* od_results, vector<DetectBox>& tracked_boxes)
{
    // 骨架连接关系 (COCO 17 keypoints)
    static const int skeleton[][2] = {
        {0, 1}, {0, 2}, {1, 3}, {2, 4},           // 头部
        {5, 6}, {5, 7}, {7, 9}, {6, 8}, {8, 10},  // 上半身
        {5, 11}, {6, 12}, {11, 12},               // 躯干
        {11, 13}, {13, 15}, {12, 14}, {14, 16}    // 下半身
    };
    static const int num_skeleton = sizeof(skeleton) / sizeof(skeleton[0]);

    // 为每个跟踪 ID 分配颜色
    auto get_color = [](int id) -> Scalar {
        int r = (id * 67) % 255;
        int g = (id * 113) % 255;
        int b = (id * 197) % 255;
        return Scalar(b, g, r);
    };

    for (int i = 0; i < od_results->count; i++) {
        object_detect_result* result = &(od_results->results[i]);
        
        // 找到对应的跟踪 ID
        int track_id = -1;
        for (size_t j = 0; j < tracked_boxes.size(); j++) {
            if (abs(tracked_boxes[j].x1 - result->box.left) < 5 &&
                abs(tracked_boxes[j].y1 - result->box.top) < 5) {
                track_id = tracked_boxes[j].trackID;
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
    if (argc < 2) {
        printf("Usage: %s <video_path or camera_id>\n", argv[0]);
        printf("  例如: %s 0                  # 使用摄像头 0\n", argv[0]);
        printf("  例如: %s video.mp4          # 使用视频文件\n", argv[0]);
        return -1;
    }

    // 初始化 YOLOv8 Pose 模型
    printf("Loading YOLOv8 Pose model: %s\n", POSE_MODEL_PATH.c_str());
    if (init_yolov8_pose_model(POSE_MODEL_PATH.c_str(), &app_ctx) != 0) {
        printf("Failed to init pose model\n");
        return -1;
    }

    // 初始化 DeepSORT 跟踪器
    printf("Loading DeepSORT ReID model: %s\n", REID_MODEL_PATH.c_str());
    tracker = new DeepSort(REID_MODEL_PATH, 1, 512, 6, RKNN_NPU_CORE_2);

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

    printf("Video opened successfully. Press 'q' to quit.\n");

    Mat frame;
    int frame_count = 0;
    double total_time = 0;

    while (running) {
        auto start = chrono::steady_clock::now();

        if (!cap.read(frame)) {
            printf("End of video or failed to read frame\n");
            break;
        }

        frame_count++;

        // YOLOv8 Pose 检测
        object_detect_result_list od_results;
        memset(&od_results, 0, sizeof(od_results));

        // 这里需要实现完整的推理流程
        // 简化版本：直接使用 postprocess（需要先实现 letterbox 和推理）
        
        // TODO: 完整的推理流程
        // 1. letterbox resize
        // 2. rknn_inputs_set
        // 3. rknn_run
        // 4. rknn_outputs_get
        // 5. post_process
        
        // 转换检测结果为 DeepSORT 格式
        vector<DetectBox> detections;
        for (int i = 0; i < od_results.count; i++) {
            DetectBox box;
            box.x1 = od_results.results[i].box.left;
            box.y1 = od_results.results[i].box.top;
            box.x2 = od_results.results[i].box.right;
            box.y2 = od_results.results[i].box.bottom;
            box.confidence = od_results.results[i].prop;
            box.classID = od_results.results[i].cls_id;
            detections.push_back(box);
        }

        // DeepSORT 跟踪
        tracker->sort(frame, detections);

        // 绘制结果
        draw_pose_with_id(frame, &od_results, detections);

        auto end = chrono::steady_clock::now();
        double elapsed = chrono::duration<double, milli>(end - start).count();
        total_time += elapsed;

        // 显示 FPS
        char fps_text[64];
        sprintf(fps_text, "FPS: %.1f", 1000.0 / elapsed);
        putText(frame, fps_text, Point(10, 30), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 255, 0), 2);

        imshow("YOLOv8 Pose + DeepSORT", frame);
        
        char key = waitKey(1);
        if (key == 'q' || key == 27) {
            break;
        }
    }

    printf("\n=== Statistics ===\n");
    printf("Total frames: %d\n", frame_count);
    printf("Average FPS: %.2f\n", frame_count / (total_time / 1000.0));

    // 清理资源
    cap.release();
    destroyAllWindows();
    
    if (tracker) delete tracker;
    
    if (app_ctx.rknn_ctx) {
        rknn_destroy(app_ctx.rknn_ctx);
    }
    if (app_ctx.input_attrs) free(app_ctx.input_attrs);
    if (app_ctx.output_attrs) free(app_ctx.output_attrs);

    return 0;
}
