// YOLOv8 Pose + DeepSORT 双摄像头版本
// 同时处理两个摄像头的姿态检测和跟踪

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <chrono>
#include <vector>
#include <string>
#include <thread>

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "rknn_api.h"
#include "rk_common.h"
#include "postprocess.h"
#include "deepsort.h"
#ifdef USE_RTSP_MPP
#include "rtsp_mpp_sender.h"
#endif

using namespace std;
using namespace cv;

// 无窗口模式（全局，供线程函数使用）
bool g_display_mode = true;

// 骨架连接关系
static const int skeleton_kps[38] = {
    16, 14, 14, 12, 17, 15, 15, 13, 12, 13, 6, 12, 7, 13, 6, 7, 6, 8,
    7, 9, 8, 10, 9, 11, 2, 3, 1, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 7
};

// 为每个跟踪 ID 分配颜色
Scalar get_track_color(int id) {
    if (id < 0) return Scalar(0, 255, 0);
    int r = (id * 67) % 255;
    int g = (id * 113) % 255;
    int b = (id * 197) % 255;
    return Scalar(b, g, r);
}

// 绘制姿态 + 跟踪 ID
void draw_pose_with_tracking(Mat& img, object_detect_result_list* od_results, vector<DetectBox>& tracked_boxes) {
    for (int i = 0; i < od_results->count; i++) {
        object_detect_result* result = &(od_results->results[i]);
        
        // 匹配跟踪 ID
        int track_id = -1;
        for (size_t j = 0; j < tracked_boxes.size(); j++) {
            if (abs(tracked_boxes[j].x1 - result->box.left) < 15 &&
                abs(tracked_boxes[j].y1 - result->box.top) < 15) {
                track_id = (int)tracked_boxes[j].trackID;
                break;
            }
        }
        
        Scalar color = get_track_color(track_id);
        
        // 边界框
        rectangle(img, Point(result->box.left, result->box.top),
                  Point(result->box.right, result->box.bottom), color, 2);
        
        // ID 和置信度
        char text[64];
        if (track_id >= 0) {
            sprintf(text, "ID:%d %.0f%%", track_id, result->prop * 100);
        } else {
            sprintf(text, "%.0f%%", result->prop * 100);
        }
        
        putText(img, text, Point(result->box.left, result->box.top - 5),
                FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
        
        // 关键点
        for (int j = 0; j < 17; j++) {
            float kx = result->keypoints[j][0];
            float ky = result->keypoints[j][1];
            float kconf = result->keypoints[j][2];
            if (kconf > 0.5 && kx > 0 && ky > 0) {
                circle(img, Point((int)kx, (int)ky), 3, color, -1);
            }
        }
        
        // 骨架
        for (int k = 0; k < 19; k++) {
            int idx1 = skeleton_kps[2 * k] - 1;
            int idx2 = skeleton_kps[2 * k + 1] - 1;
            float c1 = result->keypoints[idx1][2];
            float c2 = result->keypoints[idx2][2];
            
            if (c1 > 0.5 && c2 > 0.5) {
                Point pt1((int)result->keypoints[idx1][0], (int)result->keypoints[idx1][1]);
                Point pt2((int)result->keypoints[idx2][0], (int)result->keypoints[idx2][1]);
                line(img, pt1, pt2, color, 2);
            }
        }
    }
}

// 处理单个摄像头的函数
void process_camera(int cam_id, rknn_app_context_t* app_ctx, DeepSort* tracker, 
                    VideoCapture& cap, const string& window_name, bool& running) {
    Mat frame, rgb_img, resized_img;
    int frame_count = 0;
    double total_time = 0;
    
    printf("Camera %d 处理线程启动\n", cam_id);
    
    while (running) {
        auto start = chrono::steady_clock::now();
        
        if (!cap.read(frame)) {
            printf("Camera %d: 无法读取帧\n", cam_id);
            break;
        }
        
        if (frame.empty()) {
            printf("Camera %d: 帧为空\n", cam_id);
            continue;
        }
        
        frame_count++;
        
        // BGR -> RGB
        cvtColor(frame, rgb_img, COLOR_BGR2RGB);
        
        // 缩放
        resize(rgb_img, resized_img, Size(app_ctx->model_width, app_ctx->model_height));
        
        // RKNN 推理
        rknn_input inputs[1];
        memset(inputs, 0, sizeof(inputs));
        inputs[0].index = 0;
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].fmt = RKNN_TENSOR_NHWC;
        inputs[0].size = app_ctx->model_width * app_ctx->model_height * app_ctx->model_channel;
        inputs[0].buf = resized_img.data;
        
        rknn_inputs_set(app_ctx->rknn_ctx, app_ctx->io_num.n_input, inputs);
        rknn_run(app_ctx->rknn_ctx, nullptr);
        
        // 获取输出
        rknn_output outputs[app_ctx->io_num.n_output];
        memset(outputs, 0, sizeof(outputs));
        for (uint32_t i = 0; i < app_ctx->io_num.n_output; i++) {
            outputs[i].index = i;
            outputs[i].want_float = (!app_ctx->is_quant);
        }
        rknn_outputs_get(app_ctx->rknn_ctx, app_ctx->io_num.n_output, outputs, NULL);
        
        // 后处理
        float scale_w = (float)app_ctx->model_width / frame.cols;
        float scale_h = (float)app_ctx->model_height / frame.rows;
        
        object_detect_result_list od_results;
        post_process(app_ctx, outputs, BOX_THRESH, NMS_THRESH, scale_w, scale_h, &od_results);
        
        // DeepSORT 跟踪
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
        
        if (detections.size() > 0) {
            tracker->sort(frame, detections);
        }
        
        // 绘制
        draw_pose_with_tracking(frame, &od_results, detections);
        
        rknn_outputs_release(app_ctx->rknn_ctx, app_ctx->io_num.n_output, outputs);
        
        auto end = chrono::steady_clock::now();
        double elapsed = chrono::duration<double, milli>(end - start).count();
        total_time += elapsed;
        
        // 显示信息
        char info[128];
        sprintf(info, "Cam%d FPS:%.1f | Det:%d | Track:%zu", 
                cam_id, 1000.0/elapsed, od_results.count, detections.size());
        putText(frame, info, Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);
        
        if (g_display_mode) imshow(window_name, frame);
        
        if (frame_count % 30 == 0) {
            printf("Cam%d Frame %d | FPS: %.1f | Det: %d | Track: %zu\n",
                   cam_id, frame_count, 1000.0/elapsed, od_results.count, detections.size());
        }
    }
    
    printf("Camera %d 处理完成: %d 帧, 平均 FPS: %.2f\n", 
           cam_id, frame_count, frame_count / (total_time / 1000.0));
}

