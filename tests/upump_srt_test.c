/*
 * Copyright (C) 2021 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short unit tests for upump manager with libsrt event loop
 */

#undef NDEBUG

#include <upump-srt/upump_srt.h>
#include <srt/srt.h>
#include "upump_common_test.h"

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1

int main(int argc, char **argv)
{
    srt_startup();
    run(upump_srt_mgr_alloc(UPUMP_POOL, UPUMP_BLOCKER_POOL));
    srt_cleanup();
    return 0;
}
