// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Modified by Q-engineering 4-6-2026
//

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <set>
#include <vector>
#include "rk_common.h"

// 注意：此工程的交叉编译环境不一定自带 Float16.h，
// 这里用自定义 half(FP16) -> float 转换，避免外部头文件依赖。

static float half_to_float(uint16_t h) {
    uint16_t sign = (h >> 15) & 0x0001;
    uint16_t exp = (h >> 10) & 0x001f;
    uint16_t frac = h & 0x03ff;

    uint32_t f_sign = (uint32_t)sign << 31;
    uint32_t f_exp;
    uint32_t f_frac;

    if (exp == 0) {
        if (frac == 0) {
            f_exp = 0;
            f_frac = 0;
        } else {
            // subnormal: normalize
            exp = 1;
            while ((frac & 0x0400) == 0) {
                frac <<= 1;
                exp--;
            }
            frac &= 0x03ff;
            f_exp = (uint32_t)(exp + (127 - 15));
            f_frac = (uint32_t)frac << 13;
        }
    } else if (exp == 31) {
        // inf/nan
        f_exp = 255;
        f_frac = (uint32_t)frac << 13;
    } else {
        f_exp = (uint32_t)(exp + (127 - 15));
        f_frac = (uint32_t)frac << 13;
    }

    uint32_t f = f_sign | (f_exp << 23) | f_frac;
    float out;
    memcpy(&out, &f, sizeof(out));
    return out;
}

static inline float clamp(float val, float min_val, float max_val) {
    return val <= min_val ? min_val : (val >= max_val ? max_val : val);
}

static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0, float xmin1, float ymin1, float xmax1,
                              float ymax1)
{
    float w = fmax(0.f, fmin(xmax0, xmax1) - fmax(xmin0, xmin1) + 1.0);
    float h = fmax(0.f, fmin(ymax0, ymax1) - fmax(ymin0, ymin1) + 1.0);
    float i = w * h;
    float u = (xmax0 - xmin0 + 1.0) * (ymax0 - ymin0 + 1.0) + (xmax1 - xmin1 + 1.0) * (ymax1 - ymin1 + 1.0) - i;
    return u <= 0.f ? 0.f : (i / u);
}

static int nms(int validCount, std::vector<float> &outputLocations, std::vector<int> classIds, std::vector<int> &order,
               int filterId, float threshold)
{
    for (int i = 0; i < validCount; ++i)
    {
        if (order[i] == -1 || classIds[i] != filterId)
        {
            continue;
        }
        int n = order[i];
        for (int j = i + 1; j < validCount; ++j)
        {
            int m = order[j];
            if (m == -1 || classIds[i] != filterId)
            {
                continue;
            }
            // pose filterBoxes layout: [x, y, w, h, keypoints_index]
            float xmin0 = outputLocations[n * 5 + 0];
            float ymin0 = outputLocations[n * 5 + 1];
            float xmax0 = outputLocations[n * 5 + 0] + outputLocations[n * 5 + 2];
            float ymax0 = outputLocations[n * 5 + 1] + outputLocations[n * 5 + 3];

            float xmin1 = outputLocations[m * 5 + 0];
            float ymin1 = outputLocations[m * 5 + 1];
            float xmax1 = outputLocations[m * 5 + 0] + outputLocations[m * 5 + 2];
            float ymax1 = outputLocations[m * 5 + 1] + outputLocations[m * 5 + 3];

            float iou = CalculateOverlap(xmin0, ymin0, xmax0, ymax0, xmin1, ymin1, xmax1, ymax1);

            if (iou > threshold)
            {
                order[j] = -1;
            }
        }
    }
    return 0;
}

static int quick_sort_indice_inverse(std::vector<float> &input, int left, int right, std::vector<int> &indices)
{
    float key;
    int key_index;
    int low = left;
    int high = right;
    if (left < right)
    {
        key_index = indices[left];
        key = input[left];
        while (low < high)
        {
            while (low < high && input[high] <= key)
            {
                high--;
            }
            input[low] = input[high];
            indices[low] = indices[high];
            while (low < high && input[low] >= key)
            {
                low++;
            }
            input[high] = input[low];
            indices[high] = indices[low];
        }
        input[low] = key;
        indices[low] = key_index;
        quick_sort_indice_inverse(input, left, low - 1, indices);
        quick_sort_indice_inverse(input, low + 1, right, indices);
    }
    return low;
}