int main(int argc, char** argv) {
    printf("\n=== YOLOv8 Pose + DeepSORT 双摄像头跟踪系统 ===\n\n");
    
    if (argc < 4) {
        printf("用法: %s <rknn_model> <camera0> <camera1> [--rtsp0 <url>] [--rtsp1 <url>]\n", argv[0]);
        printf("  例如（双摄像头）: %s ../model/yolov8_pose.rknn 0 2\n", argv[0]);
        printf("  例如（双路推流）: %s ../model/yolov8_pose.rknn 0 2 --rtsp0 rtsp://192.168.1.100:8554/cam0 --rtsp1 rtsp://192.168.1.100:8554/cam1\n", argv[0]);
        printf("  其他参数: --no-display  无窗口模式\n");
        return -1;
    }
    
    char* model_path = argv[1];
    string cam0_source = argv[2];
    string cam1_source = argv[3];
    string rtsp_url0, rtsp_url1;

    // 无窗口模式
    g_display_mode = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-display") == 0) {
            g_display_mode = false;
        } else if (strcmp(argv[i], "--rtsp0") == 0 && i + 1 < argc) {
            rtsp_url0 = argv[++i];
        } else if (strcmp(argv[i], "--rtsp1") == 0 && i + 1 < argc) {
            rtsp_url1 = argv[++i];
        }
    }
    if (g_display_mode && getenv("DISPLAY") == nullptr) {
        printf("[INFO] DISPLAY 未设置，自动切换到无窗口模式\n");
        g_display_mode = false;
    }
    
    // 初始化两个 RKNN 上下文（每个摄像头一个）
    rknn_app_context_t app_ctx0, app_ctx1;
    memset(&app_ctx0, 0, sizeof(app_ctx0));
    memset(&app_ctx1, 0, sizeof(app_ctx1));
    
    printf("正在加载模型...\n");
    
    // 加载模型数据（共享）
    int model_data_size = 0;
    unsigned char* model_data = load_model(model_path, model_data_size);
    if (!model_data) {
        printf("加载模型失败\n");
        return -1;
    }
    
    // 初始化两个 RKNN 上下文
    for (int i = 0; i < 2; i++) {
        rknn_app_context_t* ctx = (i == 0) ? &app_ctx0 : &app_ctx1;
        
        rknn_init(&ctx->rknn_ctx, model_data, model_data_size, 0, NULL);
        rknn_set_core_mask(ctx->rknn_ctx, (i == 0) ? RKNN_NPU_CORE_0 : RKNN_NPU_CORE_1);
        
        rknn_query(ctx->rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &ctx->io_num, sizeof(ctx->io_num));
        
        ctx->input_attrs = (rknn_tensor_attr*)malloc(ctx->io_num.n_input * sizeof(rknn_tensor_attr));
        ctx->output_attrs = (rknn_tensor_attr*)malloc(ctx->io_num.n_output * sizeof(rknn_tensor_attr));
        
        for (uint32_t j = 0; j < ctx->io_num.n_input; j++) {
            ctx->input_attrs[j].index = j;
            rknn_query(ctx->rknn_ctx, RKNN_QUERY_INPUT_ATTR, &ctx->input_attrs[j], sizeof(rknn_tensor_attr));
        }
        
        for (uint32_t j = 0; j < ctx->io_num.n_output; j++) {
            ctx->output_attrs[j].index = j;
            rknn_query(ctx->rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &ctx->output_attrs[j], sizeof(rknn_tensor_attr));
        }
        
        if (ctx->input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
            ctx->model_channel = ctx->input_attrs[0].dims[1];
            ctx->model_height = ctx->input_attrs[0].dims[2];
            ctx->model_width = ctx->input_attrs[0].dims[3];
        } else {
            ctx->model_height = ctx->input_attrs[0].dims[1];
            ctx->model_width = ctx->input_attrs[0].dims[2];
            ctx->model_channel = ctx->input_attrs[0].dims[3];
        }
        
        ctx->is_quant = (ctx->output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC);
    }
    
    free(model_data);
    printf("✅ 两个 YOLOv8 Pose 模型加载成功 (NPU Core 0 & 1)\n");
    
    // 初始化两个 DeepSORT 跟踪器
    printf("\n正在初始化 DeepSORT...\n");
    DeepSort* tracker0 = new DeepSort("../model/osnet_x0_25_market.rknn", 1, 512, 6, RKNN_NPU_CORE_2);
    DeepSort* tracker1 = new DeepSort("../model/osnet_x0_25_market.rknn", 1, 512, 7, RKNN_NPU_CORE_2);
    printf("✅ 两个 DeepSORT 跟踪器初始化成功\n");
    
    // 打开摄像头
    VideoCapture cap0, cap1;
    
    printf("\n正在打开摄像头...\n");
    
    // Camera 0
    if (cam0_source == "0") {
        cap0.open(0, CAP_V4L2);
    } else if (cam0_source.find("/dev/video") == 0) {
        cap0.open(cam0_source, CAP_V4L2);
    } else {
        int cam_idx = atoi(cam0_source.c_str());
        cap0.open(cam_idx, CAP_V4L2);
    }
    
    if (!cap0.isOpened()) {
        printf("❌ 无法打开 Camera 0: %s\n", cam0_source.c_str());
        return -1;
    }
    
    // 设置分辨率
    cap0.set(CAP_PROP_FRAME_WIDTH, 640);
    cap0.set(CAP_PROP_FRAME_HEIGHT, 480);
    
    int w0 = cap0.get(CAP_PROP_FRAME_WIDTH);
    int h0 = cap0.get(CAP_PROP_FRAME_HEIGHT);
    printf("✅ Camera 0 打开成功: %s (%dx%d)\n", cam0_source.c_str(), w0, h0);
    
    // Camera 1
    if (cam1_source == "0") {
        cap1.open(0, CAP_V4L2);
    } else if (cam1_source.find("/dev/video") == 0) {
        cap1.open(cam1_source, CAP_V4L2);
    } else {
        int cam_idx = atoi(cam1_source.c_str());
        cap1.open(cam_idx, CAP_V4L2);
    }
    
    if (!cap1.isOpened()) {
        printf("❌ 无法打开 Camera 1: %s\n", cam1_source.c_str());
        cap0.release();
        return -1;
    }
    
    // 设置分辨率
    cap1.set(CAP_PROP_FRAME_WIDTH, 640);
    cap1.set(CAP_PROP_FRAME_HEIGHT, 480);
    
    int w1 = cap1.get(CAP_PROP_FRAME_WIDTH);
    int h1 = cap1.get(CAP_PROP_FRAME_HEIGHT);
    printf("✅ Camera 1 打开成功: %s (%dx%d)\n", cam1_source.c_str(), w1, h1);

    // ========== 初始化 RTSP 推流 ==========
