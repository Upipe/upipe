/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
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
 * @short unit tests for uprobe syslog implementation
 */

#undef NDEBUG

#include <upipe/uprobe.h>
#include <upipe/uprobe_syslog.h>

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv)
{
    const char *ident = "upipe-test";

    struct uprobe *uprobe1 = uprobe_syslog_alloc(NULL, ident, LOG_NDELAY | LOG_PID, LOG_LOCAL0, UPROBE_LOG_DEBUG);
    assert(uprobe1 != NULL);

    uprobe_err(uprobe1, NULL, "This is an error");
    uprobe_warn_va(uprobe1, NULL, "This is a %s warning with %d", "composite",
                   0x42);
    uprobe_notice(uprobe1, NULL, "This is a notice");
    uprobe_dbg(uprobe1, NULL, "This is a debug");
    uprobe_release(uprobe1);

    struct uprobe *uprobe2 = uprobe_syslog_alloc(NULL, ident, LOG_NDELAY | LOG_PID, LOG_LOCAL0, UPROBE_LOG_ERROR);
    assert(uprobe2 != NULL);
    uprobe_err_va(uprobe2, NULL, "This is another error with %d", 0x43);
    uprobe_warn(uprobe2, NULL, "This is a warning that you shouldn't see");
    uprobe_release(uprobe2);
    return 0;
}
