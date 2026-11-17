/* ============================================================
 * image_loader.c — YXG 图片加载运行时 (stb_image 封装)
 *
 * 编译: gcc -m32 -O2 -DSTBI_NO_SIMD -c image_loader.c -o image_loader.obj
 *
 * 导出 (cdecl):
 *   int  image_load(const char* path, int* w, int* h,
 *                   int* bpp, unsigned char** pixels);
 *       成功返回 1, 像素为 RGBA8、自顶向下、紧密排列 (stride = w*4);
 *       失败返回 0。
 *   void image_free(unsigned char* pixels);
 * ============================================================ */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int image_load(const char* path, int* w, int* h, int* bpp,
               unsigned char** pixels)
{
    if (!path || !w || !h || !bpp || !pixels) return 0;

    int x = 0, y = 0, channels_in_file = 0;
    unsigned char* data = stbi_load(path, &x, &y, &channels_in_file, 4);
    if (!data) {
        fprintf(stderr, "[stbi] failed: %s\n", stbi_failure_reason());
        return 0;
    }
    if (x <= 0 || y <= 0) {
        stbi_image_free(data);
        return 0;
    }

    *w = x;
    *h = y;
    *bpp = 32;
    *pixels = data;
    return 1;
}

void image_free(unsigned char* pixels)
{
    if (pixels) stbi_image_free(pixels);
}