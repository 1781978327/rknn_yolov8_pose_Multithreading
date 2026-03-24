#ifndef _RKNNPOOL_HPP
#define _RKNNPOOL_HPP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <chrono>
#include "opencv2/opencv.hpp"
#include "opencv2/highgui.hpp"
#include "postprocess.h"
#include "rk_common.h"
#include "rknn_api.h"
#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"

// pose demo: 只有 person
static const char* coco_labels[] = {"person"};

// yolov8_pose skeleton (keypoint id: 1~17, 0 is unused)
// 跟 pose demo main.cc 的 skeleton 一致：每行两个点编号(1-based)
static const int skeleton_kps[38] = {
    16, 14, 14, 12, 17, 15, 15, 13, 12, 13, 6, 12, 7, 13, 6, 7, 6, 8,
    7, 9, 8, 10, 9, 11, 2, 3, 1, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 7
};

class rknn_lite {
private:
    rknn_app_context_t app_ctx;  // RKNN应用上下文
    int ret;  // 函数返回值

public:
    cv::Mat ori_img;  // 原始图像

    rknn_lite(char* model_path, int core_id);
    ~rknn_lite();
    int interf();
    int RGA_bgr_to_rgb(const cv::Mat& bgr_image, cv::Mat &rgb_image);
    int RGA_resize(const cv::Mat& src, cv::Mat& dst, int dst_width, int dst_height);  // 新增RGA缩放函数
};

// 构造函数：初始化RKNN模型
rknn_lite::rknn_lite(char* model_path, int core_id) {
    memset(&app_ctx, 0, sizeof(rknn_app_context_t));

    // 加载模型文件
    int model_data_size = 0;
    unsigned char* model_data = load_model(model_path, model_data_size);
    
    // 初始化RKNN上下文
    ret = rknn_init(&app_ctx.rknn_ctx, model_data, model_data_size, 0, NULL);
    free(model_data);
    if (ret < 0) {
        printf("rknn_init 错误 ret=%d\n", ret);
        exit(-1);
    }

    // 设置NPU核心掩码
    rknn_core_mask core_mask;
    switch(core_id % 3) {
        case 0: core_mask = RKNN_NPU_CORE_0; break;
        case 1: core_mask = RKNN_NPU_CORE_1; break;
        default: core_mask = RKNN_NPU_CORE_2;
    }
    ret = rknn_set_core_mask(app_ctx.rknn_ctx, core_mask);
    if (ret < 0) {
        printf("rknn_set_core_mask 错误 ret=%d\n", ret);
        exit(-1);
    }

    // 获取模型输入输出信息
    ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &app_ctx.io_num, sizeof(app_ctx.io_num));
    if (ret < 0) {
        printf("rknn_query io_num 错误 ret=%d\n", ret);
        exit(-1);
    }

    // 获取输入属性
    app_ctx.input_attrs = new rknn_tensor_attr[app_ctx.io_num.n_input];
    memset(app_ctx.input_attrs, 0, sizeof(rknn_tensor_attr) * app_ctx.io_num.n_input);
    for (uint32_t i = 0; i < app_ctx.io_num.n_input; i++) {
        app_ctx.input_attrs[i].index = i;
        ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_INPUT_ATTR, &(app_ctx.input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query input_attrs 错误 ret=%d\n", ret);
            exit(-1);
        }
    }

    // 获取输出属性
    app_ctx.output_attrs = new rknn_tensor_attr[app_ctx.io_num.n_output];
    memset(app_ctx.output_attrs, 0, sizeof(rknn_tensor_attr) * app_ctx.io_num.n_output);
    for (uint32_t i = 0; i < app_ctx.io_num.n_output; i++) {
        app_ctx.output_attrs[i].index = i;
        ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &(app_ctx.output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query output_attrs 错误 ret=%d\n", ret);
            exit(-1);
        }
    }

    // 检查量化类型
    if (app_ctx.output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC && 
        app_ctx.output_attrs[0].type != RKNN_TENSOR_FLOAT16) {
        app_ctx.is_quant = true;
    } else {
        app_ctx.is_quant = false;
    }

    // 设置模型维度
    if (app_ctx.input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_height = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[3];
    } else {
        app_ctx.model_height = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[3];
    }
}

