/*  This file is part of SAIL (https://github.com/smoked-herring/sail)

    Copyright (c) 2021 Dmitry Baryshev

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

#include "sail-common.h"

#include "helpers.h"

/* PCX signature. */
static const unsigned SAIL_PCX_SIGNATURE = 0x0A;

/*
 * Codec-specific state.
 */
struct pcx_state {
    struct sail_read_options *read_options;
    struct sail_write_options *write_options;

    struct SailPcxHeader pcx_header;
    unsigned char *scan; /* buffer to read a single plane scan line. */

    bool frame_read;
};

static sail_status_t alloc_pcx_state(struct pcx_state **pcx_state) {

    void *ptr;
    SAIL_TRY(sail_malloc(sizeof(struct pcx_state), &ptr));
    *pcx_state = ptr;

    (*pcx_state)->read_options  = NULL;
    (*pcx_state)->write_options = NULL;

    (*pcx_state)->scan          = NULL;
    (*pcx_state)->frame_read    = false;

    return SAIL_OK;
}

static void destroy_pcx_state(struct pcx_state *pcx_state) {

    if (pcx_state == NULL) {
        return;
    }

    sail_destroy_read_options(pcx_state->read_options);
    sail_destroy_write_options(pcx_state->write_options);

    sail_free(pcx_state->scan);

    sail_free(pcx_state);
}

/*
 * Decoding functions.
 */

