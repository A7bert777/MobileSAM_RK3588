#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <math.h>
#include <sys/time.h>

#include "im2d.h"
#include "drmrga.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "image_utils.h"
#include "file_utils.h"

static const char* filter_image_names[] = {
    "jpg",
    "jpeg",
    "JPG",
    "JPEG",
    "png",
    "PNG",
    "data",
    NULL
};

#ifndef DISABLE_LIBJPEG
#include "turbojpeg.h"
static const char* subsampName[TJ_NUMSAMP] = {"4:4:4", "4:2:2", "4:2:0", "Grayscale", "4:4:0", "4:1:1"};
static const char* colorspaceName[TJ_NUMCS] = {"RGB", "YCbCr", "GRAY", "CMYK", "YCCK"};

static int read_image_jpeg(const char* path, image_buffer_t* image)
{
    FILE* jpegFile = NULL;
    unsigned char* jpegBuf = NULL;
    unsigned char* sw_out_buf = NULL;
    tjhandle handle = NULL;
    int own_buffer = 0;
    int ret = -1;

    jpegFile = fopen(path, "rb");
    if (jpegFile == NULL) {
        printf("open input file failure: %s\n", path);
        goto out;
    }
    if (fseek(jpegFile, 0, SEEK_END) < 0) {
        printf("seek input file failure: %s\n", path);
        goto out;
    }
    long file_size = ftell(jpegFile);
    if (file_size <= 0 || fseek(jpegFile, 0, SEEK_SET) < 0) {
        printf("determining input file size failure: %s\n", path);
        goto out;
    }

    unsigned long jpegSize = (unsigned long)file_size;
    jpegBuf = (unsigned char*)malloc(jpegSize);
    if (jpegBuf == NULL) {
        printf("allocating JPEG buffer failure\n");
        goto out;
    }
    if (fread(jpegBuf, 1, jpegSize, jpegFile) != jpegSize) {
        printf("reading input file failure: %s\n", path);
        goto out;
    }
    fclose(jpegFile);
    jpegFile = NULL;

    handle = tjInitDecompress();
    if (handle == NULL) {
        printf("tjInitDecompress failure: %s\n", tjGetErrorStr());
        goto out;
    }

    int width, height, subsample, colorspace;
    ret = tjDecompressHeader3(handle, jpegBuf, jpegSize, &width, &height, &subsample, &colorspace);
    if (ret < 0 || width <= 0 || height <= 0) {
        printf("JPEG header error: %s, errorCode:%d\n", tjGetErrorStr(), tjGetErrorCode(handle));
        goto out;
    }

    size_t sw_out_size = (size_t)width * (size_t)height * 3;
    sw_out_buf = image->virt_addr;
    if (sw_out_buf == NULL) {
        sw_out_buf = (unsigned char*)malloc(sw_out_size);
        own_buffer = 1;
    }
    if (sw_out_buf == NULL) {
        printf("allocating RGB output buffer failure\n");
        ret = -1;
        goto out;
    }

    ret = tjDecompress2(handle, jpegBuf, jpegSize, sw_out_buf, width, 0, height, TJPF_RGB, 0);
    if (ret < 0 && tjGetErrorCode(handle) != 0) {
        printf("JPEG decompress error: %s, errorCode:%d\n", tjGetErrorStr(), tjGetErrorCode(handle));
        goto out;
    }
    if (ret < 0) {
        printf("JPEG decompress warning: %s\n", tjGetErrorStr());
    }

    image->width = width;
    image->height = height;
    image->width_stride = width;
    image->height_stride = height;
    image->format = IMAGE_FORMAT_RGB888;
    image->virt_addr = sw_out_buf;
    image->size = (int)sw_out_size;
    ret = 0;

out:
    if (jpegFile != NULL) {
        fclose(jpegFile);
    }
    if (handle != NULL) {
        tjDestroy(handle);
    }
    free(jpegBuf);
    if (ret != 0 && own_buffer) {
        free(sw_out_buf);
    }
    return ret;
}

