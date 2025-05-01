#pragma once
#ifndef MY_IMAGE_LIB_H_
#define MY_IMAGE_LIB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum allocation_type { NO_ALLOCATION, STB_ALLOCATION, USER_ALLOCATION };

static const float KERNEL_BOX_BLUR[3][3] = {
    {1.0 / 9.0, 1.0 / 9.0, 1.0 / 9.0},
    {1.0 / 9.0, 1.0 / 9.0, 1.0 / 9.0},
    {1.0 / 9.0, 1.0 / 9.0, 1.0 / 9.0},
};

static const float KERNEL_SHARPEN[3][3] = {
    {0, -1, 0},
    {-1, 5, -1},
    {0, -1, 0},
};

static const float KERNEL_GAUSSIAN_BLUR[3][3] = {
    {1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0},
    {1.0 / 8.0, 1.0 / 4.0, 1.0 / 8.0},
    {1.0 / 16.0, 1.0 / 8.0, 1.0 / 16.0},
};

typedef struct {
    int width;
    int height;
    int channels;
    size_t size;
    uint8_t *data;
    enum allocation_type alloc;
} Image;

typedef int32_t energy_type;

typedef struct {
    int width;
    int height;
    energy_type *data;
} Matrix;

/**
 * @brief Load an image from file using stb_image.
 *
 * @param[out] img    Pointer to an Image struct that will be initialized.
 * @param[in]  filename Path to the image file.
 * @return true on success, false on failure.
 */
bool img_load(Image *img, const char *filename);

void img_free(Image *img);

void img_show_summary(const Image *img);

[[nodiscard]] Image img_allocate(int w, int h, int c);

[[nodiscard]] Image img_allocate_like(const Image *img);

[[nodiscard]] Image img_copy(const Image *img);

void img_copy_to(const Image *img, Image *out);

[[nodiscard]] Image img_luminance(const Image *img);

void img_write_png(const Image *img, const char *filename);

[[nodiscard]] Image img_convolve(const Image *img, const float kernel[3][3]);

[[nodiscard]] Image img_sobel_filter(const Image *img);

[[nodiscard]] Image img_convolve_fast(const Image *img, const float k[3][3]);

#define img_convolve_default img_convolve

#endif