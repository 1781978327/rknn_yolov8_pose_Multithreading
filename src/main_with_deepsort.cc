// YOLOv8 Pose + DeepSORT 完整集成版本
// 单线程版本，确保跟踪稳定性

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <chrono>
#include <vector>
#include <string>

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "rknn_api.h"
#include "rk_common.h"
#include "postprocess.h"
#include "deepsort.h"
#include "im2d.h"
#include "rga.h"
#include "RgaUtils.h"

using namespace std;
using namespace cv;

// 骨架连接关系 (COCO 17 keypoints, 1-based index)
static const int skeleton_kps[38] = {
    16, 14, 14, 12, 17, 15, 15, 13, 12, 13, 6, 12, 7, 13, 6, 7, 6, 8,
    7, 9, 8, 10, 9, 11, 2, 3, 1, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 7
};

// 为每个跟踪 ID 分配颜色
Scalar get_track_color(int id) {
    if (id < 0) return Scalar(0, 255, 0);  // 未跟踪：绿色
    int r = (id * 67) % 255;
    int g = (id * 113) % 255;
    int b = (id * 197) % 255;
    return Scalar(b, g, r);
}

// 绘制姿态 + 跟踪 ID
void draw_pose_with_tracking(Mat& img, object_detect_result_list* od_results, vector<DetectBox>& tracked_boxes) {
    for (int i = 0; i < od_results->count; i++) {
        object_detect_result* result = &(od_results->results[i]);
        
        // 通过边界框位置匹配跟踪 ID
        int track_id = -1;
        for (size_t j = 0; j < tracked_boxes.size(); j++) {
            if (abs(tracked_boxes[j].x1 - result->box.left) < 15 &&
                abs(tracked_boxes[j].y1 - result->box.top) < 15) {
                track_id = (int)tracked_boxes[j].trackID;
                break;
            }
        }
        
        Scalar color = get_track_color(track_id);
        
        // 绘制边界框
        int x1 = result->box.left;
        int y1 = result->box.top;
        int x2 = result->box.right;
        int y2 = result->box.bottom;
        rectangle(img, Point(x1, y1), Point(x2, y2), color, 2);
        
        // 绘制 ID 和置信度
        char text[64];
        if (track_id >= 0) {
            sprintf(text, "ID:%d %.0f%%", track_id, result->prop * 100);
        } else {
            sprintf(text, "%.0f%%", result->prop * 100);
        }
        
        int baseLine = 0;
        Size label_size = getTextSize(text, FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseLine);
        int y_text = y1 - 5;
        if (y_text < label_size.height) y_text = label_size.height;
        
        // 背景
        rectangle(img, Point(x1, y_text - label_size.height - baseLine),
                  Point(x1 + label_size.width, y_text + baseLine), color, -1);
        // 文字
        putText(img, text, Point(x1, y_text), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 255, 255), 2);
        
        // 绘制 17 个关键点
        for (int j = 0; j < 17; j++) {
            float kx = result->keypoints[j][0];
            float ky = result->keypoints[j][1];
            float kconf = result->keypoints[j][2];
            if (kconf > 0.5 && kx > 0 && ky > 0) {
                circle(img, Point((int)kx, (int)ky), 4, color, -1, LINE_AA);
            }
        }
        
        // 绘制骨架
        for (int k = 0; k < (int)(sizeof(skeleton_kps) / sizeof(skeleton_kps[0]) / 2); k++) {
            int idx1 = skeleton_kps[2 * k] - 1;
            int idx2 = skeleton_kps[2 * k + 1] - 1;
            float x1k = result->keypoints[idx1][0];
            float y1k = result->keypoints[idx1][1];
            float c1 = result->keypoints[idx1][2];
            float x2k = result->keypoints[idx2][0];
            float y2k = result->keypoints[idx2][1];
            float c2 = result->keypoints[idx2][2];
            
            if (c1 > 0.5 && c2 > 0.5 && x1k > 0 && y1k > 0 && x2k > 0 && y2k > 0) {
                line(img, Point((int)x1k, (int)y1k), Point((int)x2k, (int)y2k),
                     color, 2, LINE_AA);
            }
        }
    }
}

// RGA BGR 转 RGB
int rga_bgr_to_rgb(const Mat& bgr, Mat& rgb) {
    rgb.create(bgr.rows, bgr.cols, bgr.type());
    rga_buffer_t src_img, dst_img;
    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0, sizeof(dst_img));
    
    src_img = wrapbuffer_virtualaddr((void*)bgr.data, bgr.cols, bgr.rows, RK_FORMAT_BGR_888);
    dst_img = wrapbuffer_virtualaddr((void*)rgb.data, rgb.cols, rgb.rows, RK_FORMAT_RGB_888);
    
    IM_STATUS status = imcvtcolor(src_img, dst_img, RK_FORMAT_BGR_888, RK_FORMAT_RGB_888);
    return (status == IM_STATUS_SUCCESS) ? 0 : -1;
}