// 析构函数：释放资源
rknn_lite::~rknn_lite() {
    if (app_ctx.rknn_ctx != 0) {
        rknn_destroy(app_ctx.rknn_ctx);
    }
    if (app_ctx.input_attrs != nullptr) {
        delete[] app_ctx.input_attrs;
    }
    if (app_ctx.output_attrs != nullptr) {
        delete[] app_ctx.output_attrs;
    }
}

// BGR转RGB函数（使用RGA加速）
int rknn_lite::RGA_bgr_to_rgb(const cv::Mat& bgr_image, cv::Mat &rgb_image) {
    // 创建输出图像
    rgb_image.create(bgr_image.size(), bgr_image.type());
    
    rga_buffer_t src_img, dst_img;
    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0, sizeof(dst_img));

    // 设置输入输出参数
    int src_width = bgr_image.cols;
    int src_height = bgr_image.rows;
    int dst_width = rgb_image.cols;
    int dst_height = rgb_image.rows;

    // 设置图像格式
    int src_format = RK_FORMAT_BGR_888;
    int dst_format = RK_FORMAT_RGB_888;

    // 包装图像数据到RGA缓冲区
    src_img = wrapbuffer_virtualaddr((void *)bgr_image.data, src_width, src_height, src_format);
    dst_img = wrapbuffer_virtualaddr((void *)rgb_image.data, dst_width, dst_height, dst_format);

    // 执行颜色空间转换
    IM_STATUS status = imcvtcolor(src_img, dst_img, src_format, dst_format);
    if (status != IM_STATUS_SUCCESS) {
        fprintf(stderr, "RGA BGR转RGB错误: %s\n", imStrError(status));
        return -1;
    }
    
    return 0;
}

// 图像缩放函数（使用RGA加速）
int rknn_lite::RGA_resize(const cv::Mat& src, cv::Mat& dst, int dst_width, int dst_height) {
    // 创建目标图像
    dst.create(dst_height, dst_width, src.type());
    
    rga_buffer_t src_img, dst_img;
    im_rect src_rect, dst_rect;
    
    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0, sizeof(dst_img));
    memset(&src_rect, 0, sizeof(src_rect));
    memset(&dst_rect, 0, sizeof(dst_rect));

    // 设置图像格式
    int format = RK_FORMAT_RGB_888;  

    // 包装图像数据
    src_img = wrapbuffer_virtualaddr((void*)src.data, src.cols, src.rows, format);
    dst_img = wrapbuffer_virtualaddr((void*)dst.data, dst.cols, dst.rows, format);

    // 执行缩放操作
    IM_STATUS status = imresize(src_img, dst_img);
    if (status != IM_STATUS_SUCCESS) {
        fprintf(stderr, "RGA缩放错误: %s\n", imStrError(status));
        return -1;
    }
    
    return 0;
}