#ifdef USE_RTSP_MPP
    RtspMppSender* rtsp_sender0 = nullptr;
    RtspMppSender* rtsp_sender1 = nullptr;
    if (!rtsp_url0.empty()) {
        rtsp_sender0 = new RtspMppSender();
        if (!rtsp_sender0->init(rtsp_url0.c_str(), w0, h0, 25)) {
            printf("❌ Camera 0 RTSP 初始化失败: %s\n", rtsp_url0.c_str());
            delete rtsp_sender0; rtsp_sender0 = nullptr;
        } else {
            printf("✅ Camera 0 RTSP 推流: %s (%dx%d)\n", rtsp_url0.c_str(), w0, h0);
        }
    }
    if (!rtsp_url1.empty()) {
        rtsp_sender1 = new RtspMppSender();
        if (!rtsp_sender1->init(rtsp_url1.c_str(), w1, h1, 25)) {
            printf("❌ Camera 1 RTSP 初始化失败: %s\n", rtsp_url1.c_str());
            delete rtsp_sender1; rtsp_sender1 = nullptr;
        } else {
            printf("✅ Camera 1 RTSP 推流: %s (%dx%d)\n", rtsp_url1.c_str(), w1, h1);
        }
    }
    if (rtsp_sender0 || rtsp_sender1) printf("\n");
