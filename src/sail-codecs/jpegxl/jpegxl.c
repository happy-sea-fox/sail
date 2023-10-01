/*  This file is part of SAIL (https://github.com/HappySeaFox/sail)

    Copyright (c) 2023 Dmitry Baryshev

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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <jxl/decode.h>

#include "sail-common.h"

#include "helpers.h"
#include "memory.h"

/*
 * Codec-specific state.
 */
struct jpegxl_state {
    struct sail_io *io;
    struct sail_load_options *load_options;
    struct sail_save_options *save_options;

    bool frame_loaded;

    void *image_data;
    JxlMemoryManager *memory_manager;
    void *runner;
    JxlDecoder *decoder;
};

static sail_status_t alloc_jpegxl_state(struct jpegxl_state **jpegxl_state) {

    void *ptr;
    SAIL_TRY(sail_malloc(sizeof(struct jpegxl_state), &ptr));
    *jpegxl_state = ptr;

    (*jpegxl_state)->io           = NULL;
    (*jpegxl_state)->load_options = NULL;
    (*jpegxl_state)->save_options = NULL;

    (*jpegxl_state)->frame_loaded = false;

    (*jpegxl_state)->image_data     = NULL;
    (*jpegxl_state)->memory_manager = NULL;
    (*jpegxl_state)->runner         = NULL;
    (*jpegxl_state)->decoder        = NULL;

    return SAIL_OK;
}

static void destroy_jpegxl_state(struct jpegxl_state *jpegxl_state) {

    if (jpegxl_state == NULL) {
        return;
    }

    sail_destroy_load_options(jpegxl_state->load_options);
    sail_destroy_save_options(jpegxl_state->save_options);

    sail_free(jpegxl_state->image_data);
    sail_free(jpegxl_state->memory_manager);

    // TODO
    //JxlResizableParallelRunnerDestroy(jpegxl_state->runner);
    JxlDecoderDestroy(jpegxl_state->decoder);

    sail_free(jpegxl_state);
}

/*
 * Decoding functions.
 */