static int write_image_jpeg(const char* path, int quality, const image_buffer_t* image)
{
    int ret;
    int jpegSubsamp = TJSAMP_422;
    unsigned char* jpegBuf = NULL;
    unsigned long jpegSize = 0;
    int flags = 0;

    const unsigned char* data = image->virt_addr;
    int width = image->width;
    int height = image->height;
    int pixelFormat = TJPF_RGB;

	tjhandle handle = tjInitCompress();

    if (image->format == IMAGE_FORMAT_RGB888) {
        ret = tjCompress2(handle, data, width, 0, height, pixelFormat, &jpegBuf, &jpegSize, jpegSubsamp, quality, flags);
    } else {
        printf("write_image_jpeg: pixel format %d not support\n", image->format);
        return -1;
    }

	// printf("ret=%d jpegBuf=%p jpegSize=%d\n", ret, jpegBuf, jpegSize);
    if (jpegBuf != NULL && jpegSize > 0) {
        write_data_to_file(path, (const char*)jpegBuf, jpegSize);
        tjFree(jpegBuf);
    }
    tjDestroy(handle);

	return 0;
}
#endif

static int image_file_filter(const struct dirent *entry)
{
    const char ** filter;

    for (filter = filter_image_names; *filter; ++filter) {
        if(strstr(entry->d_name, *filter) != NULL) {
            return 1;
        }
    }
    return 0;
}

