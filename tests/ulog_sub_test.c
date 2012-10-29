/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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
 * @short unit tests for ulog sub implementation
 */

#undef NDEBUG

#include <upipe/ulog.h>
#include <upipe/ulog_stdio.h>
#include <upipe/ulog_sub.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv)
{
    struct ulog *ulog2 = ulog_stdio_alloc(stdout, ULOG_DEBUG, "test");
    assert(ulog2 != NULL);

    struct ulog *ulog1 = ulog_sub_alloc(ulog2, ULOG_DEBUG, "sub");
    assert(ulog1 != NULL);

    ulog_error(ulog1, "This is an error");
    ulog_warning(ulog1, "This is a %s warning with %d", "composite", 0x42);
    ulog_notice(ulog1, "This is a notice");
    ulog_debug(ulog1, "This is a debug, next error is an allocation failure");
    ulog_aerror(ulog1);
    ulog_free(ulog1);

    ulog1 = ulog_sub_alloc_va(ulog2, ULOG_ERROR, "sub[%d]", 2);
    assert(ulog1 != NULL);
    ulog_error(ulog1, "This is another error with %d", 0x43);
    ulog_warning(ulog1, "This is a warning that you shouldn't see");
    ulog_free(ulog1);

    ulog_free(ulog2);
    return 0;
}