inline static int32_t __clip(float val, float min, float max)
{
    float f = val <= min ? min : (val >= max ? max : val);
    return f;
}

static int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale)
{
    float dst_val = (f32 / scale) + zp;
    int8_t res = (int8_t)__clip(dst_val, -128, 127);
    return res;
}

static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale) { return ((float)qnt - (float)zp) * scale; }

static void compute_dfl(float* tensor, int dfl_len, float* box){
    for (int b=0; b<4; b++){
        float exp_t[dfl_len];
        float exp_sum=0;
        float acc_sum=0;
        for (int i=0; i< dfl_len; i++){
            exp_t[i] = exp(tensor[i+b*dfl_len]);
            exp_sum += exp_t[i];
        }

        for (int i=0; i< dfl_len; i++){
            acc_sum += exp_t[i]/exp_sum *i;
        }
        box[b] = acc_sum;
    }
}

static int process_i8(int8_t *box_tensor, int32_t box_zp, float box_scale,
                      int8_t *score_tensor, int32_t score_zp, float score_scale,
                      int8_t *score_sum_tensor, int32_t score_sum_zp, float score_sum_scale,
                      int grid_h, int grid_w, int stride, int dfl_len,
                      std::vector<float> &boxes,
                      std::vector<float> &objProbs,
                      std::vector<int> &classId,
                      float threshold)
{
    int validCount = 0;
    int grid_len = grid_h * grid_w;
    int8_t score_thres_i8 = qnt_f32_to_affine(threshold, score_zp, score_scale);
    int8_t score_sum_thres_i8 = qnt_f32_to_affine(threshold, score_sum_zp, score_sum_scale);

    for (int i = 0; i < grid_h; i++)
    {
        for (int j = 0; j < grid_w; j++)
        {
            int offset = i* grid_w + j;
            int max_class_id = -1;

            // 通过 score sum 起到快速过滤的作用
            if (score_sum_tensor != nullptr){
                if (score_sum_tensor[offset] < score_sum_thres_i8){
                    continue;
                }
            }

            int8_t max_score = -score_zp;
            for (int c= 0; c< OBJ_CLASS_NUM; c++){
                if ((score_tensor[offset] > score_thres_i8) && (score_tensor[offset] > max_score))
                {
                    max_score = score_tensor[offset];
                    max_class_id = c;
                }
                offset += grid_len;
            }

            // compute box
            if (max_score> score_thres_i8){
                offset = i* grid_w + j;
                float box[4];
                float before_dfl[dfl_len*4];
                for (int k=0; k< dfl_len*4; k++){
                    before_dfl[k] = deqnt_affine_to_f32(box_tensor[offset], box_zp, box_scale);
                    offset += grid_len;
                }
                compute_dfl(before_dfl, dfl_len, box);

                float x1,y1,x2,y2,w,h;
                x1 = (-box[0] + j + 0.5)*stride;
                y1 = (-box[1] + i + 0.5)*stride;
                x2 = (box[2] + j + 0.5)*stride;
                y2 = (box[3] + i + 0.5)*stride;
                w = x2 - x1;
                h = y2 - y1;
                boxes.push_back(x1);
                boxes.push_back(y1);
                boxes.push_back(w);
                boxes.push_back(h);

                objProbs.push_back(deqnt_affine_to_f32(max_score, score_zp, score_scale));
                classId.push_back(max_class_id);
                validCount ++;
            }
        }
    }
    return validCount;
}

