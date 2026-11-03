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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <vector>
#include "preprocess.h"

float* read_coords_from_file(const char* filename, int* value_count)
{
    if (filename == NULL || value_count == NULL)
    {
        return NULL;
    }

    std::ifstream file(filename);
    if (!file.is_open())
    {
        printf("Failed to open the file: %s\n", filename);
        return NULL;
    }

    std::vector<float> values;
    float value;
    while (file >> value)
    {
        values.push_back(value);
    }
    if (!file.eof() || values.empty())
    {
        printf("Invalid or empty prompt file: %s\n", filename);
        return NULL;
    }

    float* result = (float*)malloc(values.size() * sizeof(float));
    if (result == NULL)
    {
        return NULL;
    }
    memcpy(result, values.data(), values.size() * sizeof(float));
    *value_count = static_cast<int>(values.size());
    return result;
}

int get_preprocess_shape(int ori_heigth, int ori_width, int* new_shape)
{
    if (ori_heigth <= 0 || ori_width <= 0 || new_shape == NULL)
    {
        return -1;
    }

    float scale = IMG_SIZE * 1.0 / (ori_heigth > ori_width ? ori_heigth : ori_width);
    new_shape[0] = ori_heigth * scale + 0.5;
    new_shape[1] = ori_width * scale + 0.5;
    return 0;
}

int point_coords_preprocess(float* ori_point_coords, int coords_size, int ori_height,
                            int ori_width, float* cvt_point_coords)
{
    if (ori_point_coords == NULL || cvt_point_coords == NULL || coords_size <= 0 ||
        coords_size % 2 != 0 || ori_height <= 0 || ori_width <= 0)
    {
        return -1;
    }

    int new_shape[2];
    if (get_preprocess_shape(ori_height, ori_width, new_shape) != 0)
    {
        return -1;
    }
    for (int i = 0; i < coords_size; i += 2)
    {
        cvt_point_coords[i] = ori_point_coords[i] * (new_shape[1] * 1.0 / ori_width);
        cvt_point_coords[i + 1] = ori_point_coords[i + 1] * (new_shape[0] * 1.0 / ori_height);
    }
    return 0;
}

int pre_process(image_buffer_t* src_img, image_buffer_t* dst_img)
{
    if (src_img == NULL || dst_img == NULL || src_img->virt_addr == NULL ||
        dst_img->virt_addr == NULL || src_img->format != IMAGE_FORMAT_RGB888 ||
        src_img->width <= 0 || src_img->height <= 0 ||
        dst_img->width != IMG_SIZE || dst_img->height != IMG_SIZE)
    {
        printf("invalid MobileSAM preprocessing input\n");
        return -1;
    }

    int new_shape[2];
    if (get_preprocess_shape(src_img->height, src_img->width, new_shape) != 0)
    {
        return -1;
    }
    int newh = new_shape[0];
    int neww = new_shape[1];
    int padh = IMG_SIZE - newh;
    int padw = IMG_SIZE - neww;
    printf("src_height:%d, src_width:%d\n", src_img->height, src_img->width);
    printf("newh:%d neww:%d padh:%d padw:%d\n", newh, neww, padh, padw);

    try
    {
        cv::Mat img(src_img->height, src_img->width, CV_8UC3, src_img->virt_addr);
        cv::Mat resized;
        cv::Mat padded;
        cv::resize(img, resized, cv::Size(neww, newh), 0, 0, cv::INTER_LINEAR);
        cv::copyMakeBorder(resized, padded, 0, padh, 0, padw,
                           cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        if (!padded.isContinuous() ||
            dst_img->size < 3 * IMG_SIZE * IMG_SIZE * (int)sizeof(unsigned char))
        {
            return -1;
        }
        memcpy(dst_img->virt_addr, padded.data,
               3 * IMG_SIZE * IMG_SIZE * sizeof(unsigned char));
    }
    catch (const cv::Exception& e)
    {
        printf("OpenCV preprocess fail: %s\n", e.what());
        return -1;
    }

    return 0;
}
