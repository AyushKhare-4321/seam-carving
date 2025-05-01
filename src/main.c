#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/image.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define IMG_AT(img, r, c) (img).data[(r) * (img).width * (img).channels + (c)]

[[nodiscard]] Image sobel_filter(const Image *img);

[[maybe_unused]]
static inline void log_error(const char *func, const char *msg) {
    fprintf(stderr, "\033[31mError %s: %s\n\033[0m", func, msg);
}

static inline energy_type min2(energy_type a, energy_type b) {
    return a < b ? a : b;
}

static inline int min3(energy_type a, energy_type b, energy_type c) {
    return (a < b) ? ((a < c) ? a : c) : ((b < c) ? b : c);
}

Matrix mat_copy(const Matrix);

Matrix energy_map(const Image *img) {
    if (!img || !img->data || img->channels != 1) {
        printf("Energy map requires a valid single-channel gradient image\n");
        return (Matrix){};
    }

    energy_type *const energy_arr =
        calloc(img->width * img->height, sizeof(*energy_arr));
    if (!energy_arr) {
        log_error(__func__, "Failed to allocate energy buffer");
        return (Matrix){};
    }

    // copy the whole image values
    [[maybe_unused]] const int row_stride = img->width * img->channels;

    // Initialize the first row
    for (int x = 0; x < img->width; ++x) {
        energy_arr[x] = (energy_type)(img->data[x]);
    }

    for (int row = 1; row < img->height; ++row) {
        const uint8_t *in_row = img->data + row_stride * row;
        energy_type *out_current_row = energy_arr + row * row_stride;
        energy_type *out_prev_row = out_current_row - row_stride;

        // Leftmost column
        out_current_row[0] = min2(out_prev_row[0], out_prev_row[1]) + in_row[0];

        // Middle columns
        for (int col = 1; col < img->width - 1; ++col) {
            out_current_row[col] =
                min3(out_prev_row[col - 1], out_prev_row[col],
                     out_prev_row[col + 1]) +
                in_row[col];
        }

        // Rightmost column
        out_current_row[img->width - 1] =
            min2(out_prev_row[img->width - 1], out_prev_row[img->width - 2]) +
            in_row[img->width - 1];
    }

    return (Matrix){
        .width = img->width, .height = img->height, .data = energy_arr};
}

void save_energy_map(const char *filename, const energy_type *map, int width, int height) {
    if (!map) {
        log_error(__func__, "energy map can't be null");
        return;
    }
    Image out = img_allocate(width, height, 1);
    if (!out.data) {
        log_error(__func__, "memory allocation failed");
        return;
    }

    // Noralize the image
    energy_type min_val = INT32_MAX, max_val = INT32_MIN;
    for (int i = 0; i < width * height; ++i) {
        if (map[i] < min_val)
            min_val = map[i];
        if (map[i] > max_val)
            max_val = map[i];
    }

    float range = (float)(max_val - min_val);
    if (range == 0)
        range = 1;

    for (int i = 0; i < out.size; ++i) {
        float val = ((map[i] - min_val) / range * 255.0f);
        out.data[i] = (u8)val;
    }
    img_write_png(&out, filename);
}

void matrix_free(Matrix *m) {
    if (m && m->data) {
        free(m->data);
        m->data = NULL;
        m->height = m->width = 0;
    } else {
        log_error(__func__, "empty matrix given");
    }
}

#define mat_at(mat, y, x) (mat).data[(y) * (mat).width + (x)]

// remove the pixel horizontally
void shift_pixels_img(Image img, int row, int col) {
    for (int x = col; x < (img.width - 1) * img.channels; ++x) {
        IMG_AT(img, row, x) = IMG_AT(img, row, x + img.channels);
    }
}

// remove the pixel horizontally (for 3 channel)
void shift_pixels_img3(Image img, int row, int col) {
    assert(img.channels == 3 && "This function is for 3-channel images");
    for (int x = col; x < img.width - 1; ++x) {
        for (int c = 0; c < img.channels; ++c)
            img.data[row * img.width * img.channels + x * img.channels + c] =
                img.data[row * img.width * img.channels +
                         (x + 1) * img.channels + c];
    }
}

void shift_pixels_mat(Matrix m, int row, int col) {
    for (int x = col; x < m.width - 1; ++x) {
        mat_at(m, row, x) = mat_at(m, row, x + 1);
    }
}

