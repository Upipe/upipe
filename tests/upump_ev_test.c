/*
 * Copyright (C) 2012-2018 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for upump manager with ev event loop
 */

#undef NDEBUG

#include "upump-ev/upump_ev.h"

#include "upump_common_test.h"

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1

int main(int argc, char **argv)
{
    run(upump_ev_mgr_alloc_default(UPUMP_POOL, UPUMP_BLOCKER_POOL));
    return 0;
}