static int read_image_raw(const char* path, image_buffer_t* image)
{
    FILE *fp = fopen(path, "rb");
    if(fp == NULL) {
        printf("fopen %s fail!\n", path);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    unsigned char *data = image->virt_addr;
    if (image->virt_addr == NULL) {
        data = (unsigned char *)malloc(file_size+1);
    }
    data[file_size] = 0;
    fseek(fp, 0, SEEK_SET);
    if(file_size != fread(data, 1, file_size, fp)) {
        printf("fread %s fail!\n", path);
        free(data);
        return -1;
    }
    if(fp) {
        fclose(fp);
    }
    if (image->virt_addr == NULL) {
        image->virt_addr = data;
        image->size = file_size;
    }

    return 0;
}

static int read_image_stb(const char* path, image_buffer_t* image)
{
    int w, h, c;
    // 灰度、RGB 和 RGBA 图片统一转换为 RGB888。
    unsigned char* pixeldata = stbi_load(path, &w, &h, &c, 3);
    if (!pixeldata) {
        printf("error: read image %s fail: %s\n", path, stbi_failure_reason());
        return -1;
    }
    int size = w * h * 3;

    if (image->virt_addr != NULL) {
        memcpy(image->virt_addr, pixeldata, size);
        stbi_image_free(pixeldata);
    } else {
        image->virt_addr = pixeldata;
    }
    image->width = w;
    image->height = h;
    image->width_stride = w;
    image->height_stride = h;
    image->format = IMAGE_FORMAT_RGB888;
    image->size = size;
    return 0;
}

int read_image(const char* path, image_buffer_t* image)
{
    const char* _ext = strrchr(path, '.');
    if (!_ext) {
        // missing extension
        return -1;
    }
    if (strcmp(_ext, ".data") == 0) {
        return read_image_raw(path, image);
#ifndef DISABLE_LIBJPEG
    } else if (strcmp(_ext, ".jpg") == 0 || strcmp(_ext, ".jpeg") == 0 || strcmp(_ext, ".JPG") == 0 ||
        strcmp(_ext, ".JPEG") == 0) {
        return read_image_jpeg(path, image);
#endif
    } else {
        return read_image_stb(path, image);
    }
}

int write_image(const char* path, const image_buffer_t* img)
{
    int ret;
    int width = img->width;
    int height = img->height;
    int channel = 3;
    void* data = img->virt_addr;
    printf("write_image path: %s width=%d height=%d channel=%d data=%p\n",
        path, width, height, channel, data);

    const char* _ext = strrchr(path, '.');
    if (!_ext) {
        // missing extension
        return -1;
    }

    if (strcmp(_ext, ".png") == 0 || strcmp(_ext, ".PNG") == 0) {
        ret = stbi_write_png(path, width, height, channel, data, 0) ? 0 : -1;

    } else if (strcmp(_ext, ".jpg") == 0 || strcmp(_ext, ".jpeg") == 0 || strcmp(_ext, ".JPG") == 0 ||
        strcmp(_ext, ".JPEG") == 0) {
        int quality = 95;
#ifndef DISABLE_LIBJPEG
        ret = write_image_jpeg(path, quality, img);
#else
        ret = stbi_write_jpg(path, width, height, channel, data, quality);
#endif
    } else if (strcmp(_ext, ".data") == 0 || strcmp(_ext, ".DATA") == 0) {
        int size = get_image_size(img);
        ret = write_data_to_file(path, data, size);
    } else {
        // unknown extension type
        return -1;
    }
    return ret;
}

static int crop_and_scale_image_c(int channel, unsigned char *src, int src_width, int src_height,
                                    int crop_x, int crop_y, int crop_width, int crop_height,
                                    unsigned char *dst, int dst_width, int dst_height,
                                    int dst_box_x, int dst_box_y, int dst_box_width, int dst_box_height) {
    if (dst == NULL) {
        printf("dst buffer is null\n");
        return -1;
    }

    float x_ratio = (float)crop_width / (float)dst_box_width;
    float y_ratio = (float)crop_height / (float)dst_box_height;

    // printf("src_width=%d src_height=%d crop_x=%d crop_y=%d crop_width=%d crop_height=%d\n",
    //     src_width, src_height, crop_x, crop_y, crop_width, crop_height);
    // printf("dst_width=%d dst_height=%d dst_box_x=%d dst_box_y=%d dst_box_width=%d dst_box_height=%d\n",
    //     dst_width, dst_height, dst_box_x, dst_box_y, dst_box_width, dst_box_height);
    // printf("channel=%d x_ratio=%f y_ratio=%f\n", channel, x_ratio, y_ratio);

    // 从原图指定区域取数据，双线性缩放到目标指定区域
    for (int dst_y = dst_box_y; dst_y < dst_box_y + dst_box_height; dst_y++) {
        for (int dst_x = dst_box_x; dst_x < dst_box_x + dst_box_width; dst_x++) {
            int dst_x_offset = dst_x - dst_box_x;
            int dst_y_offset = dst_y - dst_box_y;

            int src_x = (int)(dst_x_offset * x_ratio) + crop_x;
            int src_y = (int)(dst_y_offset * y_ratio) + crop_y;

            float x_diff = (dst_x_offset * x_ratio) - (src_x - crop_x);
            float y_diff = (dst_y_offset * y_ratio) - (src_y - crop_y);

            int index1 = src_y * src_width * channel + src_x * channel;
            int down_offset = src_height == 1 ? 0 : src_width * channel;
            int right_offset = src_width == 1 ? 0 : channel;
            int index2 = index1 + down_offset;            // down
            if (src_y == src_height - 1 && src_height > 1) {
                // 如果到图像最下边缘，变成选择上面的像素
                index2 = index1 - down_offset;
            }
            int index3 = index1 + right_offset;           // right
            int index4 = index2 + right_offset;           // down right
            if (src_x == src_width - 1 && src_width > 1) {
                // 如果到图像最右边缘，变成选择左边的像素
                index3 = index1 - right_offset;
                index4 = index2 - right_offset;
            }

            // printf("dst_x=%d dst_y=%d dst_x_offset=%d dst_y_offset=%d src_x=%d src_y=%d x_diff=%f y_diff=%f src index=%d %d %d %d\n",
            //     dst_x, dst_y, dst_x_offset, dst_y_offset,
            //     src_x, src_y, x_diff, y_diff,
            //     index1, index2, index3, index4);

            for (int c = 0; c < channel; c++) {
                unsigned char A = src[index1+c];
                unsigned char B = src[index3+c];
                unsigned char C = src[index2+c];
                unsigned char D = src[index4+c];

                unsigned char pixel = (unsigned char)(
                    A * (1 - x_diff) * (1 - y_diff) +
                    B * x_diff * (1 - y_diff) +
                    C * y_diff * (1 - x_diff) +
                    D * x_diff * y_diff
                );

                dst[(dst_y * dst_width  + dst_x) * channel + c] = pixel;
            }
        }
    }

    return 0;
}

static int crop_and_scale_image_yuv420sp(unsigned char *src, int src_width, int src_height,
                                    int crop_x, int crop_y, int crop_width, int crop_height,
                                    unsigned char *dst, int dst_width, int dst_height,
                                    int dst_box_x, int dst_box_y, int dst_box_width, int dst_box_height) {

    unsigned char* src_y = src;
    unsigned char* src_uv = src + src_width * src_height;

    unsigned char* dst_y = dst;
    unsigned char* dst_uv = dst + dst_width * dst_height;

    crop_and_scale_image_c(1, src_y, src_width, src_height, crop_x, crop_y, crop_width, crop_height,
        dst_y, dst_width, dst_height, dst_box_x, dst_box_y, dst_box_width, dst_box_height);
    
    crop_and_scale_image_c(2, src_uv, src_width / 2, src_height / 2, crop_x / 2, crop_y / 2, crop_width / 2, crop_height / 2,
        dst_uv, dst_width / 2, dst_height / 2, dst_box_x, dst_box_y, dst_box_width, dst_box_height);

    return 0;
}

static int convert_image_cpu(image_buffer_t *src, image_buffer_t *dst, image_rect_t *src_box, image_rect_t *dst_box, char color) {
    int ret;
    if (dst->virt_addr == NULL) {
        return -1;
    }
    if (src->virt_addr == NULL) {
        return -1;
    }
    if (src->format != dst->format) {
        return -1;
    }

    int src_box_x = 0;
    int src_box_y = 0;
    int src_box_w = src->width;
    int src_box_h = src->height;
    if (src_box != NULL) {
        src_box_x = src_box->left;
        src_box_y = src_box->top;
        src_box_w = src_box->right - src_box->left + 1;
        src_box_h = src_box->bottom - src_box->top + 1;
    }
    int dst_box_x = 0;
    int dst_box_y = 0;
    int dst_box_w = dst->width;
    int dst_box_h = dst->height;
    if (dst_box != NULL) {
        dst_box_x = dst_box->left;
        dst_box_y = dst_box->top;
        dst_box_w = dst_box->right - dst_box->left + 1;
        dst_box_h = dst_box->bottom - dst_box->top + 1;
    }

    // fill pad color
    if (dst_box_w != dst->width || dst_box_h != dst->height) {
        int dst_size = get_image_size(dst);
        memset(dst->virt_addr, color, dst_size);
    }

    int need_release_dst_buffer = 0;
    int reti = 0;
    if (src->format == IMAGE_FORMAT_RGB888) {
        reti = crop_and_scale_image_c(3, src->virt_addr, src->width, src->height,
            src_box_x, src_box_y, src_box_w, src_box_h,
            dst->virt_addr, dst->width, dst->height,
            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    } else if (src->format == IMAGE_FORMAT_RGBA8888) {
        reti = crop_and_scale_image_c(4, src->virt_addr, src->width, src->height,
            src_box_x, src_box_y, src_box_w, src_box_h,
            dst->virt_addr, dst->width, dst->height,
            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    } else if (src->format == IMAGE_FORMAT_GRAY8) {
        reti = crop_and_scale_image_c(1, src->virt_addr, src->width, src->height,
            src_box_x, src_box_y, src_box_w, src_box_h,
            dst->virt_addr, dst->width, dst->height,
            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    } else if (src->format == IMAGE_FORMAT_YUV420SP_NV12 || src->format == IMAGE_FORMAT_YUV420SP_NV21) {
        reti = crop_and_scale_image_yuv420sp(src->virt_addr, src->width, src->height,
            src_box_x, src_box_y, src_box_w, src_box_h,
            dst->virt_addr, dst->width, dst->height,
            dst_box_x, dst_box_y, dst_box_w, dst_box_h);
    } else {
        printf("no support format %d\n", src->format);
    }
    if (reti != 0) {
        printf("convert_image_cpu fail %d\n", reti);
        return -1;
    }
    printf("finish\n");
    return 0;
}

static int get_rga_fmt(image_format_t fmt) {
    switch (fmt)
    {
    case IMAGE_FORMAT_RGB888:
        return RK_FORMAT_RGB_888;
    case IMAGE_FORMAT_RGBA8888:
        return RK_FORMAT_RGBA_8888;
    case IMAGE_FORMAT_YUV420SP_NV12:
        return RK_FORMAT_YCbCr_420_SP;
    case IMAGE_FORMAT_YUV420SP_NV21:
        return RK_FORMAT_YCrCb_420_SP;
    default:
        return -1;
    }
}

int get_image_size(const image_buffer_t* image)
{
    if (image == NULL) {
        return 0;
    }
    switch (image->format)
    {
    case IMAGE_FORMAT_GRAY8:
        return image->width * image->height;
    case IMAGE_FORMAT_RGB888:
        return image->width * image->height * 3;    
    case IMAGE_FORMAT_RGBA8888:
        return image->width * image->height * 4;
    case IMAGE_FORMAT_YUV420SP_NV12:
    case IMAGE_FORMAT_YUV420SP_NV21:
        return image->width * image->height * 3 / 2;
    default:
        break;
    }
}

static int convert_image_rga(image_buffer_t* src_img, image_buffer_t* dst_img, image_rect_t* src_box, image_rect_t* dst_box, char color)
{
    int ret = 0;

    int srcWidth = src_img->width;
    int srcHeight = src_img->height;
    void *src = src_img->virt_addr;
    int src_fd = src_img->fd;
    void *src_phy = NULL;
    int srcFmt = get_rga_fmt(src_img->format);

    int dstWidth = dst_img->width;
    int dstHeight = dst_img->height;
    void *dst = dst_img->virt_addr;
    int dst_fd = dst_img->fd;
    void *dst_phy = NULL;
    int dstFmt = get_rga_fmt(dst_img->format);

    int rotate = 0;

    int use_handle = 0;
#if defined(LIBRGA_IM2D_HANDLE)
    use_handle = 1;
#endif

    // printf("src width=%d height=%d fmt=0x%x virAddr=0x%p fd=%d\n",
    //     srcWidth, srcHeight, srcFmt, src, src_fd);
    // printf("dst width=%d height=%d fmt=0x%x virAddr=0x%p fd=%d\n",
    //     dstWidth, dstHeight, dstFmt, dst, dst_fd);
    // printf("rotate=%d\n", rotate);

    int usage = 0;
    IM_STATUS ret_rga = IM_STATUS_NOERROR;

    // set rga usage
    usage |= rotate;

    // set rga rect
    im_rect srect;
    im_rect drect;
    im_rect prect;
    memset(&prect, 0, sizeof(im_rect));

    if (src_box != NULL) {
        srect.x = src_box->left;
        srect.y = src_box->top;
        srect.width = src_box->right - src_box->left + 1;
        srect.height = src_box->bottom - src_box->top + 1;
    } else {
        srect.x = 0;
        srect.y = 0;
        srect.width = srcWidth;
        srect.height = srcHeight;
    }

    if (dst_box != NULL) {
        drect.x = dst_box->left;
        drect.y = dst_box->top;
        drect.width = dst_box->right - dst_box->left + 1;
        drect.height = dst_box->bottom - dst_box->top + 1;
    } else {
        drect.x = 0;
        drect.y = 0;
        drect.width = dstWidth;
        drect.height = dstHeight;
    }

    // set rga buffer
    rga_buffer_t rga_buf_src;
    rga_buffer_t rga_buf_dst;
    rga_buffer_t pat;
    rga_buffer_handle_t rga_handle_src = 0;
    rga_buffer_handle_t rga_handle_dst = 0;
    memset(&pat, 0, sizeof(rga_buffer_t));

    im_handle_param_t in_param;
    in_param.width = srcWidth;
    in_param.height = srcHeight;
    in_param.format = srcFmt;

    im_handle_param_t dst_param;
    dst_param.width = dstWidth;
    dst_param.height = dstHeight;
    dst_param.format = dstFmt;

    if (use_handle) {
        if (src_phy != NULL) {
            rga_handle_src = importbuffer_physicaladdr((uint64_t)src_phy, &in_param);
        } else if (src_fd > 0) {
            rga_handle_src = importbuffer_fd(src_fd, &in_param);
        } else {
            rga_handle_src = importbuffer_virtualaddr(src, &in_param);
        }
        if (rga_handle_src <= 0) {
            printf("src handle error %d\n", rga_handle_src);
            ret = -1;
            goto err;
        }
        rga_buf_src = wrapbuffer_handle(rga_handle_src, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
    } else {
        if (src_phy != NULL) {
            rga_buf_src = wrapbuffer_physicaladdr(src_phy, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        } else if (src_fd > 0) {
            rga_buf_src = wrapbuffer_fd(src_fd, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        } else {
            rga_buf_src = wrapbuffer_virtualaddr(src, srcWidth, srcHeight, srcFmt, srcWidth, srcHeight);
        }
    }

    if (use_handle) {
        if (dst_phy != NULL) {
            rga_handle_dst = importbuffer_physicaladdr((uint64_t)dst_phy, &dst_param);
        } else if (dst_fd > 0) {
            rga_handle_dst = importbuffer_fd(dst_fd, &dst_param);
        } else {
            rga_handle_dst = importbuffer_virtualaddr(dst, &dst_param);
        }
        if (rga_handle_dst <= 0) {
            printf("dst handle error %d\n", rga_handle_dst);
            ret = -1;
            goto err;
        }
        rga_buf_dst = wrapbuffer_handle(rga_handle_dst, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
    } else {
        if (dst_phy != NULL) {
            rga_buf_dst = wrapbuffer_physicaladdr(dst_phy, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        } else if (dst_fd > 0) {
            rga_buf_dst = wrapbuffer_fd(dst_fd, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        } else {
            rga_buf_dst = wrapbuffer_virtualaddr(dst, dstWidth, dstHeight, dstFmt, dstWidth, dstHeight);
        }
    }

    if (drect.width != dstWidth || drect.height != dstHeight) {
        im_rect dst_whole_rect = {0, 0, dstWidth, dstHeight};
        if (dst != NULL && dst_fd <= 0) {
            // 普通 cacheable 内存直接填充，避免虚拟地址目标上的 imfill 兼容性问题。
            size_t dst_size = get_image_size(dst_img);
            memset(dst, color, dst_size);
        } else {
            // zero-copy dma-buf 由 RGA 填充，避免 CPU 写 non-cacheable 输入内存。
            int imcolor;
            char* p_imcolor = (char*)&imcolor;
            p_imcolor[0] = color;
            p_imcolor[1] = color;
            p_imcolor[2] = color;
            p_imcolor[3] = color;
            ret_rga = imfill(rga_buf_dst, dst_whole_rect, imcolor);
            if (ret_rga <= 0) {
                printf("Warning: Can not fill color on target image\n");
                ret = -1;
                goto err;
            }
        }
    }

    // rga process
    ret_rga = improcess(rga_buf_src, rga_buf_dst, pat, srect, drect, prect, usage);
    if (ret_rga <= 0) {
        printf("Error on improcess STATUS=%d\n", ret_rga);
        printf("RGA error message: %s\n", imStrError((IM_STATUS)ret_rga));
        ret = -1;
    }

err:
    if (rga_handle_src > 0) {
        releasebuffer_handle(rga_handle_src);
    }

    if (rga_handle_dst > 0) {
        releasebuffer_handle(rga_handle_dst);
    }

    // printf("finish\n");
    return ret;
}

int convert_image(image_buffer_t* src_img, image_buffer_t* dst_img, image_rect_t* src_box, image_rect_t* dst_box, char color)
{
    int ret;
#if defined(DISABLE_RGA) 
    printf("convert image use cpu\n");
    ret = convert_image_cpu(src_img, dst_img, src_box, dst_box, color);
#else

#if defined(RV1106_1103) 
    if(src_img->width % 4 == 0 && dst_img->width % 4 == 0) {
#else
    if(src_img->width % 16 == 0 && dst_img->width % 16 == 0) {
#endif
        ret = convert_image_rga(src_img, dst_img, src_box, dst_box, color);
        if (ret != 0) {
            printf("try convert image use cpu\n");
            ret = convert_image_cpu(src_img, dst_img, src_box, dst_box, color);
        }
    } else {
        printf("src width is not 4/16-aligned, convert image use cpu\n");
        ret = convert_image_cpu(src_img, dst_img, src_box, dst_box, color);
    }
#endif
    return ret;
}

int convert_image_with_letterbox(image_buffer_t* src_image, image_buffer_t* dst_image, letterbox_t* letterbox, char color)
{
    if (src_image == NULL || dst_image == NULL ||
        src_image->width <= 0 || src_image->height <= 0 ||
        dst_image->width <= 0 || dst_image->height <= 0) {
        return -1;
    }

    int ret = 0;
    int allow_slight_change = 1;
    int src_w = src_image->width;
    int src_h = src_image->height;
    int dst_w = dst_image->width;
    int dst_h = dst_image->height;
    int resize_w = dst_w;
    int resize_h = dst_h;

    int padding_w = 0;
    int padding_h = 0;

    int _left_offset = 0;
    int _top_offset = 0;
    float scale = 1.0;

    image_rect_t src_box;
    src_box.left = 0;
    src_box.top = 0;
    src_box.right = src_image->width - 1;
    src_box.bottom = src_image->height - 1;

    image_rect_t dst_box;
    dst_box.left = 0;
    dst_box.top = 0;
    dst_box.right = dst_image->width - 1;
    dst_box.bottom = dst_image->height - 1;

    float _scale_w = (float)dst_w / src_w;
    float _scale_h = (float)dst_h / src_h;
    if(_scale_w < _scale_h) {
        scale = _scale_w;
        resize_h = (int) src_h*scale;
    } else {
        scale = _scale_h;
        resize_w = (int) src_w*scale;
    }
    // slight change image size for align
    if (allow_slight_change == 1 && (resize_w % 4 != 0)) {
        resize_w -= resize_w % 4;
    }
    if (allow_slight_change == 1 && (resize_h % 2 != 0)) {
        resize_h -= resize_h % 2;
    }
    // Alignment and truncation must never discard the entire source image.
    if (resize_w < 1) {
        resize_w = 1;
    }
    if (resize_h < 1) {
        resize_h = 1;
    }
    if (resize_w > dst_w || resize_h > dst_h) {
        printf("invalid letterbox resize: %dx%d -> %dx%d\n", src_w, src_h, resize_w, resize_h);
        return -1;
    }
    // padding
    padding_h = dst_h - resize_h;
    padding_w = dst_w - resize_w;
    // center
    if (_scale_w < _scale_h) {
        dst_box.top = padding_h / 2;
        if (dst_box.top % 2 != 0) {
            dst_box.top -= dst_box.top % 2;
            if (dst_box.top < 0) {
                dst_box.top = 0;
            }
        }
        dst_box.bottom = dst_box.top + resize_h - 1;
        _top_offset = dst_box.top;
    } else {
        dst_box.left = padding_w / 2;
        if (dst_box.left % 2 != 0) {
            dst_box.left -= dst_box.left % 2;
            if (dst_box.left < 0) {
                dst_box.left = 0;
            }
        }
        dst_box.right = dst_box.left + resize_w - 1;
        _left_offset = dst_box.left;
    }
    if (dst_box.left < 0 || dst_box.top < 0 ||
        dst_box.right < dst_box.left || dst_box.bottom < dst_box.top ||
        dst_box.right >= dst_w || dst_box.bottom >= dst_h) {
        printf("invalid letterbox destination box: (%d %d %d %d), dst=%dx%d\n",
               dst_box.left, dst_box.top, dst_box.right, dst_box.bottom, dst_w, dst_h);
        return -1;
    }
    printf("scale=%f dst_box=(%d %d %d %d) allow_slight_change=%d _left_offset=%d _top_offset=%d padding_w=%d padding_h=%d\n",
        scale, dst_box.left, dst_box.top, dst_box.right, dst_box.bottom, allow_slight_change,
        _left_offset, _top_offset, padding_w, padding_h);

    //set offset and scale
    if(letterbox != NULL){
        letterbox->scale = scale;
        letterbox->x_pad = _left_offset;
        letterbox->y_pad = _top_offset;
    }
    // alloc memory buffer for dst image,
    // remember to free
    if (dst_image->virt_addr == NULL && dst_image->fd <= 0) {
        int dst_size = get_image_size(dst_image);
        dst_image->virt_addr = (uint8_t *)malloc(dst_size);
        if (dst_image->virt_addr == NULL) {
            printf("malloc size %d error\n", dst_size);
            return -1;
        }
    }
    ret = convert_image(src_image, dst_image, &src_box, &dst_box, color);
    return ret;
}