// 简化版：直接使用 OpenCV resize（避免 RGA 宏冲突）
int simple_resize(const Mat& src, Mat& dst, int dst_w, int dst_h) {
    cv::resize(src, dst, Size(dst_w, dst_h), 0, 0, cv::INTER_LINEAR);
    return 0;
}

int main(int argc, char** argv) {
    printf("\n=== YOLOv8 Pose + DeepSORT 跟踪系统 ===\n\n");
    
    if (argc < 3) {
        printf("用法: %s <rknn_model> <video_source> [output_video]\n", argv[0]);
        printf("  例如: %s model/yolov8_pose.rknn 0\n", argv[0]);
        printf("  例如: %s model/yolov8_pose.rknn video.mp4 output.avi\n", argv[0]);
        return -1;
    }
    
    char* model_path = argv[1];
    string video_source = argv[2];
    string output_path = (argc >= 4) ? argv[3] : "";
    
    // ========== 1. 初始化 RKNN 模型 ==========
    printf("正在加载 YOLOv8 Pose 模型: %s\n", model_path);
    
    rknn_app_context_t app_ctx;
    memset(&app_ctx, 0, sizeof(app_ctx));
    
    int model_data_size = 0;
    unsigned char* model_data = load_model(model_path, model_data_size);
    if (!model_data) {
        printf("加载模型失败\n");
        return -1;
    }
    
    int ret = rknn_init(&app_ctx.rknn_ctx, model_data, model_data_size, 0, NULL);
    free(model_data);
    if (ret < 0) {
        printf("rknn_init 失败 ret=%d\n", ret);
        return -1;
    }
    
    // 设置 NPU Core 0
    ret = rknn_set_core_mask(app_ctx.rknn_ctx, RKNN_NPU_CORE_0);
    
    // 获取模型信息
    ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &app_ctx.io_num, sizeof(app_ctx.io_num));
    
    app_ctx.input_attrs = (rknn_tensor_attr*)malloc(app_ctx.io_num.n_input * sizeof(rknn_tensor_attr));
    app_ctx.output_attrs = (rknn_tensor_attr*)malloc(app_ctx.io_num.n_output * sizeof(rknn_tensor_attr));
    
    for (uint32_t i = 0; i < app_ctx.io_num.n_input; i++) {
        app_ctx.input_attrs[i].index = i;
        rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_INPUT_ATTR, &app_ctx.input_attrs[i], sizeof(rknn_tensor_attr));
    }
    
    for (uint32_t i = 0; i < app_ctx.io_num.n_output; i++) {
        app_ctx.output_attrs[i].index = i;
        rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &app_ctx.output_attrs[i], sizeof(rknn_tensor_attr));
    }
    
    // 获取模型输入尺寸
    if (app_ctx.input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_height = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[3];
    } else {
        app_ctx.model_height = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[3];
    }
    
    app_ctx.is_quant = (app_ctx.output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC);
    
    printf("✅ YOLOv8 Pose 模型加载成功\n");
    printf("   输入尺寸: %dx%d\n", app_ctx.model_width, app_ctx.model_height);
    printf("   量化模式: %s\n", app_ctx.is_quant ? "INT8" : "FP16");
    
    // ========== 2. 初始化 DeepSORT ==========
    printf("\n正在初始化 DeepSORT...\n");
    string reid_model = "../model/osnet_x0_25_market.rknn";
    DeepSort* tracker = new DeepSort(reid_model, 1, 512, 6, RKNN_NPU_CORE_2);
    printf("✅ DeepSORT 初始化成功 (NPU Core 2)\n");
    
    // ========== 3. 打开视频源 ==========
    VideoCapture cap;
    if (video_source == "0") {
        cap.open(0, CAP_V4L2);
    } else if (video_source.find("/dev/video") == 0) {
        cap.open(video_source, CAP_V4L2);
    } else {
        cap.open(video_source, CAP_FFMPEG);
    }
    
    if (!cap.isOpened()) {
        printf("❌ 无法打开视频源: %s\n", video_source.c_str());
        return -1;
    }
    
    int width = (int)cap.get(CAP_PROP_FRAME_WIDTH);
    int height = (int)cap.get(CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(CAP_PROP_FPS);
    if (fps <= 0 || fps > 120) fps = 25.0;
    
    printf("✅ 视频源打开成功\n");
    printf("   分辨率: %dx%d\n", width, height);
    printf("   帧率: %.1f FPS\n\n", fps);
    
    // ========== 4. 初始化输出视频 ==========
    VideoWriter writer;
    if (!output_path.empty()) {
        int fourcc = VideoWriter::fourcc('M', 'J', 'P', 'G');
        writer.open(output_path, fourcc, fps, Size(width, height));
        if (writer.isOpened()) {
            printf("✅ 输出视频: %s\n\n", output_path.c_str());
        }
    }
    
    // ========== 5. 主循环 ==========
    printf("开始处理... 按 'q' 退出\n\n");
    
    namedWindow("YOLOv8 Pose + DeepSORT", WINDOW_NORMAL);
    
    Mat frame, rgb_img, resized_img;
    int frame_count = 0;
    double total_time = 0;
    
    struct timeval start_tv, end_tv;
    
    while (true) {
        gettimeofday(&start_tv, nullptr);
        
        // 读取帧
        if (!cap.read(frame)) {
            printf("视频结束\n");
            break;
        }
        
        frame_count++;
        
        // BGR -> RGB
        if (rga_bgr_to_rgb(frame, rgb_img) != 0) {
            cvtColor(frame, rgb_img, COLOR_BGR2RGB);
        }
        
        // 缩放到模型输入尺寸
        simple_resize(rgb_img, resized_img, app_ctx.model_width, app_ctx.model_height);
        
        // 设置输入
        rknn_input inputs[1];
        memset(inputs, 0, sizeof(inputs));
        inputs[0].index = 0;
        inputs[0].type = RKNN_TENSOR_UINT8;
        inputs[0].fmt = RKNN_TENSOR_NHWC;
        inputs[0].size = app_ctx.model_width * app_ctx.model_height * app_ctx.model_channel;
        inputs[0].buf = resized_img.data;
        
        rknn_inputs_set(app_ctx.rknn_ctx, app_ctx.io_num.n_input, inputs);
        
        // 推理
        rknn_run(app_ctx.rknn_ctx, nullptr);
        
        // 获取输出
        rknn_output outputs[app_ctx.io_num.n_output];
        memset(outputs, 0, sizeof(outputs));
        for (uint32_t i = 0; i < app_ctx.io_num.n_output; i++) {
            outputs[i].index = i;
            outputs[i].want_float = (!app_ctx.is_quant);
        }
        rknn_outputs_get(app_ctx.rknn_ctx, app_ctx.io_num.n_output, outputs, NULL);
        
        // 后处理
        float scale_w = (float)app_ctx.model_width / width;
        float scale_h = (float)app_ctx.model_height / height;
        
        object_detect_result_list od_results;
        post_process(&app_ctx, outputs, BOX_THRESH, NMS_THRESH, scale_w, scale_h, &od_results);
        
        // 转换为 DeepSORT 格式
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
        if (detections.size() > 0) {
            tracker->sort(frame, detections);
        }
        
        // 绘制结果
        draw_pose_with_tracking(frame, &od_results, detections);
        
        // 释放输出
        rknn_outputs_release(app_ctx.rknn_ctx, app_ctx.io_num.n_output, outputs);
        
        gettimeofday(&end_tv, nullptr);
        double elapsed = (end_tv.tv_sec - start_tv.tv_sec) * 1000.0 + (end_tv.tv_usec - start_tv.tv_usec) / 1000.0;
        total_time += elapsed;
        
        // 显示 FPS 和统计信息
        char info_text[128];
        sprintf(info_text, "FPS: %.1f | Detected: %d | Tracked: %zu", 
                1000.0 / elapsed, od_results.count, detections.size());
        putText(frame, info_text, Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 255, 0), 2);
        
        // 显示和保存
        imshow("YOLOv8 Pose + DeepSORT", frame);
        if (writer.isOpened()) {
            writer.write(frame);
        }
        
        // 每 30 帧打印一次
        if (frame_count % 30 == 0) {
            printf("Frame %4d | FPS: %.1f | Detected: %d | Tracked: %zu\n",
                   frame_count, 1000.0 / elapsed, od_results.count, detections.size());
        }
        
        char key = waitKey(1);
        if (key == 'q' || key == 27) {
            printf("\n用户中断\n");
            break;
        }
    }
    
    // ========== 6. 统计和清理 ==========
    printf("\n=== 处理完成 ===\n");
    printf("总帧数: %d\n", frame_count);
    printf("平均 FPS: %.2f\n", frame_count / (total_time / 1000.0));
    
    cap.release();
    if (writer.isOpened()) writer.release();
    destroyAllWindows();
    
    delete tracker;
    rknn_destroy(app_ctx.rknn_ctx);
    if (app_ctx.input_attrs) free(app_ctx.input_attrs);
    if (app_ctx.output_attrs) free(app_ctx.output_attrs);
    
    printf("\n✅ 程序正常退出\n");
    return 0;
}
