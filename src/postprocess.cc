// Copyright (c) 2024 by Rockchip Electronics Co., Ltd. All Rights Reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <opencv2/opencv.hpp>
#include "preprocess.h"

#include "mobilesam.h"
#define MASK_THRESHOLD 0

static int argmax(const float* arr, int size)
{
    int index = 0;
    for (int i = 0; i < size; i++)
    {
        if (arr[i] > arr[index])
        {
            index = i;
        }
    }
    return index;
}

static int clamp(float val, int min, int max)
{
    return val > min ? (val < max ? val : max) : min;
}

int post_process(rknn_app_context_t* app_ctx, float* iou_predictions, float* low_res_masks,
                 mobilesam_res* res, int ori_height, int ori_width)
{
    if (app_ctx == NULL || iou_predictions == NULL || low_res_masks == NULL || res == NULL ||
        ori_height <= 0 || ori_width <= 0)
    {
        return -1;
    }
    res->mask = NULL;
    res->score = 0.0f;

    const rknn_tensor_attr& scoreAttr = app_ctx->decoder.output_attrs[0];
    const rknn_tensor_attr& maskAttr = app_ctx->decoder.output_attrs[1];
    if (scoreAttr.n_elems == 0 || maskAttr.n_dims != 4)
    {
        return -1;
    }

    int masksNum = scoreAttr.n_elems;
    int lowResHeight = maskAttr.dims[2];
    int lowResWidth = maskAttr.dims[3];
    if (lowResHeight <= 0 || lowResWidth <= 0 ||
        maskAttr.dims[1] < masksNum ||
        maskAttr.n_elems < (uint32_t)(masksNum * lowResHeight * lowResWidth))
    {
        printf("unsupported decoder mask shape\n");
        return -1;
    }

    int index = argmax(iou_predictions, masksNum);
    int newShape[2];
    get_preprocess_shape(ori_height, ori_width, newShape);
    if (newShape[0] <= 0 || newShape[1] <= 0 ||
        newShape[0] > IMG_SIZE || newShape[1] > IMG_SIZE)
    {
        return -1;
    }

    uint8_t* resMask = (uint8_t*)malloc((size_t)ori_height * ori_width * sizeof(uint8_t));
    if (resMask == NULL)
    {
        return -1;
    }

    try
    {
        float* selectedMask = low_res_masks + index * lowResHeight * lowResWidth;
        cv::Mat lowRes(lowResHeight, lowResWidth, CV_32F, selectedMask);
        cv::Mat mask448;
        cv::resize(lowRes, mask448, cv::Size(IMG_SIZE, IMG_SIZE), 0, 0, cv::INTER_LINEAR);

        cv::Mat cropped = mask448(cv::Rect(0, 0, newShape[1], newShape[0]));
        cv::Mat maskOriginal;
        cv::resize(cropped, maskOriginal, cv::Size(ori_width, ori_height), 0, 0, cv::INTER_LINEAR);

        const float* maskData = maskOriginal.ptr<float>();
        int pixelCount = ori_height * ori_width;
        for (int i = 0; i < pixelCount; ++i)
        {
            resMask[i] = maskData[i] > MASK_THRESHOLD ? 1 : 0;
        }
    }
    catch (const cv::Exception& e)
    {
        printf("OpenCV postprocess fail: %s\n", e.what());
        free(resMask);
        return -1;
    }

    res->mask = resMask;
    res->score = iou_predictions[index];
    return 0;
}

void draw_mask(image_buffer_t* src_imag, uint8_t* mask)
{
    if (src_imag == NULL || src_imag->virt_addr == NULL || mask == NULL ||
        src_imag->format != IMAGE_FORMAT_RGB888)
    {
        return;
    }

    static const int color[3] = {30, 144, 144};
    int width = src_imag->width;
    int height = src_imag->height;
    uint8_t* oriImg = src_imag->virt_addr;
    float alpha = 0.5f;
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            int pixelOffset = 3 * (i * width + j);
            if (mask[i * width + j] != 0)
            {
                oriImg[pixelOffset + 0] = (unsigned char)clamp(color[0] * (1 - alpha) + oriImg[pixelOffset + 0] * alpha, 0, 255);
                oriImg[pixelOffset + 1] = (unsigned char)clamp(color[1] * (1 - alpha) + oriImg[pixelOffset + 1] * alpha, 0, 255);
                oriImg[pixelOffset + 2] = (unsigned char)clamp(color[2] * (1 - alpha) + oriImg[pixelOffset + 2] * alpha, 0, 255);
            }
        }
    }
}

void rknn_nchw_2_nhwc(float* nchw, float* nhwc, int N, int C, int H, int W)
{
    if (nchw == NULL || nhwc == NULL)
    {
        return;
    }
    for (int ni = 0; ni < N; ni++)
    {
        for (int hi = 0; hi < H; hi++)
        {
            for (int wi = 0; wi < W; wi++)
            {
                for (int ci = 0; ci < C; ci++)
                {
                    nhwc[ni * H * W * C + hi * W * C + wi * C + ci] =
                        nchw[ni * C * H * W + ci * H * W + hi * W + wi];
                }
            }
        }
    }
}