static int process_fp32(float *box_tensor, float *score_tensor, float *score_sum_tensor,
                        int grid_h, int grid_w, int stride, int dfl_len,
                        std::vector<float> &boxes,
                        std::vector<float> &objProbs,
                        std::vector<int> &classId,
                        float threshold)
{
    int validCount = 0;
    int grid_len = grid_h * grid_w;
    for (int i = 0; i < grid_h; i++)
    {
        for (int j = 0; j < grid_w; j++)
        {
            int offset = i* grid_w + j;
            int max_class_id = -1;

            // 通过 score sum 起到快速过滤的作用
            if (score_sum_tensor != nullptr){
                if (score_sum_tensor[offset] < threshold){
                    continue;
                }
            }

            float max_score = 0;
            for (int c= 0; c< OBJ_CLASS_NUM; c++){
                if ((score_tensor[offset] > threshold) && (score_tensor[offset] > max_score))
                {
                    max_score = score_tensor[offset];
                    max_class_id = c;
                }
                offset += grid_len;
            }

            // compute box
            if (max_score> threshold){
                offset = i* grid_w + j;
                float box[4];
                float before_dfl[dfl_len*4];
                for (int k=0; k< dfl_len*4; k++){
                    before_dfl[k] = box_tensor[offset];
                    offset += grid_len;
                }
                compute_dfl(before_dfl, dfl_len, box);

                float x1,y1,x2,y2,w,h;
                x1 = (-box[0] + j + 0.5)*stride;
                y1 = (-box[1] + i + 0.5)*stride;
                x2 = (box[2] + j + 0.5)*stride;
                y2 = (box[3] + i + 0.5)*stride;
                w = x2 - x1;
                h = y2 - y1;
                boxes.push_back(x1);
                boxes.push_back(y1);
                boxes.push_back(w);
                boxes.push_back(h);

                objProbs.push_back(max_score);
                classId.push_back(max_class_id);
                validCount ++;
            }
        }
    }
    return validCount;
}

// ---------------- Pose decode helpers (yolov8n-pose) ----------------
static float pose_sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }
static float pose_unsigmoid(float y) { return -1.0f * logf((1.0f / y) - 1.0f); }
static void pose_softmax(float *input, int size) {
    float max_val = input[0];
    for (int i = 1; i < size; ++i) {
        if (input[i] > max_val) max_val = input[i];
    }
    float sum_exp = 0.0f;
    for (int i = 0; i < size; ++i) sum_exp += expf(input[i] - max_val);
    for (int i = 0; i < size; ++i) input[i] = expf(input[i] - max_val) / sum_exp;
}

// filterBoxes layout per candidate: [x, y, w, h, keypoints_index]
static int pose_process_i8(int8_t *input, int grid_h, int grid_w, int stride,
                            std::vector<float> &boxes, std::vector<float> &boxScores, std::vector<int> &classId,
                            float threshold, int32_t zp, float scale, int index) {
    const int input_loc_len = 64;
    const int validClassNum = OBJ_CLASS_NUM;
    int validCount = 0;

    int8_t thres_i8 = qnt_f32_to_affine(pose_unsigmoid(threshold), zp, scale);

    for (int h = 0; h < grid_h; h++) {
        for (int w = 0; w < grid_w; w++) {
            for (int a = 0; a < validClassNum; a++) {
                int8_t raw_score = input[(input_loc_len + a) * grid_w * grid_h + h * grid_w + w];
                if (raw_score >= thres_i8) {
                    float box_conf_f32 = pose_sigmoid(deqnt_affine_to_f32(raw_score, zp, scale));

                    float loc[input_loc_len];
                    for (int i = 0; i < input_loc_len; ++i) {
                        loc[i] = deqnt_affine_to_f32(input[i * grid_w * grid_h + h * grid_w + w], zp, scale);
                    }
                    for (int i = 0; i < input_loc_len / 16; ++i) {
                        pose_softmax(&loc[i * 16], 16);
                    }

                    float xywh_[4] = {0, 0, 0, 0};
                    float xywh[4] = {0, 0, 0, 0};
                    for (int dfl = 0; dfl < 16; ++dfl) {
                        xywh_[0] += loc[dfl] * dfl;
                        xywh_[1] += loc[1 * 16 + dfl] * dfl;
                        xywh_[2] += loc[2 * 16 + dfl] * dfl;
                        xywh_[3] += loc[3 * 16 + dfl] * dfl;
                    }

                    xywh_[0] = (w + 0.5f) - xywh_[0];
                    xywh_[1] = (h + 0.5f) - xywh_[1];
                    xywh_[2] = (w + 0.5f) + xywh_[2];
                    xywh_[3] = (h + 0.5f) + xywh_[3];

                    xywh[0] = ((xywh_[0] + xywh_[2]) / 2.0f) * stride;
                    xywh[1] = ((xywh_[1] + xywh_[3]) / 2.0f) * stride;
                    xywh[2] = (xywh_[2] - xywh_[0]) * stride;
                    xywh[3] = (xywh_[3] - xywh_[1]) * stride;

                    xywh[0] = xywh[0] - xywh[2] / 2.0f;
                    xywh[1] = xywh[1] - xywh[3] / 2.0f;

                    boxes.push_back(xywh[0]);  // x
                    boxes.push_back(xywh[1]);  // y
                    boxes.push_back(xywh[2]);  // w
                    boxes.push_back(xywh[3]);  // h
                    boxes.push_back(float(index + (h * grid_w) + w)); // keypoints index

                    boxScores.push_back(box_conf_f32);
                    classId.push_back(a);
                    validCount++;
                }
            }
        }
    }
    return validCount;
}

