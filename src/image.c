#include <math.h>
#include <stddef.h>
#include <stdint.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include "image.h"

#define IMG_AT(img, y, x, c)                                                   \
    (img).data[(y) * (img).width * (img).channels + (x) * (img).channels + (c)]

static inline void log_error(const char *func, const char *msg) {
    fprintf(stderr, "\033[31mError %s: %s\n\033[0m", func, msg);
}

// https://stackoverflow.com/questions/596216/formula-to-determine-perceived-brightness-of-rgb-color
static inline uint8_t rgb_to_lum(uint8_t r, uint8_t g, uint8_t b) {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

static inline bool is_index_valid(const Image *img, int row, int col) {
    return 0 <= row && row < img->height && 0 <= col && col < img->width;
}

static inline uint8_t clip(float val) {
    return (uint8_t)((val < 0) ? 0 : (val > 255 ? 255 : val));
}

static const float Gx[3][3] = {
    {-1, 0, 1},
    {-2, 0, 2},
    {-1, 0, 1},
};

static const float Gy[3][3] = {
    {-1, -2, -1},
    {0, 0, 0},
    {1, 2, 1},
};

bool img_load(Image *img, const char *filename) {
    img->data =
        stbi_load(filename, &img->width, &img->height, &img->channels, 0);
    if (img->data) {
        img->size = img->width * img->height * img->channels;
        img->alloc = STB_ALLOCATION;
        return true;
    }
    img->alloc = NO_ALLOCATION;
    return false;
}

void img_free(Image *img) {
    if (img && img->data) {
        switch (img->alloc) {
        case STB_ALLOCATION:
            stbi_image_free(img->data);
            break;

        case USER_ALLOCATION:
            free(img->data);
            break;

        default:
            log_error(__func__, "image is not allocated");
            break;
        }

        img->width = img->height = img->channels = img->size = 0;
        img->data = NULL;
        img->alloc = NO_ALLOCATION;
    }
}

Image img_allocate(int w, int h, int c) {
    Image img = {.width = w,
                 .height = h,
                 .channels = c,
                 .size = w * h * c,
                 .data = calloc((size_t)w * h * c, sizeof *img.data),
                 .alloc = USER_ALLOCATION};
    if (img.data) {
        return img;
    }

    return (Image){.alloc = NO_ALLOCATION};
}

// allocate the space for new image only if
// img provided contains data and is valid
Image img_allocate_like(const Image *img) {
    if (img && img->data && img->width > 0 && img->height > 0 &&
        img->channels > 0) {
        return img_allocate(img->width, img->height, img->channels);
    }
    return (Image){};
}

Image img_copy(const Image *img) {
    Image out = img_allocate_like(img);
    if (!out.data) {
        log_error(__func__, "Memory allcation failed");
        return out;
    }
    memcpy(out.data, img->data, img->size);
    return out;
}

void img_copy_to(const Image *img, Image *out) {
    if (img && out && img->data && out->data && img->size == out->size) {
        memcpy(out->data, img->data, img->size);
    } else {
        log_error(__func__, "empty image or size mismatch");
    }
}

Image img_luminance(const Image *img) {
    const int32_t channels_req =
        (img->channels == 4 || img->channels == 2) ? 2 : 1;
    Image out = img_allocate(img->width, img->height, channels_req);
    if (!out.data) {
        log_error(__func__, "Memory allocation failed");
        return out;
    }

    switch (img->channels) {
    case 1:
    case 2:
        img_copy_to(img, &out);
        break;

    case 3:
        for (int i = 0, j = 0; i < img->size; i += img->channels) {
            out.data[j++] =
                rgb_to_lum(img->data[i], img->data[i + 1], img->data[i + 2]);
        }
        break;

    case 4:
        for (int i = 0, j = 0; i < img->size; i += img->channels) {
            out.data[j++] =
                rgb_to_lum(img->data[i], img->data[i + 1], img->data[i + 2]);
            out.data[j++] = img->data[i + 3]; // copy the alpha channel
        }
        break;

    default:
        log_error(__func__, "invalid channels");
        img_free(&out);
    }

    return out;
}

static inline float convolve_pixel(const Image *img, int y, int x, int c,
                                   const float k[3][3]) {
    float val = 0;
    for (int dy = -1; dy < 2; ++dy) {
        for (int dx = -1; dx < 2; ++dx) {
            int row = y + dy;
            int col = x + dx;
            if (is_index_valid(img, row, col)) {
                val += IMG_AT(*img, row, col, c) * k[dy + 1][dx + 1];
            }
        }
    }

    return val;
}

Image img_convolve(const Image *img, const float k[3][3]) {
    Image out = img_allocate_like(img);
    if (!out.data) {
        log_error(__func__, "Memory allocation failed");
        return out;
    }

    if (out.channels == 1) {
        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                IMG_AT(out, y, x, 0) = clip(convolve_pixel(img, y, x, 0, k));
            }
        }
    } else if (out.channels == 2) {
        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                IMG_AT(out, y, x, 0) = clip(convolve_pixel(img, y, x, 0, k));
                // copy the alpha channel
                IMG_AT(out, y, x, 1) = IMG_AT(*img, y, x, 1);
            }
        }
    } else if (out.channels == 3) {
        for (int y = 0; y < out.height; ++y) {
            [[maybe_unused]] uint8_t *row_ptr =
                out.data + y * out.width * out.channels;
            for (int x = 0; x < out.width; ++x) {
                IMG_AT(out, y, x, 0) = clip(convolve_pixel(img, y, x, 0, k));
                IMG_AT(out, y, x, 1) = clip(convolve_pixel(img, y, x, 1, k));
                IMG_AT(out, y, x, 2) = clip(convolve_pixel(img, y, x, 2, k));
            }
        }
    } else if (out.channels == 4) {
        for (int y = 0; y < out.height; ++y) {
            for (int x = 0; x < out.width; ++x) {
                IMG_AT(out, y, x, 0) = clip(convolve_pixel(img, y, x, 0, k));
                IMG_AT(out, y, x, 1) = clip(convolve_pixel(img, y, x, 1, k));
                IMG_AT(out, y, x, 2) = clip(convolve_pixel(img, y, x, 2, k));
                // copy the alpha channel
                IMG_AT(out, y, x, 3) = IMG_AT(*img, y, x, 3);
            }
        }
    } else {
        log_error(__func__, "invalid channels");
        img_free(&out);
        return out;
    }

    return out;
}

