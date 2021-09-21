/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation https (the
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
 * @short probe catching http scheme hook for SSL connection
 */

#include "upipe/uprobe_helper_uprobe.h"
#include "upipe/uprobe_helper_alloc.h"
#include "upipe/uref_uri.h"

#include "upipe-modules/upipe_http_source.h"

#include "upipe-bearssl/uprobe_https.h"

#include "https_source_hook.h"

/** @This stores the private context of the probe. */
struct uprobe_https {
    /** public probe structure */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_https, uprobe);

/** @internal @This catches events.
 *
 * @param uprobe structure used to raise events
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional arguments
 */
static int uprobe_https_catch(struct uprobe *uprobe,
                              struct upipe *upipe,
                              int event, va_list args)
{
    if (event != UPROBE_HTTP_SRC_SCHEME_HOOK ||
        ubase_get_signature(args) != UPIPE_HTTP_SRC_SIGNATURE)
        return uprobe_throw_next(uprobe, upipe, event, args);

    va_list args_copy;
    va_copy(args_copy, args);
    UBASE_SIGNATURE_CHECK(args_copy, UPIPE_HTTP_SRC_SIGNATURE);
    struct uref *flow_def = va_arg(args_copy, struct uref *);
    struct upipe_http_src_hook **hook =
        va_arg(args_copy, struct upipe_http_src_hook **);

    const char *scheme = NULL;
    uref_uri_get_scheme(flow_def, &scheme);
    if (scheme && !strcasecmp(scheme, "https")) {
        struct upipe_http_src_hook *https_hook = https_src_hook_alloc(flow_def);
        if (unlikely(!https_hook))
            return UBASE_ERR_ALLOC;

        if (hook)
            *hook = https_hook;
        else
            upipe_http_src_hook_release(https_hook);
        return UBASE_ERR_NONE;
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This initializes a HTTPS probe for SSL connection.
 *
 * @param uprobe_https pointer to the private context
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
static struct uprobe *uprobe_https_init(struct uprobe_https *uprobe_https,
                                        struct uprobe *next)
{
    assert(uprobe_https);
    struct uprobe *uprobe = uprobe_https_to_uprobe(uprobe_https);
    uprobe_init(uprobe, uprobe_https_catch, next);
    return uprobe;
}

/** @internal @This cleans a uprobe_https structure.
 *
 * @param uprobe_https pointer to the private context
 */
static void uprobe_https_clean(struct uprobe_https *uprobe_https)
{
    assert(uprobe_https);
    struct uprobe *uprobe = uprobe_https_to_uprobe(uprobe_https);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_https)
#undef ARGS
#undef ARGS_DECL