static int pose_process_fp32(float *input, int grid_h, int grid_w, int stride,
                               std::vector<float> &boxes, std::vector<float> &boxScores, std::vector<int> &classId,
                               float threshold, int32_t zp, float scale, int index) {
    const int input_loc_len = 64;
    const int validClassNum = OBJ_CLASS_NUM;
    int validCount = 0;

    float thres_fp = pose_unsigmoid(threshold);

    for (int h = 0; h < grid_h; h++) {
        for (int w = 0; w < grid_w; w++) {
            for (int a = 0; a < validClassNum; a++) {
                float raw_score = input[(input_loc_len + a) * grid_w * grid_h + h * grid_w + w];
                if (raw_score >= thres_fp) {
                    float box_conf_f32 = pose_sigmoid(raw_score);

                    float loc[input_loc_len];
                    for (int i = 0; i < input_loc_len; ++i) {
                        loc[i] = input[i * grid_w * grid_h + h * grid_w + w];
                    }
                    for (int i = 0; i < input_loc_len / 16; ++i) {
                        pose_softmax(&loc[i * 16], 16);
                    }

                    float xywh_[4] = {0, 0, 0, 0};
                    float xywh[4] = {0, 0, 0, 0};
                    for (int dfl = 0; dfl < 16; ++dfl) {
                        xywh_[0] += loc[dfl] * dfl;
                        xywh_[1] += loc[1 * 16 + dfl] * dfl;
                        xywh_[2] += loc[2 * 16 + dfl] * dfl;
                        xywh_[3] += loc[3 * 16 + dfl] * dfl;
                    }

                    xywh_[0] = (w + 0.5f) - xywh_[0];
                    xywh_[1] = (h + 0.5f) - xywh_[1];
                    xywh_[2] = (w + 0.5f) + xywh_[2];
                    xywh_[3] = (h + 0.5f) + xywh_[3];

                    xywh[0] = ((xywh_[0] + xywh_[2]) / 2.0f) * stride;
                    xywh[1] = ((xywh_[1] + xywh_[3]) / 2.0f) * stride;
                    xywh[2] = (xywh_[2] - xywh_[0]) * stride;
                    xywh[3] = (xywh_[3] - xywh_[1]) * stride;

                    xywh[0] = xywh[0] - xywh[2] / 2.0f;
                    xywh[1] = xywh[1] - xywh[3] / 2.0f;

                    boxes.push_back(xywh[0]);
                    boxes.push_back(xywh[1]);
                    boxes.push_back(xywh[2]);
                    boxes.push_back(xywh[3]);
                    boxes.push_back(float(index + (h * grid_w) + w));

                    boxScores.push_back(box_conf_f32);
                    classId.push_back(a);
                    validCount++;
                }
            }
        }
    }
    return validCount;
}