void remove_seam(Matrix *map, Image *img, int *current_width) {
    assert(img->width == map->width && img->height == map->height);

    // Find starting column of the seam in the bottom row and remove it
    int min_col_idx = 0;
    int min_val = mat_at(*map, map->height - 1, min_col_idx);
    for (int x = 0; x < *current_width; ++x) {
        if (min_val > mat_at(*map, map->height - 1, x)) {
            min_col_idx = x;
            min_val = mat_at(*map, map->height - 1, x);
        }
    }
    if (img->channels == 1)
        shift_pixels_img(*img, map->height - 1, min_col_idx);
    else if (img->channels == 3)
        shift_pixels_img3(*img, map->height - 1, min_col_idx);
    shift_pixels_mat(*map, map->height - 1, min_col_idx);

    // Remove the seam from bottom to top
    for (int y = img->height - 2; y >= 0; --y) {
        const int center_min_col = min_col_idx;
        min_val = mat_at(*map, y, center_min_col);
        if (center_min_col > 0 &&
            min_val > mat_at(*map, y, center_min_col - 1)) {
            min_val = mat_at(*map, y, center_min_col - 1);
            min_col_idx = center_min_col - 1;
        }

        if (center_min_col < *current_width - 1 &&
            min_val > mat_at(*map, y, center_min_col + 1)) {
            min_val = mat_at(*map, y, center_min_col + 1);
            min_col_idx = center_min_col + 1;
        }

        if (img->channels == 1)
            shift_pixels_img(*img, y, min_col_idx);
        else if (img->channels == 3)
            shift_pixels_img3(*img, y, min_col_idx);
        shift_pixels_mat(*map, y, min_col_idx);
    }

    (*current_width)--;
}

Image get_seam_img(const Image *img, const Matrix *mat, int target_width) {
    // Copy the original image and energy map
    Image new_img = img_copy(img);
    Matrix new_mat = mat_copy(*mat);
    assert(new_img.data);
    assert(new_mat.data);

    int current_width = new_img.width;

    while (current_width > target_width) {
        remove_seam(&new_mat, &new_img, &current_width);
    }

    Image out = img_allocate(target_width, new_img.height, new_img.channels);
    assert(out.data);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width * out.channels; ++x) {
            IMG_AT(out, y, x) = IMG_AT(new_img, y, x);
        }
    }

    img_free(&new_img);

    return out;
}

Matrix mat_copy(const Matrix m) {
    Matrix out = {.width = m.width,
                  .height = m.height,
                  .data = malloc(m.width * m.height * sizeof(m.data[0]))};
    if (!out.data) {
        return (Matrix){};
    }
    for (int i = 0; i < m.height * m.width; ++i) {
        out.data[i] = m.data[i];
    }
    return out;
}

void flush_input(void) {
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF)
        continue;
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <image path>\n", argv[0]);
        return -1;
    }

    // Loading the image
    Image img = {};
    const char *filename = argv[1];
    if (!img_load(&img, filename)) {
        fprintf(stderr, "Error: failed to load image\n");
        return 1;
    }
    img_show_summary(&img);

    int target_width = 0;
    while (true) {
        printf("Final width of the image: ");
        if (scanf("%i", &target_width) == 1) {
            if (target_width > 0 && target_width < img.width) {
                break;
            }
            fprintf(stderr,
                    "\033[31mTarget width must be grater than 0 and less than "
                    "%i\033[0m\n",
                    img.width);
        }
        flush_input();
    }

    // Applying the luminance
    Image lum = img_luminance(&img);
    if (lum.data) {
        img_write_png(&lum, "images/lum.png");
    }

    // Applying blur filter
    Image blurred_img = img_convolve_default(&lum, KERNEL_GAUSSIAN_BLUR);
    if (blurred_img.data) {
        img_write_png(&blurred_img, "images/blurred.png");
    }

    // Applying sobel filter
    Image sobel = img_sobel_filter(&blurred_img);
    if (sobel.data) {
        img_write_png(&sobel, "images/sobel.png");
    }

    // Calculate the image energy
    Matrix energy = energy_map(&sobel);
    save_energy_map("images/energy_map.png", energy.data, energy.width, energy.height);

    Image seam = get_seam_img(&img, &energy, target_width);
    if (seam.data) {
        img_write_png(&seam, "images/out.png");
        img_free(&seam);
    }

    img_free(&img);
    matrix_free(&energy);
    img_free(&lum);
    img_free(&blurred_img);
    img_free(&sobel);

    return 0;
}
