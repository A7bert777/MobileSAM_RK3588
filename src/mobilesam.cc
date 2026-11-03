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
#include <vector>

#include "mobilesam.h"
#include "common.h"
#include "file_utils.h"
#include "image_utils.h"

int init_mobilesam_model(const char* encoder_model_path, const char* decoder_model_path,
                         rknn_app_context_t* app_ctx)
{
    if (encoder_model_path == NULL || decoder_model_path == NULL || app_ctx == NULL)
    {
        return -1;
    }
    memset(app_ctx, 0, sizeof(*app_ctx));

    printf("--> init mobilesam encoder model\n");
    int ret = init_mobilesam_model_utils(&(app_ctx->encoder), encoder_model_path);
    if (ret < 0)
    {
        printf("rknn_init mobilesam encoder model fail! ret=%d\n", ret);
        return ret;
    }

    printf("--> init mobilesam decoder model\n");
    ret = init_mobilesam_model_utils(&(app_ctx->decoder), decoder_model_path);
    if (ret < 0)
    {
        printf("rknn_init mobilesam decoder model fail! ret=%d\n", ret);
        release_mobilesam_model_utils(&(app_ctx->encoder));
        return ret;
    }

    return 0;
}

int release_mobilesam_model(rknn_app_context_t* app_ctx)
{
    if (app_ctx == NULL)
    {
        return -1;
    }

    int encoderRet = release_mobilesam_model_utils(&(app_ctx->encoder));
    int decoderRet = release_mobilesam_model_utils(&(app_ctx->decoder));
    return encoderRet != 0 ? encoderRet : decoderRet;
}

int inference_mobilesam_model(rknn_app_context_t* app_ctx, image_buffer_t* img,
                              float* point_coords, float* point_labels, mobilesam_res* res)
{
    if (app_ctx == NULL || img == NULL || img->virt_addr == NULL || point_coords == NULL ||
        point_labels == NULL || res == NULL || app_ctx->encoder.output_attrs == NULL ||
        app_ctx->decoder.output_attrs == NULL)
    {
        printf("invalid MobileSAM inference argument\n");
        return -1;
    }
    memset(res, 0, sizeof(*res));

    const rknn_tensor_attr& embeddingAttr = app_ctx->encoder.output_attrs[0];
    if (embeddingAttr.n_dims != 4 || embeddingAttr.n_elems == 0 ||
        embeddingAttr.n_elems != app_ctx->decoder.input_attrs[0].n_elems)
    {
        printf("unsupported image embedding shape\n");
        return -1;
    }

    std::vector<float> imgEmbedsNchw(embeddingAttr.n_elems, 0.0f);
    std::vector<float> imgEmbedsNhwc(embeddingAttr.n_elems, 0.0f);
    std::vector<float> iouPredictions(app_ctx->decoder.output_attrs[0].n_elems, 0.0f);
    std::vector<float> lowResMasks(app_ctx->decoder.output_attrs[1].n_elems, 0.0f);

    printf("--> inference mobilesam encoder model\n");
    int ret = inference_mobilesam_encoder_utils(&(app_ctx->encoder), img, imgEmbedsNchw.data());
    if (ret != 0)
    {
        printf("inference mobilesam encoder model fail! ret=%d\n", ret);
        return ret;
    }

    rknn_nchw_2_nhwc(imgEmbedsNchw.data(), imgEmbedsNhwc.data(),
                     embeddingAttr.dims[0], embeddingAttr.dims[1],
                     embeddingAttr.dims[2], embeddingAttr.dims[3]);

    printf("--> inference mobilesam decoder model\n");
    ret = inference_mobilesam_decoder_utils(&(app_ctx->decoder), imgEmbedsNhwc.data(),
                                            point_coords, point_labels,
                                            iouPredictions.data(), lowResMasks.data());
    if (ret != 0)
    {
        printf("inference mobilesam decoder model fail! ret=%d\n", ret);
        return ret;
    }

    return post_process(app_ctx, iouPredictions.data(), lowResMasks.data(),
                        res, img->height, img->width);
}
