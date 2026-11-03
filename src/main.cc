#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mobilesam.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <dirent.h>
#include <set>
#include <string>
#include <vector>

std::string extractFileNameWithoutExtension(const std::string &path)
{
    auto pos = path.find_last_of("/\\");
    std::string filename = (pos == std::string::npos) ? path : path.substr(pos + 1);
    pos = filename.find_last_of(".");
    if (pos != std::string::npos)
    {
        filename = filename.substr(0, pos);
    }
    return filename;
}

static bool hasImageExtension(const std::string &fileName)
{
    auto pos = fileName.find_last_of(".");
    if (pos == std::string::npos)
    {
        return false;
    }
    std::string extension = fileName.substr(pos);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png";
}

static int readPromptFiles(const char *coordsPath, const char *labelsPath,
                           std::vector<float> *pointCoords, std::vector<float> *pointLabels)
{
    int coordValueCount = 0;
    int labelValueCount = 0;
    float *coords = read_coords_from_file(coordsPath, &coordValueCount);
    float *labels = read_coords_from_file(labelsPath, &labelValueCount);
    if (coords == NULL || labels == NULL || coordValueCount <= 0 || labelValueCount <= 0)
    {
        printf("read prompt files fail! coords_path=%s labels_path=%s\n", coordsPath, labelsPath);
        free(coords);
        free(labels);
        return -1;
    }

    pointCoords->assign(coords, coords + coordValueCount);
    pointLabels->assign(labels, labels + labelValueCount);
    free(coords);
    free(labels);
    return 0;
}

static void drawPrompts(image_buffer_t *image, const std::vector<float> &pointCoords,
                        const std::vector<float> &pointLabels)
{
    mobilesam_box box = {0, 0, 0, 0};
    bool hasTopLeft = false;
    bool hasBottomRight = false;

    for (size_t i = 0; i < pointLabels.size(); ++i)
    {
        int pointX = static_cast<int>(pointCoords[i * 2]);
        int pointY = static_cast<int>(pointCoords[i * 2 + 1]);
        int label = static_cast<int>(pointLabels[i]);
        if (label == 0)
        {
            draw_circle(image, pointX, pointY, 12, COLOR_RED, 2);
        }
        else if (label == 1)
        {
            draw_circle(image, pointX, pointY, 12, COLOR_GREEN, 2);
        }
        else if (label == 2)
        {
            box.x1 = pointX;
            box.y1 = pointY;
            hasTopLeft = true;
        }
        else if (label == 3)
        {
            box.x2 = pointX;
            box.y2 = pointY;
            hasBottomRight = true;
        }
    }

    if (hasTopLeft && hasBottomRight)
    {
        draw_rectangle(image, box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1, COLOR_GREEN, 3);
    }
}

