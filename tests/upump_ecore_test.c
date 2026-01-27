/*
 * Copyright (C) 2012-2018 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for upump manager with ecore event loop
 */

#undef NDEBUG

#include "upump-ecore/upump_ecore.h"

#include <Ecore.h>

#include "upump_common_test.h"

#define UPUMP_POOL 1
#define UPUMP_BLOCKER_POOL 1

int main(int argc, char **argv)
{
    run(upump_ecore_mgr_alloc(UPUMP_POOL, UPUMP_BLOCKER_POOL));
    return 0;
}