SAIL_EXPORT sail_status_t sail_codec_read_init_v6_pcx(struct sail_io *io, const struct sail_read_options *read_options, void **state) {

    SAIL_CHECK_PTR(state);
    *state = NULL;

    SAIL_TRY(sail_check_io_valid(io));
    SAIL_CHECK_PTR(read_options);

    /* Allocate a new state. */
    struct pcx_state *pcx_state;
    SAIL_TRY(alloc_pcx_state(&pcx_state));
    *state = pcx_state;

    /* Deep copy read options. */
    SAIL_TRY(sail_copy_read_options(read_options, &pcx_state->read_options));

    /* Read PCX header. */
    SAIL_TRY(pcx_private_read_header(io, &pcx_state->pcx_header));

    if (pcx_state->pcx_header.id != SAIL_PCX_SIGNATURE) {
        SAIL_LOG_ERROR("PCX: ID is %u, but must be %u", pcx_state->pcx_header.id, SAIL_PCX_SIGNATURE);
        SAIL_LOG_AND_RETURN(SAIL_ERROR_BROKEN_IMAGE);
    }

    if (pcx_state->pcx_header.bytes_per_line == 0) {
        SAIL_LOG_ERROR("PCX: Bytes per line is 0");
        SAIL_LOG_AND_RETURN(SAIL_ERROR_BROKEN_IMAGE);
    }

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_read_seek_next_frame_v6_pcx(void *state, struct sail_io *io, struct sail_image **image) {

    SAIL_CHECK_PTR(state);
    SAIL_TRY(sail_check_io_valid(io));
    SAIL_CHECK_PTR(image);

    struct pcx_state *pcx_state = (struct pcx_state *)state;

    if (pcx_state->frame_read) {
        SAIL_LOG_AND_RETURN(SAIL_ERROR_NO_MORE_FRAMES);
    }

    pcx_state->frame_read = true;

    struct sail_image *image_local;
    SAIL_TRY(sail_alloc_image(&image_local));
    SAIL_TRY_OR_CLEANUP(sail_alloc_source_image(&image_local->source_image),
                        /* cleanup */ sail_destroy_image(image_local));

    SAIL_TRY_OR_CLEANUP(pcx_private_sail_pixel_format(
                            pcx_state->pcx_header.bits_per_plane,
                            pcx_state->pcx_header.planes,
                            pcx_state->pcx_header.palette_info,
                            &image_local->source_image->pixel_format),
                        /* cleanup */ sail_destroy_image(image_local));

    image_local->source_image->compression = SAIL_COMPRESSION_RLE;

    image_local->width = pcx_state->pcx_header.xmax - pcx_state->pcx_header.xmin + 1;
    image_local->height = pcx_state->pcx_header.ymax - pcx_state->pcx_header.ymin + 1;
    image_local->pixel_format = image_local->source_image->pixel_format;

    SAIL_TRY_OR_CLEANUP(sail_bytes_per_line(image_local->width, image_local->pixel_format, &image_local->bytes_per_line),
                        /* cleanup */ sail_destroy_image(image_local));

    /* Temporary scan line buffer. */
    switch (image_local->pixel_format) {
        case SAIL_PIXEL_FORMAT_BPP24_RGB:
        case SAIL_PIXEL_FORMAT_BPP16_RGBA:
        case SAIL_PIXEL_FORMAT_BPP32_RGBA: {
            SAIL_TRY_OR_CLEANUP(sail_malloc(image_local->bytes_per_line, &pcx_state->scan),
                                /* cleanup */ sail_destroy_image(image_local));

            break;
        }
    }

    /* Build palette if needed. */
    SAIL_TRY_OR_CLEANUP(pcx_private_build_palette(image_local->pixel_format, io, pcx_state->pcx_header.palette, &image_local->palette),
                        /* cleanup */ sail_destroy_image(image_local));

    if (pcx_state->pcx_header.hdpi > 0 && pcx_state->pcx_header.vdpi > 0) {
        SAIL_TRY_OR_CLEANUP(sail_alloc_resolution_from_data(SAIL_RESOLUTION_UNIT_INCH,
                                                            pcx_state->pcx_header.hdpi,
                                                            pcx_state->pcx_header.vdpi,
                                                            &image_local->resolution),
                            /* cleanup */ sail_destroy_image(image_local));
    }

    *image = image_local;

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_read_frame_v6_pcx(void *state, struct sail_io *io, struct sail_image *image) {

    SAIL_CHECK_PTR(state);
    SAIL_TRY(sail_check_io_valid(io));
    SAIL_TRY(sail_check_image_skeleton_valid(image));

    const struct pcx_state *pcx_state = (struct pcx_state *)state;

    if (pcx_state->pcx_header.encoding == SAIL_PCX_NO_ENCODING) {
        switch (image->pixel_format) {
            case SAIL_PIXEL_FORMAT_BPP1_INDEXED:
            case SAIL_PIXEL_FORMAT_BPP4_INDEXED:
            case SAIL_PIXEL_FORMAT_BPP8_INDEXED:
            case SAIL_PIXEL_FORMAT_BPP8_GRAYSCALE: {
                const unsigned line_padding = pcx_state->pcx_header.bytes_per_line - image->bytes_per_line;

                for (unsigned row = 0; row < image->height; row++) {
                    unsigned char *scan = (unsigned char *)image->pixels + image->bytes_per_line * row;

                    SAIL_TRY(io->strict_read(io->stream, scan, image->bytes_per_line));
                    SAIL_TRY(io->seek(io->stream, line_padding, SEEK_CUR));
                }

                break;
            }
            case SAIL_PIXEL_FORMAT_BPP24_RGB: {
                const unsigned line_padding = pcx_state->pcx_header.bytes_per_line - image->width;

                for (unsigned row = 0; row < image->height; row++) {
                    unsigned char *target_scan = (unsigned char *)image->pixels + image->bytes_per_line * row;

                    for (unsigned component = 0; component < 3; component++) {
                        SAIL_TRY(io->strict_read(io->stream, pcx_state->scan, image->width));
                        SAIL_TRY(io->seek(io->stream, line_padding, SEEK_CUR));

                        for (unsigned column = 0; column < image->width; column++) {
                            *(target_scan + column * 3 + component) = *(pcx_state->scan + column);
                        }
                    }
                }

                break;
            }
            case SAIL_PIXEL_FORMAT_BPP16_RGBA:
            case SAIL_PIXEL_FORMAT_BPP32_RGBA: {
                break;
            }
        }
    } else {
    }

    return SAIL_OK;
}

SAIL_EXPORT sail_status_t sail_codec_read_finish_v6_pcx(void **state, struct sail_io *io) {

    SAIL_CHECK_PTR(state);
    SAIL_TRY(sail_check_io_valid(io));

    struct pcx_state *pcx_state = (struct pcx_state *)(*state);

    /* Subsequent calls to finish() will expectedly fail in the above line. */
    *state = NULL;

    destroy_pcx_state(pcx_state);

    return SAIL_OK;
}

/*
 * Encoding functions.
 */

SAIL_EXPORT sail_status_t sail_codec_write_init_v6_pcx(struct sail_io *io, const struct sail_write_options *write_options, void **state) {

    SAIL_CHECK_PTR(state);
    SAIL_TRY(sail_check_io_valid(io));
    SAIL_CHECK_PTR(write_options);

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_write_seek_next_frame_v6_pcx(void *state, struct sail_io *io, const struct sail_image *image) {

    SAIL_CHECK_PTR(state);
    SAIL_TRY(sail_check_io_valid(io));
    SAIL_TRY(sail_check_image_valid(image));

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_write_frame_v6_pcx(void *state, struct sail_io *io, const struct sail_image *image) {

    SAIL_CHECK_PTR(state);
    SAIL_TRY(sail_check_io_valid(io));
    SAIL_TRY(sail_check_image_valid(image));

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}

SAIL_EXPORT sail_status_t sail_codec_write_finish_v6_pcx(void **state, struct sail_io *io) {

    SAIL_CHECK_PTR(state);
    SAIL_TRY(sail_check_io_valid(io));

    SAIL_LOG_AND_RETURN(SAIL_ERROR_NOT_IMPLEMENTED);
}
