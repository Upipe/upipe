/*
 * Copyright (C) 2021 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for upump manager with libsrt event loop
 */

#undef NDEBUG

#include "upump-srt/upump_srt.h"
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