#else
    if (!rtsp_url0.empty() || !rtsp_url1.empty()) {
        printf("⚠ RTSP 推流需要编译时加 -DFFMPEG_RKMPP_ROOT=<path>，当前未启用\n\n");
    }
    void* rtsp_sender0 = nullptr;
    void* rtsp_sender1 = nullptr;
#endif

    printf("\n开始处理... 按 'q' 退出\n\n");
    
    // 创建窗口
    if (g_display_mode) {
        namedWindow("Camera 0", WINDOW_NORMAL);
        namedWindow("Camera 1", WINDOW_NORMAL);
    }

    printf("使用单线程轮流处理模式\n\n");
    
    // 单线程轮流处理两个摄像头
    Mat frame0, frame1, rgb0, rgb1, resized0, resized1;
    int frame_count0 = 0, frame_count1 = 0;
    double total_time0 = 0, total_time1 = 0;
    
    while (true) {
        // ========== 处理 Camera 0 ==========
        auto start0 = chrono::steady_clock::now();
        
        if (cap0.read(frame0) && !frame0.empty()) {
            frame_count0++;
            
            cvtColor(frame0, rgb0, COLOR_BGR2RGB);
            resize(rgb0, resized0, Size(app_ctx0.model_width, app_ctx0.model_height));
            
            // RKNN 推理
            rknn_input inputs0[1];
            memset(inputs0, 0, sizeof(inputs0));
            inputs0[0].index = 0;
            inputs0[0].type = RKNN_TENSOR_UINT8;
            inputs0[0].fmt = RKNN_TENSOR_NHWC;
            inputs0[0].size = app_ctx0.model_width * app_ctx0.model_height * app_ctx0.model_channel;
            inputs0[0].buf = resized0.data;
            
            rknn_inputs_set(app_ctx0.rknn_ctx, app_ctx0.io_num.n_input, inputs0);
            rknn_run(app_ctx0.rknn_ctx, nullptr);
            
            rknn_output outputs0[app_ctx0.io_num.n_output];
            memset(outputs0, 0, sizeof(outputs0));
            for (uint32_t i = 0; i < app_ctx0.io_num.n_output; i++) {
                outputs0[i].index = i;
                outputs0[i].want_float = (!app_ctx0.is_quant);
            }
            rknn_outputs_get(app_ctx0.rknn_ctx, app_ctx0.io_num.n_output, outputs0, NULL);
            
            float scale_w0 = (float)app_ctx0.model_width / frame0.cols;
            float scale_h0 = (float)app_ctx0.model_height / frame0.rows;
            
            object_detect_result_list od_results0;
            post_process(&app_ctx0, outputs0, BOX_THRESH, NMS_THRESH, scale_w0, scale_h0, &od_results0);
            
            vector<DetectBox> detections0;
            for (int i = 0; i < od_results0.count; i++) {
                DetectBox box;
                box.x1 = od_results0.results[i].box.left;
                box.y1 = od_results0.results[i].box.top;
                box.x2 = od_results0.results[i].box.right;
                box.y2 = od_results0.results[i].box.bottom;
                box.confidence = od_results0.results[i].prop;
                box.classID = od_results0.results[i].cls_id;
                detections0.push_back(box);
            }
            
            if (detections0.size() > 0) {
                tracker0->sort(frame0, detections0);
            }
            
            draw_pose_with_tracking(frame0, &od_results0, detections0);
            rknn_outputs_release(app_ctx0.rknn_ctx, app_ctx0.io_num.n_output, outputs0);
            
            auto end0 = chrono::steady_clock::now();
            double elapsed0 = chrono::duration<double, milli>(end0 - start0).count();
            total_time0 += elapsed0;
            
            char info0[128];
            sprintf(info0, "Cam0 FPS:%.1f | Det:%d | Track:%zu",
                    1000.0/elapsed0, od_results0.count, detections0.size());
            putText(frame0, info0, Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);

            if (g_display_mode) imshow("Camera 0", frame0);
#ifdef USE_RTSP_MPP
            if (rtsp_sender0) rtsp_sender0->push(frame0);
#endif

            if (frame_count0 % 30 == 0) {
                printf("Cam0 Frame %d | FPS: %.1f | Det: %d | Track: %zu\n",
                       frame_count0, 1000.0/elapsed0, od_results0.count, detections0.size());
            }
        }
        
        // ========== 处理 Camera 1 ==========
        auto start1 = chrono::steady_clock::now();
        
        if (cap1.read(frame1) && !frame1.empty()) {
            frame_count1++;
            
            cvtColor(frame1, rgb1, COLOR_BGR2RGB);
            resize(rgb1, resized1, Size(app_ctx1.model_width, app_ctx1.model_height));
            
            rknn_input inputs1[1];
            memset(inputs1, 0, sizeof(inputs1));
            inputs1[0].index = 0;
            inputs1[0].type = RKNN_TENSOR_UINT8;
            inputs1[0].fmt = RKNN_TENSOR_NHWC;
            inputs1[0].size = app_ctx1.model_width * app_ctx1.model_height * app_ctx1.model_channel;
            inputs1[0].buf = resized1.data;
            
            rknn_inputs_set(app_ctx1.rknn_ctx, app_ctx1.io_num.n_input, inputs1);
            rknn_run(app_ctx1.rknn_ctx, nullptr);
            
            rknn_output outputs1[app_ctx1.io_num.n_output];
            memset(outputs1, 0, sizeof(outputs1));
            for (uint32_t i = 0; i < app_ctx1.io_num.n_output; i++) {
                outputs1[i].index = i;
                outputs1[i].want_float = (!app_ctx1.is_quant);
            }
            rknn_outputs_get(app_ctx1.rknn_ctx, app_ctx1.io_num.n_output, outputs1, NULL);
            
            float scale_w1 = (float)app_ctx1.model_width / frame1.cols;
            float scale_h1 = (float)app_ctx1.model_height / frame1.rows;
            
            object_detect_result_list od_results1;
            post_process(&app_ctx1, outputs1, BOX_THRESH, NMS_THRESH, scale_w1, scale_h1, &od_results1);
            
            vector<DetectBox> detections1;
            for (int i = 0; i < od_results1.count; i++) {
                DetectBox box;
                box.x1 = od_results1.results[i].box.left;
                box.y1 = od_results1.results[i].box.top;
                box.x2 = od_results1.results[i].box.right;
                box.y2 = od_results1.results[i].box.bottom;
                box.confidence = od_results1.results[i].prop;
                box.classID = od_results1.results[i].cls_id;
                detections1.push_back(box);
            }
            
            if (detections1.size() > 0) {
                tracker1->sort(frame1, detections1);
            }
            
            draw_pose_with_tracking(frame1, &od_results1, detections1);
            rknn_outputs_release(app_ctx1.rknn_ctx, app_ctx1.io_num.n_output, outputs1);
            
            auto end1 = chrono::steady_clock::now();
            double elapsed1 = chrono::duration<double, milli>(end1 - start1).count();
            total_time1 += elapsed1;
            
            char info1[128];
            sprintf(info1, "Cam1 FPS:%.1f | Det:%d | Track:%zu",
                    1000.0/elapsed1, od_results1.count, detections1.size());
            putText(frame1, info1, Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);

            if (g_display_mode) imshow("Camera 1", frame1);
#ifdef USE_RTSP_MPP
            if (rtsp_sender1) rtsp_sender1->push(frame1);
#endif

            if (frame_count1 % 30 == 0) {
                printf("Cam1 Frame %d | FPS: %.1f | Det: %d | Track: %zu\n",
                       frame_count1, 1000.0/elapsed1, od_results1.count, detections1.size());
            }
        }
        
        // 按键检测
        if (g_display_mode) {
            char key = waitKey(1);
            if (key == 'q' || key == 27) {
                break;
            }
        }
    }
    
    printf("\n=== 处理完成 ===\n");
    printf("Camera 0: %d 帧, 平均 FPS: %.2f\n", frame_count0, frame_count0 / (total_time0 / 1000.0));
    printf("Camera 1: %d 帧, 平均 FPS: %.2f\n", frame_count1, frame_count1 / (total_time1 / 1000.0));
    
    // 清理
    cap0.release();
    cap1.release();
    if (g_display_mode) destroyAllWindows();
#ifdef USE_RTSP_MPP
    if (rtsp_sender0) { rtsp_sender0->destroy(); delete rtsp_sender0; rtsp_sender0 = nullptr; }
    if (rtsp_sender1) { rtsp_sender1->destroy(); delete rtsp_sender1; rtsp_sender1 = nullptr; }
#endif
    
    delete tracker0;
    delete tracker1;
    
    rknn_destroy(app_ctx0.rknn_ctx);
    rknn_destroy(app_ctx1.rknn_ctx);
    free(app_ctx0.input_attrs);
    free(app_ctx0.output_attrs);
    free(app_ctx1.input_attrs);
    free(app_ctx1.output_attrs);
    
    printf("\n✅ 程序正常退出\n");
    return 0;
}