Image img_sobel_filter(const Image *img) {
    Image out = img_allocate_like(img);
    if (!out.data) {
        log_error(__func__, "memory allocation failed");
        return out;
    }

    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            if (out.channels == 1) {
                float grad_x = convolve_pixel(img, y, x, 0, Gx);
                float grad_y = convolve_pixel(img, y, x, 0, Gy);
                IMG_AT(out, y, x, 0) =
                    clip(sqrt(grad_x * grad_x + grad_y * grad_y));
            } else if (out.channels == 2) {
                float grad_x = convolve_pixel(img, y, x, 0, Gx);
                float grad_y = convolve_pixel(img, y, x, 0, Gy);
                IMG_AT(out, y, x, 0) =
                    clip(sqrt(grad_x * grad_x + grad_y * grad_y));
                IMG_AT(out, y, x, 1) =
                    IMG_AT(*img, y, x, 1); // copy the alpha channel
            } else if (out.channels == 3) {
                for (int c = 0; c < 3; ++c) {
                    float grad_x = convolve_pixel(img, y, x, c, Gx);
                    float grad_y = convolve_pixel(img, y, x, c, Gy);
                    IMG_AT(out, y, x, c) =
                        clip(sqrt(grad_x * grad_x + grad_y * grad_y));
                }
            } else if (out.channels == 4) {
                for (int c = 0; c < 3; ++c) {
                    float grad_x = convolve_pixel(img, y, x, c, Gx);
                    float grad_y = convolve_pixel(img, y, x, c, Gy);
                    IMG_AT(out, y, x, c) =
                        clip(sqrt(grad_x * grad_x + grad_y * grad_y));
                }
                IMG_AT(out, y, x, 3) =
                    IMG_AT(*img, y, x, 3); // copy the alpha channel
            } else {
                log_error(__func__, "invalid channels");
                img_free(&out);
                return out;
            }
        }
    }

    return out;
}

Image img_convolve_fast(const Image *img, const float k[3][3]) {
    Image out = img_allocate_like(img);
    if (!out.data) {
        log_error(__func__, "Memory allocation failed");
        return out;
    }

    const int height = img->height;
    const int width = img->width;
    const int channels = img->channels;
    const size_t row_stride = width * channels;

    // Flatten kernel for better locality
    const float k0 = k[0][0], k1 = k[0][1], k2 = k[0][2];
    const float k3 = k[1][0], k4 = k[1][1], k5 = k[1][2];
    const float k6 = k[2][0], k7 = k[2][1], k8 = k[2][2];

    // Process inner pixels (assumes 1-pixel padded input)
    for (int y = 1; y < height - 1; y++) {
        const uint8_t *prev_row = img->data + (y - 1) * row_stride;
        const uint8_t *curr_row = img->data + y * row_stride;
        const uint8_t *next_row = img->data + (y + 1) * row_stride;
        uint8_t *out_row = out.data + y * row_stride;

        for (int x = 1; x < width - 1; x++) {
            const int px_offset = x * channels;

            // Unrolled kernel application
            for (int c = 0; c < channels; c++) {
                const uint8_t *p = prev_row + px_offset - channels + c;
                const uint8_t *q = curr_row + px_offset - channels + c;
                const uint8_t *r = next_row + px_offset - channels + c;

                float accum =
                    p[0] * k0 + p[channels] * k1 + p[2 * channels] * k2 +
                    q[0] * k3 + q[channels] * k4 + q[2 * channels] * k5 +
                    r[0] * k6 + r[channels] * k7 + r[2 * channels] * k8;

                // Branchless clipping to [0, 255]
                accum = accum < 0 ? 0 : (accum > 255 ? 255 : accum);
                out_row[px_offset + c] = (uint8_t)accum;
            }

            // Preserve alpha channel if present
            if (channels == 4) {
                out_row[px_offset + 3] = curr_row[px_offset + 3];
            }
        }
    }

    return out;
}

void img_write_png(const Image *img, const char *filename) {
    if (img && img->data) {
        stbi_write_png(filename, img->width, img->height, img->channels,
                       img->data, img->width * img->channels);
        printf("\033[32mImage '%s' saved sucessfully\033[0m\n", filename);
    } else {
        log_error(__func__, "empty image given");
    }
}

void img_show_summary(const Image *img) {
    if (img) {
        printf("Width: %i, Height: %i, Channels: %i, Size: %zu bytes\n",
               img->width, img->height, img->channels, img->size);
        if (!img->data)
            fprintf(stderr, "Warning: No image is loaded\n");
    } else {
        log_error(__func__, "NULL image given");
    }
}