// 模型和提示只加载一次，逐张处理目录中的 JPG/JPEG/PNG 图片。
int processImagesInFolder(const std::string &folderPath, rknn_app_context_t *rknnAppCtx,
                          const std::string &outputFolderPath,
                          const std::vector<float> &originalPointCoords,
                          const std::vector<float> &pointLabels)
{
    DIR *dir = opendir(folderPath.c_str());
    if (dir == nullptr)
    {
        perror("opendir");
        return -1;
    }

    std::vector<std::string> fileNames;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string fileName = entry->d_name;
        if (hasImageExtension(fileName))
        {
            fileNames.push_back(fileName);
        }
    }
    closedir(dir);
    std::sort(fileNames.begin(), fileNames.end());

    std::set<std::string> usedOutputNames;
    int succeeded = 0;
    int failed = 0;
    for (const std::string &fileName : fileNames)
    {
        std::string stem = extractFileNameWithoutExtension(fileName);
        std::string outputName = stem + "_out.png";
        if (usedOutputNames.count(outputName) != 0)
        {
            outputName = fileName + "_out.png";
        }
        if (usedOutputNames.count(outputName) != 0)
        {
            const std::string outputBase = fileName + "_out";
            int suffix = 2;
            do
            {
                outputName = outputBase + "_" + std::to_string(suffix) + ".png";
                ++suffix;
            } while (usedOutputNames.count(outputName) != 0);
        }
        usedOutputNames.insert(outputName);
        std::string fullPath = folderPath + "/" + fileName;
        std::string outputFileName = outputFolderPath + "/" + outputName;
        printf("\nProcessing %s\n", fullPath.c_str());

        image_buffer_t srcImage;
        memset(&srcImage, 0, sizeof(srcImage));
        mobilesam_res res;
        memset(&res, 0, sizeof(res));

        int ret = read_image(fullPath.c_str(), &srcImage);
        if (ret != 0)
        {
            printf("read image fail! ret=%d image_path=%s\n", ret, fullPath.c_str());
            ++failed;
            continue;
        }

        std::vector<float> convertedCoords(originalPointCoords.size());
        ret = point_coords_preprocess(const_cast<float *>(originalPointCoords.data()),
                                      static_cast<int>(originalPointCoords.size()),
                                      srcImage.height, srcImage.width, convertedCoords.data());
        if (ret == 0)
        {
            ret = inference_mobilesam_model(rknnAppCtx, &srcImage, convertedCoords.data(),
                                            const_cast<float *>(pointLabels.data()), &res);
        }
        if (ret != 0)
        {
            printf("inference_mobilesam_model fail! ret=%d image_path=%s\n", ret, fullPath.c_str());
            free(res.mask);
            free(srcImage.virt_addr);
            ++failed;
            continue;
        }
        printf("mask score: %.6f\n", res.score);

        draw_mask(&srcImage, res.mask);
        drawPrompts(&srcImage, originalPointCoords, pointLabels);

        ret = write_image(outputFileName.c_str(), &srcImage);
        if (ret != 0)
        {
            printf("write image fail! ret=%d output_path=%s\n", ret, outputFileName.c_str());
            ++failed;
        }
        else
        {
            printf("saved %s\n", outputFileName.c_str());
            ++succeeded;
        }

        free(res.mask);
        free(srcImage.virt_addr);
    }

    printf("\nBatch finished: %d succeeded, %d failed\n", succeeded, failed);
    return (succeeded > 0 && failed == 0) ? 0 : -1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    // ***** 以下六个参数：Encoder、Decoder、提示坐标、提示标签、输入目录、输出目录（均为绝对路径）
    const std::string encoderModelPath = "/data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/model/mobilesam_encoder_tiny_fp16.rknn";
    const std::string decoderModelPath = "/data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/model/mobilesam_decoder_fp16.rknn";
    const std::string coordsPath = "/data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/model/coords.txt";
    const std::string labelsPath = "/data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/model/labels.txt";
    const std::string imageFolder = "/data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/inputimage";
    const std::string outputFolder = "/data/04RKNN_GIthub_related/rknn_play/MobileSAM_RK3588/outputimage";

    std::vector<float> pointCoords;
    std::vector<float> pointLabels;
    if (readPromptFiles(coordsPath.c_str(), labelsPath.c_str(), &pointCoords, &pointLabels) != 0)
    {
        return -1;
    }
    if (pointCoords.size() != pointLabels.size() * 2)
    {
        printf("prompt size mismatch: %zu coordinates, %zu labels\n",
               pointCoords.size() / 2, pointLabels.size());
        return -1;
    }
    for (float coord : pointCoords)
    {
        if (!std::isfinite(coord))
        {
            printf("prompt coordinates must be finite numbers\n");
            return -1;
        }
    }
    for (float label : pointLabels)
    {
        if (label != -1 && label != 0 && label != 1 && label != 2 && label != 3)
        {
            printf("invalid prompt label: %f; expected -1, 0, 1, 2 or 3\n", label);
            return -1;
        }
    }

    rknn_app_context_t rknnAppCtx;
    memset(&rknnAppCtx, 0, sizeof(rknnAppCtx));
    int ret = init_mobilesam_model(encoderModelPath.c_str(), decoderModelPath.c_str(), &rknnAppCtx);
    if (ret != 0)
    {
        printf("init_mobilesam_model fail! ret=%d encoder=%s decoder=%s\n",
               ret, encoderModelPath.c_str(), decoderModelPath.c_str());
        return -1;
    }

    uint32_t expectedPoints = rknnAppCtx.decoder.input_attrs[1].n_elems / 2;
    if (rknnAppCtx.decoder.input_attrs[1].n_elems % 2 != 0 ||
        pointLabels.size() != expectedPoints ||
        rknnAppCtx.decoder.input_attrs[2].n_elems != expectedPoints)
    {
        printf("decoder expects %u prompt points, but files provide %zu\n",
               expectedPoints, pointLabels.size());
        release_mobilesam_model(&rknnAppCtx);
        return -1;
    }

    int processRet = processImagesInFolder(imageFolder, &rknnAppCtx, outputFolder,
                                           pointCoords, pointLabels);
    int releaseRet = release_mobilesam_model(&rknnAppCtx);
    if (releaseRet != 0)
    {
        printf("release_mobilesam_model fail! ret=%d\n", releaseRet);
    }

    return (processRet == 0 && releaseRet == 0) ? 0 : -1;
}
