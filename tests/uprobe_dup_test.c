/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for uprobe_dup implementation
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_dup.h"
#include "upipe/upipe.h"

#include <stdio.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0

static int events;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
        case UPROBE_LOCAL:
            events++;
            break;
    }
    return UBASE_ERR_NONE;
}

/** definition of our uprobe */
static int catch_dup(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
            break;
        case UPROBE_LOCAL:
            events++;
            break;
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char **argv)
{
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *logger = uprobe_stdio_alloc(&uprobe, stdout,
                                               UPROBE_LOG_VERBOSE);
    assert(logger != NULL);

    struct uprobe uprobe2;
    uprobe_init(&uprobe2, catch_dup, NULL);

    struct uprobe *uprobe_dup = uprobe_dup_alloc(uprobe_use(logger), &uprobe2,
                                                 UPROBE_LOCAL);
    assert(uprobe_dup != NULL);

    struct upipe test_pipe;
    test_pipe.uprobe = uprobe_dup;
    struct upipe *upipe = &test_pipe;

    ubase_assert(upipe_throw(upipe, UPROBE_READY));

    ubase_assert(upipe_throw(upipe, UPROBE_LOCAL));

    assert(events == 2);

    uprobe_release(uprobe_dup);
    uprobe_release(logger);
    uprobe_clean(&uprobe);
    uprobe_clean(&uprobe2);

    return 0;
}