SAIL_EXPORT sail_status_t sail_codec_load_init_v8_jpegxl(struct sail_io *io, const struct sail_load_options *load_options, void **state) {

    *state = NULL;

    /* Allocate a new state. */
    struct jpegxl_state *jpegxl_state;
    SAIL_TRY(alloc_jpegxl_state(&jpegxl_state));
    *state = jpegxl_state;

    /* Save I/O for further operations. */
    jpegxl_state->io = io;

    /* Deep copy load options. */
    SAIL_TRY(sail_copy_load_options(load_options, &jpegxl_state->load_options));

    /* Init decoder. */
    void *ptr;
    SAIL_TRY(sail_malloc(sizeof(JxlMemoryManager), &ptr));
    jpegxl_state->memory_manager = ptr;
    jpegxl_state->memory_manager->opaque = NULL,
    jpegxl_state->memory_manager->alloc  = jpegxl_private_alloc_func,
    jpegxl_state->memory_manager->free   = jpegxl_private_free_func,

    // TODO
    // jpegxl_state->runner = JxlResizableParallelRunnerCreate(jpegxl_state->memory_manager);

    jpegxl_state->decoder = JxlDecoderCreate(jpegxl_state->memory_manager);

    if (JxlDecoderSubscribeEvents(jpegxl_state->decoder, JXL_DEC_BASIC_INFO
                                                            | JXL_DEC_COLOR_ENCODING
                                                            | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
        SAIL_LOG_ERROR("JPEGXL: Failed to subscribe to decoder events");
        SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
    }

    // TODO
    //if (JxlDecoderSetParallelRunner(jpegxl_state->decoder,
    //                                JxlResizableParallelRunner,
    //                                jpegxl_state->runner) != JXL_DEC_SUCCESS) {
    //    SAIL_LOG_ERROR("JPEGXL: Failed to set a parallel runner");
    //    SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
    //}

    /* Read the entire image to use the JasPer memory API. */
    size_t image_size;
    SAIL_TRY(sail_alloc_data_from_io_contents(io, &jpegxl_state->image_data, &image_size));

    JxlDecoderSetInput(jpegxl_state->decoder, jpegxl_state->image_data, image_size);
    JxlDecoderCloseInput(jpegxl_state->decoder);

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_load_seek_next_frame_v8_jpegxl(void *state, struct sail_image **image) {

    struct jpegxl_state *jpegxl_state = state;

    if (jpegxl_state->frame_loaded) {
        SAIL_LOG_AND_RETURN(SAIL_ERROR_NO_MORE_FRAMES);
    }

    jpegxl_state->frame_loaded = true;

    struct sail_image *image_local;

    SAIL_TRY(sail_alloc_image(&image_local));
    SAIL_TRY_OR_CLEANUP(sail_alloc_source_image(&image_local->source_image),
                        /* cleanup */ sail_destroy_image(image_local));

    bool done = false;

    while (!done) {
        JxlDecoderStatus status = JxlDecoderProcessInput(jpegxl_state->decoder);

        switch (status) {
            case JXL_DEC_ERROR: {
                sail_destroy_image(image_local);
                SAIL_LOG_ERROR("JPEGXL: Unknown decoder error");
                SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
            }
            case JXL_DEC_NEED_MORE_INPUT: {
                sail_destroy_image(image_local);
                SAIL_LOG_ERROR("JPEGXL: For unknown reason decoder needs more input");
                SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
            }
            case JXL_DEC_BASIC_INFO: {
                JxlBasicInfo info;

                if (JxlDecoderGetBasicInfo(jpegxl_state->decoder, &info) != JXL_DEC_SUCCESS) {
                    sail_destroy_image(image_local);
                    SAIL_LOG_ERROR("JPEGXL: Failed to get image info");
                    SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
                }

                image_local->source_image->pixel_format =
                    jpegxl_private_sail_pixel_format(info.num_color_channels, info.alpha_bits);
                //image_local->source_image->chroma_subsampling = jpegxl_private_sail_chroma_subsampling(jpegxl_image->yuvFormat);
                //image_local->source_image->compression = SAIL_COMPRESSION_NONE;

                image_local->width          = info.xsize;
                image_local->height         = info.ysize;
                image_local->pixel_format   = image_local->source_image->pixel_format;
                //image_local->delay          = (int)(jpegxl_state->jpegxl_decoder->imageTiming.duration * 1000);
                image_local->bytes_per_line = sail_bytes_per_line(image_local->width, image_local->pixel_format);

                // TODO
                //JxlResizableParallelRunnerSetThreads(
                //        jpegxl_state->runner,
                //        JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
                break;
            }
            case JXL_DEC_COLOR_ENCODING: {
                // TODO
                /*
                size_t icc_size;
                if (JXL_DEC_SUCCESS !=
                        JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                                                                                &icc_size)) {
                    fprintf(stderr, "JxlDecoderGetICCProfileSize failed\n");
                    return false;
                }
                icc_profile->resize(icc_size);
                if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(
                                                                     dec.get(), JXL_COLOR_PROFILE_TARGET_DATA,
                                                                     icc_profile->data(), icc_profile->size())) {
                    fprintf(stderr, "JxlDecoderGetColorAsICCProfile failed\n");
                    return false;
                }
                */
                break;
            }
            case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
/*
                JxlPixelFormat format = {
                    .num_channels = 4,
                    .data_type    = JXL_TYPE_FLOAT,
                    .endianness   = JXL_NATIVE_ENDIAN,
                    .align        = 0
                };
                size_t buffer_size;
                if (JxlDecoderImageOutBufferSize(jpegxl_state->decoder,
                                                    &format,
                                                    &buffer_size) != JXL_DEC_SUCCESS) {
                    sail_destroy_image(image_local);
                    SAIL_LOG_ERROR("JPEGXL: Failed to get output buffer size");
                    SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
                }
                if (buffer_size != (size_t)image_local->width * image_local->height * 16) {
                    sail_destroy_image(image_local);
                    SAIL_LOG_ERROR("JPEGXL: Invalid output buffer size");
                    SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
                }
                pixels->resize(*xsize * *ysize * 4);
                void* pixels_buffer = (void*)pixels->data();
                size_t pixels_buffer_size = pixels->size() * sizeof(float);
                if (JxlDecoderSetImageOutBuffer(jpegxl_state->decoder,
                                                &format,
                                                pixels_buffer,
                                                pixels_buffer_size) != JXL_DEC_SUCCESS) {
                    sail_destroy_image(image_local);
                    SAIL_LOG_ERROR("JPEGXL: Failed to set output buffer");
                    SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
                }
*/
                break;
            }
            case JXL_DEC_FULL_IMAGE: {
                // Nothing to do. Do not yet return. If the image is an animation, more
                // full frames may be decoded. This example only keeps the last one.
                break;
            }
            case JXL_DEC_SUCCESS: {
                done = true;
                break;
            }
            default: {
                sail_destroy_image(image_local);
                SAIL_LOG_ERROR("JPEGXL: Unknown decoder status");
                SAIL_LOG_AND_RETURN(SAIL_ERROR_UNDERLYING_CODEC);
            }
        }
    }

#if 0
    image_local->source_image->pixel_format =
        jpegxl_private_sail_pixel_format(jpegxl_image->yuvFormat, jpegxl_image->depth, jpegxl_image->alphaPlane != NULL);
    image_local->source_image->chroma_subsampling = jpegxl_private_sail_chroma_subsampling(jpegxl_image->yuvFormat);
    image_local->source_image->compression = SAIL_COMPRESSION_NONE;

    image_local->width          = jpegxl_state->width;
    image_local->height         = jpegxl_state->height;
    image_local->pixel_format   = jpegxl_private_rgb_sail_pixel_format(jpegxl_state->rgb_image.format, jpegxl_state->rgb_image.depth);
    image_local->delay          = (int)(jpegxl_state->jpegxl_decoder->imageTiming.duration * 1000);
    image_local->bytes_per_line = sail_bytes_per_line(image_local->width, image_local->pixel_format);

    /* Fetch ICC profile. */
    if (sail_iccp_codec_option(jpegxl_state->load_options->codec_options)) {
        SAIL_TRY_OR_CLEANUP(jpegxl_private_fetch_iccp(&jpegxl_image->icc, &image_local->iccp),
                            /* cleanup */ sail_destroy_image(image_local));
    }
#endif

    *image = image_local;

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_load_frame_v8_jpegxl(void *state, struct sail_image *image) {

    const struct jpegxl_state *jpegxl_state = state;

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_load_finish_v8_jpegxl(void **state) {

    struct jpegxl_state *jpegxl_state = *state;

    *state = NULL;

    destroy_jpegxl_state(jpegxl_state);

    return SAIL_OK;
}

/*
 * Encoding functions.
 */

SAIL_EXPORT sail_status_t sail_codec_save_init_v8_jpegxl(struct sail_io *io, const struct sail_save_options *save_options, void **state) {

    (void)io;
    (void)save_options;
    (void)state;

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_save_seek_next_frame_v8_jpegxl(void *state, const struct sail_image *image) {

    (void)state;
    (void)image;

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_save_frame_v8_jpegxl(void *state, const struct sail_image *image) {

    (void)state;
    (void)image;

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_save_finish_v8_jpegxl(void **state) {

    (void)state;

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}