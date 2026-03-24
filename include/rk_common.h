#ifndef _RK_COMMON_H_
#define _RK_COMMON_H_

#include "rknn_api.h"
#include <stdint.h>

#define OBJ_CLASS_NUM      1   // pose demo: 只输出 person
#define OBJ_NUMB_MAX_SIZE  128

typedef struct {
    int left;
    int right;
    int top;
    int bottom;
} image_rect_t;

typedef struct {
    image_rect_t box;
    // keypoints: [17][3] = x,y,conf; 坐标单位为原始图像坐标（移植 pose 时会填充）
    float keypoints[17][3];
    float prop;
    int cls_id;
} object_detect_result;

typedef struct {
    int id;
    int count;
    object_detect_result results[OBJ_NUMB_MAX_SIZE];
} object_detect_result_list;

typedef struct {
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
    int model_channel;
    int model_width;
    int model_height;
    int is_quant;
} rknn_app_context_t;

void dump_tensor_attr(rknn_tensor_attr* attr);
unsigned char* load_model(const char* filename, int& fileSize);

#endif //_RK_COMMON_H_
