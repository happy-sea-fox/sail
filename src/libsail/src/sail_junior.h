/*  This file is part of SAIL (https://github.com/smoked-herring/sail)

    Copyright (c) 2020 Dmitry Baryshev

    The MIT License

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#ifndef SAIL_SAIL_JUNIOR_H
#define SAIL_SAIL_JUNIOR_H

#ifdef SAIL_BUILD
    #include "error.h"
    #include "export.h"
#else
    #include <sail-common/error.h>
    #include <sail-common/export.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct sail_image;
struct sail_io;
struct sail_plugin_info;

/*
 * Loads the specified image file and returns its properties without pixels. The assigned image
 * MUST be destroyed later with sail_destroy_image(). The assigned plugin info MUST NOT be destroyed
 * because it is a pointer to an internal data structure. If you don't need it, just pass NULL.
 *
 * This function is pretty fast because it doesn't decode whole image data for most image formats.
 *
 * Typical usage: This is a standalone function that could be called at any time.
 *
 * Returns 0 on success or sail_error_t on error.
 */
SAIL_EXPORT sail_error_t sail_probe_file(const char *path, struct sail_image **image, const struct sail_plugin_info **plugin_info);

/*
 * Loads the specified image file and returns its properties and pixels. The assigned image
 * MUST be destroyed later with sail_destroy_image().
 *
 * Outputs pixels in the BPP32-RGBA pixel format.
 *
 * Typical usage: This is a standalone function that could be called at any time.
 *
 * Returns 0 on success or sail_error_t on error.
 */
SAIL_EXPORT sail_error_t sail_read_file(const char *path, struct sail_image **image);

/*
 * Loads the specified image file from the specified memory buffer and returns its properties and pixels.
 * The assigned image MUST be destroyed later with sail_destroy_image().
 *
 * Outputs pixels in the BPP32-RGBA pixel format.
 *
 * Typical usage: This is a standalone function that could be called at any time.
 *
 * Returns 0 on success or sail_error_t on error.
 */
SAIL_EXPORT sail_error_t sail_read_mem(const void *buffer, size_t buffer_length, struct sail_image **image);

/*
 * Writes the pixels of the specified image file into the file.
 *
 * Outputs pixels in the pixel format as specified in sail_write_features.default_output_pixel_format.
 *
 * Typical usage: This is a standalone function that could be called at any time.
 *
 * Returns 0 on success or sail_error_t on error.
 */
SAIL_EXPORT sail_error_t sail_write_file(const char *path, const struct sail_image *image);

/*
 * Writes the pixels of the specified image file into the specified memory buffer.
 *
 * Outputs pixels in the pixel format as specified in sail_write_features.default_output_pixel_format.
 *
 * Saves the number of bytes written into the 'written' parameter if it's not NULL.
 *
 * Typical usage: This is a standalone function that could be called at any time.
 *
 * Returns 0 on success or sail_error_t on error.
 */
SAIL_EXPORT sail_error_t sail_write_mem(void *buffer, size_t buffer_length, const struct sail_image *image, size_t *written);

/* extern "C" */
#ifdef __cplusplus
}
#endif

#endif
