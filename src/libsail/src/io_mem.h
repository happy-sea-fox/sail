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

#ifndef SAIL_IO_MEM_H
#define SAIL_IO_MEM_H

#include <stddef.h>

#ifdef SAIL_BUILD
    #include "error.h"
    #include "export.h"
#else
    #include <sail-common/error.h>
    #include <sail-common/export.h>
#endif

struct sail_io;

/*
 * Opens the specified memory buffer for reading and allocates a new I/O object for it.
 * The assigned I/O object MUST be destroyed later with sail_destroy_io().
 *
 * Returns 0 on success or sail_status_t on error.
 */
SAIL_HIDDEN sail_status_t alloc_io_read_mem(const void *buffer, size_t length, struct sail_io **io);

/*
 * Opens the specified memory buffer for writing and allocates a new I/O object for it.
 * The assigned I/O object MUST be destroyed later with sail_destroy_io().
 *
 * Returns 0 on success or sail_status_t on error.
 */
SAIL_HIDDEN sail_status_t alloc_io_write_mem(void *buffer, size_t length, struct sail_io **io);

#endif
