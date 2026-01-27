/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for uprobe prefix implementation
 */

#undef NDEBUG

#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"
#include "upipe/uprobe_prefix.h"

#include <stdio.h>
#include <assert.h>

int main(int argc, char **argv)
{
    struct uprobe *uprobe2 = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_DEBUG);
    assert(uprobe2 != NULL);

    struct uprobe *uprobe1 = uprobe_pfx_alloc(uprobe_use(uprobe2),
                                              UPROBE_LOG_DEBUG, "pfx");
    assert(uprobe1 != NULL);

    uprobe_err(uprobe1, NULL, "This is an error");
    uprobe_warn_va(uprobe1, NULL, "This is a %s warning with %d", "composite",
                   0x42);
    uprobe_notice(uprobe1, NULL, "This is a notice");
    uprobe_dbg(uprobe1, NULL, "This is a debug");
    uprobe_release(uprobe1);

    uprobe1 = uprobe_pfx_alloc_va(uprobe_use(uprobe2),
                                  UPROBE_LOG_ERROR, "pfx[%d]", 2);
    assert(uprobe1 != NULL);
    uprobe_err_va(uprobe1, NULL, "This is another error with %d", 0x43);
    uprobe_warn(uprobe1, NULL, "This is a warning that you shouldn't see");
    uprobe_release(uprobe1);

    uprobe_release(uprobe2);
    return 0;
}