// 推理接口函数
int rknn_lite::interf() {
    cv::Mat img;
    // 使用RGA进行BGR到RGB转换
    if (RGA_bgr_to_rgb(ori_img, img) != 0) {
        printf("RGA BGR转RGB失败，回退到OpenCV\n");
        return -1;
    }
    
    int img_width = img.cols;
    int img_height = img.rows;
    
    // 准备输入张量
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].size = app_ctx.model_width * app_ctx.model_height * app_ctx.model_channel;
    
    // 使用RGA进行图像缩放
    cv::Mat resized_img;
    void* buf = nullptr;
    if (img_width != app_ctx.model_width || img_height != app_ctx.model_height) {
        if (RGA_resize(img, resized_img, app_ctx.model_width, app_ctx.model_height) != 0) {
            printf("RGA缩放失败，回退到OpenCV\n");
            cv::resize(img, resized_img, cv::Size(app_ctx.model_width, app_ctx.model_height));
        }
        buf = (void*)resized_img.data;
    } else {
        buf = (void*)img.data;
    }
    inputs[0].buf = buf;
    
    // 设置输入
    ret = rknn_inputs_set(app_ctx.rknn_ctx, app_ctx.io_num.n_input, inputs);
    if (ret < 0) {
        printf("rknn_inputs_set 错误 ret=%d\n", ret);
        return -1;
    }
    
    // 准备输出
    rknn_output outputs[app_ctx.io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (uint32_t i = 0; i < app_ctx.io_num.n_output; i++) {
        outputs[i].index = i;
        outputs[i].want_float = (!app_ctx.is_quant);
    }
    
    // 执行推理
    ret = rknn_run(app_ctx.rknn_ctx, nullptr);
    ret = rknn_outputs_get(app_ctx.rknn_ctx, app_ctx.io_num.n_output, outputs, NULL);
    
    // 后处理
    float scale_w = (float)app_ctx.model_width / img_width;
    float scale_h = (float)app_ctx.model_height / img_height;
    
    object_detect_result_list od_results;
    post_process(&app_ctx, outputs, BOX_THRESH, NMS_THRESH, scale_w, scale_h, &od_results);
    
    // 绘制检测结果（仅显示人体框 + 骨架，不做告警上报）
    char text[256];
    for (int i = 0; i < od_results.count; i++) {
        object_detect_result* det_result = &(od_results.results[i]);
        
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;
        cv::rectangle(ori_img, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0));
        
        sprintf(text, "%s %.1f%%", coco_labels[det_result->cls_id], det_result->prop * 100);
        
        int baseLine = 0;
        cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
        
        int x = x1;
        int y = y1 - label_size.height - baseLine;
        if (y < 0) y = 0;
        if (x + label_size.width > ori_img.cols) x = ori_img.cols - label_size.width;
        
        cv::rectangle(ori_img, cv::Rect(cv::Point(x, y), 
                      cv::Size(label_size.width, label_size.height + baseLine)), 
                      cv::Scalar(255, 255, 255), -1);
        
        cv::putText(ori_img, text, cv::Point(x, y + label_size.height),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));

        // 画 17 个关键点（pose demo：0 坐标表示无效点）
        for (int j = 0; j < 17; ++j) {
            float kx = det_result->keypoints[j][0];
            float ky = det_result->keypoints[j][1];
            if (kx == 0.0f && ky == 0.0f) continue;
            cv::circle(ori_img, cv::Point((int)kx, (int)ky), 3, cv::Scalar(0, 255, 255), -1, cv::LINE_AA);
        }

        // 画骨架线
        for (int k = 0; k < (int)(sizeof(skeleton_kps) / sizeof(skeleton_kps[0]) / 2); ++k) {
            int idx1 = skeleton_kps[2 * k] - 1;
            int idx2 = skeleton_kps[2 * k + 1] - 1;
            float x1k = det_result->keypoints[idx1][0];
            float y1k = det_result->keypoints[idx1][1];
            float x2k = det_result->keypoints[idx2][0];
            float y2k = det_result->keypoints[idx2][1];
            if ((x1k == 0.0f && y1k == 0.0f) || (x2k == 0.0f && y2k == 0.0f)) continue;
            cv::line(ori_img, cv::Point((int)x1k, (int)y1k), cv::Point((int)x2k, (int)y2k),
                     cv::Scalar(0, 165, 255), 2, cv::LINE_AA);
        }
    }
    
    // 释放输出
    ret = rknn_outputs_release(app_ctx.rknn_ctx, app_ctx.io_num.n_output, outputs);
    return 0;
}

#endif // _RKNNPOOL_HPP