int post_process(rknn_app_context_t *app_ctx, void *outputs, float conf_threshold, float nms_threshold, float scale_w, float scale_h, object_detect_result_list *od_results)
{
    // Pose model (yolov8n-pose) decoding:
    // output[0..2] box/score, output[3] keypoints (17x3x8400)
    rknn_output *_outputs = (rknn_output *)outputs;

    std::vector<float> filterBoxes; // layout: [x, y, w, h, keypoints_index]
    std::vector<float> objProbs;
    std::vector<int> classId;

    int validCount = 0;
    int grid_h = 0;
    int grid_w = 0;
    int model_in_w = app_ctx->model_width;
    int model_in_h = app_ctx->model_height;

    memset(od_results, 0, sizeof(object_detect_result_list));

    // first 3 branches
    int index = 0;
    for (int i = 0; i < 3; i++)
    {
        grid_h = app_ctx->output_attrs[i].dims[2];
        grid_w = app_ctx->output_attrs[i].dims[3];
        int stride = model_in_h / grid_h;

        if (app_ctx->is_quant)
        {
            validCount += pose_process_i8((int8_t *)_outputs[i].buf, grid_h, grid_w, stride,
                                           filterBoxes, objProbs, classId,
                                           conf_threshold,
                                           app_ctx->output_attrs[i].zp, app_ctx->output_attrs[i].scale,
                                           index);
        }
        else
        {
            validCount += pose_process_fp32((float *)_outputs[i].buf, grid_h, grid_w, stride,
                                              filterBoxes, objProbs, classId,
                                              conf_threshold,
                                              app_ctx->output_attrs[i].zp, app_ctx->output_attrs[i].scale,
                                              index);
        }
        index += grid_h * grid_w;
    }

    if (validCount <= 0) return 0;

    std::vector<int> indexArray;
    for (int i = 0; i < validCount; ++i) indexArray.push_back(i);

    // sort by objProbs descending (and keep indexArray consistent)
    quick_sort_indice_inverse(objProbs, 0, validCount - 1, indexArray);

    // NMS per class (this pose demo only has OBJ_CLASS_NUM=1)
    std::set<int> class_set(std::begin(classId), std::end(classId));
    for (auto c : class_set)
    {
        nms(validCount, filterBoxes, classId, indexArray, c, nms_threshold);
    }

    int last_count = 0;
    od_results->count = 0;

    for (int i = 0; i < validCount; ++i)
    {
        if (indexArray[i] == -1 || last_count >= OBJ_NUMB_MAX_SIZE) continue;

        int n = indexArray[i];

        float x1 = filterBoxes[n * 5 + 0];
        float y1 = filterBoxes[n * 5 + 1];
        float w = filterBoxes[n * 5 + 2];
        float h = filterBoxes[n * 5 + 3];
        int keypoints_index = (int)filterBoxes[n * 5 + 4];

        // decode keypoints from output[3] (FP16)
        const bool kp_is_fp16 = (app_ctx->output_attrs[3].type == RKNN_TENSOR_FLOAT16);
        for (int j = 0; j < 17; ++j)
        {
            if (kp_is_fp16)
            {
                const uint16_t *kp_buf = (const uint16_t *)_outputs[3].buf;
                int base = j * 3 * 8400 + keypoints_index;
                float xk = half_to_float(kp_buf[base + 0 * 8400]);
                float yk = half_to_float(kp_buf[base + 1 * 8400]);
                float ck = half_to_float(kp_buf[base + 2 * 8400]);
                od_results->results[last_count].keypoints[j][0] = xk / scale_w;
                od_results->results[last_count].keypoints[j][1] = yk / scale_h;
                od_results->results[last_count].keypoints[j][2] = ck;
            }
            else
            {
                // 兜底：如果运行时直接返回 float
                const float *kp_buf = (const float *)_outputs[3].buf;
                float xk = kp_buf[j * 3 * 8400 + 0 * 8400 + keypoints_index];
                float yk = kp_buf[j * 3 * 8400 + 1 * 8400 + keypoints_index];
                float ck = kp_buf[j * 3 * 8400 + 2 * 8400 + keypoints_index];
                od_results->results[last_count].keypoints[j][0] = xk / scale_w;
                od_results->results[last_count].keypoints[j][1] = yk / scale_h;
                od_results->results[last_count].keypoints[j][2] = ck;
            }
        }

        int id = classId[n];
        float obj_conf = objProbs[i];

        // boxes in model input coord -> original coord
        od_results->results[last_count].box.left = (int)(clamp(x1, 0, model_in_w) / scale_w);
        od_results->results[last_count].box.top = (int)(clamp(y1, 0, model_in_h) / scale_h);
        od_results->results[last_count].box.right = (int)(clamp(x1 + w, 0, model_in_w) / scale_w);
        od_results->results[last_count].box.bottom = (int)(clamp(y1 + h, 0, model_in_h) / scale_h);

        od_results->results[last_count].prop = obj_conf;
        od_results->results[last_count].cls_id = id;
        last_count++;
    }

    od_results->count = last_count;
    return 0;
}
