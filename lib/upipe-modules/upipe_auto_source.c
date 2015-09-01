/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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

#include <stdlib.h>

#include <upipe/ulist.h>
#include <upipe/uuri.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe-modules/upipe_auto_source.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_http_source.h>

/** @hidden */
static struct upipe *upipe_auto_src_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature,
                                          va_list args);
/** @hidden */
static void upipe_auto_src_free(struct urefcount *urefcount);
/** @hidden */
static int upipe_auto_src_control(struct upipe *upipe, int command,
                                  va_list args);

/** @internal @This stores the private context for an auto source manager. */
struct upipe_auto_src_mgr {
    /** public manager structure */
    struct upipe_mgr mgr;
    /** urefcount structure */
    struct urefcount urefcount;
    /** list of @tt {struct upipe_ato_src_mgr_sub *} */
    struct uchain mgrs;
};

UBASE_FROM_TO(upipe_auto_src_mgr, upipe_mgr, mgr, mgr);
UBASE_FROM_TO(upipe_auto_src_mgr, urefcount, urefcount, urefcount);

/** @internal @This stores a registered manager for a given scheme. */
struct upipe_auto_src_mgr_sub {
    /** scheme of the sub manager */
    char *scheme;
    /** the sub manager */
    struct upipe_mgr *mgr;
    /** uchain for @tt {upipe_auto_src_mgr->mgrs} */
    struct uchain uchain;
};

UBASE_FROM_TO(upipe_auto_src_mgr_sub, uchain, uchain, uchain);

/** @internal @This returns the registered manager for a given scheme.
 *
 * @param mgr pointer to upipe auto source manager
 * @param scheme an URI scheme (ex: "http", "file", "https", ...)
 * @returns the corresponding registered manager of NULL
 */
static struct upipe_auto_src_mgr_sub *
upipe_auto_src_mgr_sub_from_scheme(struct upipe_mgr *mgr, const char *scheme)
{
    struct upipe_auto_src_mgr *upipe_auto_src_mgr =
        upipe_auto_src_mgr_from_mgr(mgr);

    if (!scheme)
        return NULL;

    struct uchain *uchain;
    ulist_foreach(&upipe_auto_src_mgr->mgrs, uchain) {
        struct upipe_auto_src_mgr_sub *upipe_auto_src_mgr_sub =
            upipe_auto_src_mgr_sub_from_uchain(uchain);
        if (!strcmp(upipe_auto_src_mgr_sub->scheme, scheme))
            return upipe_auto_src_mgr_sub;
    }
    return NULL;
}

/** @internal @This gets the registered manager for the given scheme.
 *
 * @param mgr pointer to upipe auto source manager
 * @param scheme an URI scheme (ex: "http", "file", "https", ...)
 * @param mgr_src_p a pointer to @tt {struct upipe_mgr *} filled with the
 * registered manager for the scheme @ref scheme
 * @return an error code
 */
static int _upipe_auto_src_mgr_get_mgr(struct upipe_mgr *mgr,
                                       const char *scheme,
                                       struct upipe_mgr **mgr_src_p)
{
    struct upipe_auto_src_mgr_sub *upipe_auto_src_mgr_sub =
        upipe_auto_src_mgr_sub_from_scheme(mgr, scheme);

    if (upipe_auto_src_mgr_sub) {
        *mgr_src_p = upipe_auto_src_mgr_sub->mgr;
        return UBASE_ERR_NONE;
    }
    return UBASE_ERR_INVALID;
}

/** @internal @This registers a manager for the given scheme.
 *
 * @param mgr pointer to upipe auto source manager
 * @param scheme an URI scheme (ex: "http", "file", "https", ...)
 * @param mgr_src the manager to register or NULL
 * @return an error code
 */
static int _upipe_auto_src_mgr_set_mgr(struct upipe_mgr *mgr,
                                       const char *scheme,
                                       struct upipe_mgr *mgr_src)
{
    struct upipe_auto_src_mgr *upipe_auto_src_mgr =
        upipe_auto_src_mgr_from_mgr(mgr);
    struct upipe_auto_src_mgr_sub *upipe_auto_src_mgr_sub =
        upipe_auto_src_mgr_sub_from_scheme(mgr, scheme);

    if (!scheme)
        return UBASE_ERR_INVALID;

    if (upipe_auto_src_mgr_sub) {
        upipe_mgr_release(upipe_auto_src_mgr_sub->mgr);
        upipe_auto_src_mgr_sub->mgr = upipe_mgr_use(mgr_src);
    }
    else {
        upipe_auto_src_mgr_sub = malloc(sizeof (*upipe_auto_src_mgr_sub));
        if (unlikely(upipe_auto_src_mgr_sub == NULL))
            return UBASE_ERR_ALLOC;
        upipe_auto_src_mgr_sub->scheme = strdup(scheme);
        if (unlikely(upipe_auto_src_mgr_sub->scheme == NULL)) {
            free(upipe_auto_src_mgr_sub);
            return UBASE_ERR_ALLOC;
        }
        upipe_auto_src_mgr_sub->mgr = upipe_mgr_use(mgr_src);
        if (unlikely(upipe_auto_src_mgr_sub->mgr == NULL)) {
            free(upipe_auto_src_mgr_sub->scheme);
            free(upipe_auto_src_mgr_sub);
            return UBASE_ERR_ALLOC;
        }
        ulist_add(&upipe_auto_src_mgr->mgrs,
                  upipe_auto_src_mgr_sub_to_uchain(upipe_auto_src_mgr_sub));
    }
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches auto source manager commands.
 *
 * @param mgr pointer to upipe auto source manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_auto_src_mgr_control(struct upipe_mgr *mgr,
                                      int command,
                                      va_list args)
{
    switch (command) {
    case UPIPE_AUTO_SRC_MGR_SET_MGR: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_AUTOSRC_SIGNATURE)
        const char *scheme = va_arg(args, const char *);
        struct upipe_mgr *mgr_src = va_arg(args, struct upipe_mgr *);
        return _upipe_auto_src_mgr_set_mgr(mgr, scheme, mgr_src);
    }

    case UPIPE_AUTO_SRC_MGR_GET_MGR: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_AUTOSRC_SIGNATURE)
        const char *scheme = va_arg(args, const char *);
        struct upipe_mgr **mgr_src_p = va_arg(args, struct upipe_mgr **);
        return _upipe_auto_src_mgr_get_mgr(mgr, scheme, mgr_src_p);
    }
    }

    return UBASE_ERR_UNHANDLED;
}

/** @internal @This frees an auto source manager.
 *
 * @param urefcount the embedded @tt {struct urefcount} of the manager
 */
void upipe_auto_src_mgr_free(struct urefcount *urefcount)
{
    struct upipe_auto_src_mgr *upipe_auto_src_mgr =
        upipe_auto_src_mgr_from_urefcount(urefcount);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_auto_src_mgr->mgrs)) != NULL) {
        struct upipe_auto_src_mgr_sub *upipe_auto_src_mgr_sub =
            upipe_auto_src_mgr_sub_from_uchain(uchain);
        upipe_mgr_release(upipe_auto_src_mgr_sub->mgr);
        free(upipe_auto_src_mgr_sub->scheme);
        free(upipe_auto_src_mgr_sub);
    }
    urefcount_clean(urefcount);
    free(upipe_auto_src_mgr);
}


/** @This allocates an auto source manager.
 *
 * @return a pointer to the allocated manager.
 */
struct upipe_mgr *upipe_auto_src_mgr_alloc(void)
{
    struct upipe_auto_src_mgr *upipe_auto_src_mgr =
        malloc(sizeof (*upipe_auto_src_mgr));
    if (unlikely(upipe_auto_src_mgr == NULL))
        return NULL;

    urefcount_init(upipe_auto_src_mgr_to_urefcount(upipe_auto_src_mgr),
                   upipe_auto_src_mgr_free);
    ulist_init(&upipe_auto_src_mgr->mgrs);
    upipe_auto_src_mgr->mgr = (struct upipe_mgr){
        .refcount = upipe_auto_src_mgr_to_urefcount(upipe_auto_src_mgr),
        .signature = UPIPE_AUTOSRC_SIGNATURE,

        .upipe_alloc = upipe_auto_src_alloc,
        .upipe_control = upipe_auto_src_control,
        .upipe_mgr_control = upipe_auto_src_mgr_control,
    };

    return upipe_auto_src_mgr_to_mgr(upipe_auto_src_mgr);
}

/** @internal @This stores the private context of an auto source pipe. */
struct upipe_auto_src {
    /** no reference refcount */
    struct urefcount urefcount;
    /** real refcount */
    struct urefcount urefcount_real;
    /** for helper upipe */
    struct upipe upipe;
    /** proxy probe */
    struct uprobe proxy_probe;
    /** output pipe */
    struct upipe *output;
    /** inner source pipe */
    struct upipe *src;
};

UBASE_FROM_TO(upipe_auto_src, uprobe, proxy_probe, proxy_probe);
UBASE_FROM_TO(upipe_auto_src, urefcount, urefcount_real, urefcount_real);

UPIPE_HELPER_UPIPE(upipe_auto_src, upipe, UPIPE_AUTOSRC_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_auto_src, urefcount, upipe_auto_src_no_ref);
UPIPE_HELPER_VOID(upipe_auto_src);

/** @internal @This catches event from the inner pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner pointer to inner pipe
 * @param event event thrown
 * @param args optional arguments
 * @return an error code
 */
static int upipe_auto_src_proxy_probe(struct uprobe *uprobe,
                                      struct upipe *inner,
                                      int event, va_list args)
{
    struct upipe_auto_src *upipe_auto_src =
        upipe_auto_src_from_proxy_probe(uprobe);
    struct upipe *upipe = upipe_auto_src_to_upipe(upipe_auto_src);

    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This allocates an auto source pipe.
 *
 * @param mgr pointer to upipe auto source manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return a pointer to the allocated pipe
 */
static struct upipe *upipe_auto_src_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature,
                                          va_list args)
{
    struct upipe *upipe =
        upipe_auto_src_alloc_void(mgr, uprobe, signature, args);
    upipe_auto_src_init_urefcount(upipe);

    struct upipe_auto_src *upipe_auto_src = upipe_auto_src_from_upipe(upipe);
    urefcount_init(upipe_auto_src_to_urefcount_real(upipe_auto_src),
                   upipe_auto_src_free);
    upipe_auto_src->src = NULL;
    upipe_auto_src->output = NULL;
    uprobe_init(upipe_auto_src_to_proxy_probe(upipe_auto_src),
                upipe_auto_src_proxy_probe, NULL);
    upipe_auto_src->proxy_probe.refcount =
        upipe_auto_src_to_urefcount_real(upipe_auto_src);

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees an auto source pipe.
 *
 * @param urefcount the embedded @tt {struct urefcount} of the pipe
 */
static void upipe_auto_src_free(struct urefcount *urefcount)
{
    struct upipe_auto_src *upipe_auto_src =
        upipe_auto_src_from_urefcount_real(urefcount);
    struct upipe *upipe = upipe_auto_src_to_upipe(upipe_auto_src);

    upipe_throw_dead(upipe);

    urefcount_clean(&upipe_auto_src->urefcount_real);
    upipe_auto_src_clean_urefcount(upipe);
    upipe_auto_src_free_void(upipe);
}

/** @internal @This is called when there is no external reference to the
 * pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_auto_src_no_ref(struct upipe *upipe)
{
    struct upipe_auto_src *upipe_auto_src = upipe_auto_src_from_upipe(upipe);

    upipe_release(upipe_auto_src->output);
    upipe_release(upipe_auto_src->src);

    urefcount_release(upipe_auto_src_to_urefcount_real(upipe_auto_src));
}

/** @internal @This creates the inner pipe according to the scheme and sets
 * the uri.
 *
 * @param upipe description structure of the pipe
 * @param uri the uri to set
 * @param scheme the scheme of the uri.
 * @return an error code
 */
static int upipe_auto_src_set_uri_scheme(struct upipe *upipe,
                                         const char *uri,
                                         const char *scheme)
{
    struct upipe_auto_src *upipe_auto_src = upipe_auto_src_from_upipe(upipe);
    struct upipe_auto_src_mgr_sub *upipe_auto_src_mgr_sub;

    if (unlikely(scheme == NULL)) {
        upipe_err_va(upipe, "invalid scheme for uri %s", uri);
        return UBASE_ERR_INVALID;
    }

    upipe_auto_src_mgr_sub =
        upipe_auto_src_mgr_sub_from_scheme(upipe->mgr, scheme);
    if (unlikely(upipe_auto_src_mgr_sub == NULL)) {
        upipe_warn_va(upipe, "no sub manager for scheme %s", scheme);
        return UBASE_ERR_UNHANDLED;
    }

    if (unlikely(upipe_auto_src->src) &&
        unlikely(upipe_auto_src->src->mgr != upipe_auto_src_mgr_sub->mgr)) {
        upipe_release(upipe_auto_src->src);
        upipe_auto_src->src = NULL;
    }

    if (likely(upipe_auto_src->src == NULL)) {
        upipe_auto_src->src = upipe_void_alloc(
            upipe_auto_src_mgr_sub->mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_auto_src->proxy_probe),
                             UPROBE_LOG_VERBOSE, scheme));

        if (unlikely(upipe_auto_src->src == NULL)) {
            upipe_err(upipe, "fail to allocate inner pipe");
            return UBASE_ERR_ALLOC;
        }

        if (upipe_auto_src->output) {
            int ret = upipe_set_output(upipe_auto_src->src,
                                       upipe_auto_src->output);
            if (ret) {
                upipe_err(upipe, "fail to set inner pipe output");
                return ret;
            }
        }
    }

    return upipe_set_uri(upipe_auto_src->src, uri);
}

/** @internal @This sets the uri of the inner pipe.
 *
 * @param upipe description structure of the pipe
 * @param uri the uri to set
 * @return an error code
 */
static int upipe_auto_src_set_uri(struct upipe *upipe, const char *uri)
{
    struct uuri uuri;
    if (ubase_check(uuri_from_str(&uuri, uri))) {
        char scheme[uuri.scheme.len + 1];
        ustring_cpy(uuri.scheme, scheme, sizeof (scheme));
        return upipe_auto_src_set_uri_scheme(upipe, uri, scheme);
    }
    else
        return upipe_auto_src_set_uri_scheme(upipe, uri, "file");
}

/** @internal @This sets the output pipe of the inner pipe.
 *
 * @param upipe description structure of the pipe
 * @param output output pipe to set
 * @return an error code
 */
static int upipe_auto_src_set_output(struct upipe *upipe,
                                     struct upipe *output)
{
    struct upipe_auto_src *upipe_auto_src = upipe_auto_src_from_upipe(upipe);
    upipe_release(upipe_auto_src->output);
    upipe_auto_src->output = upipe_use(output);
    if (upipe_auto_src->src)
        upipe_set_output(upipe_auto_src->src, output);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the output pipe of the inner pipe.
 *
 * @param upipe description structure of the pipe
 * @param output_p filled with the output pipe.
 * @return an error code
 */
static int upipe_auto_src_get_output(struct upipe *upipe,
                                     struct upipe **output_p)
{
    struct upipe_auto_src *upipe_auto_src = upipe_auto_src_from_upipe(upipe);
    if (output_p)
        *output_p = upipe_auto_src->output;
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches auto source pipe commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_auto_src_control(struct upipe *upipe,
                                  int command,
                                  va_list args)
{
    struct upipe_auto_src *upipe_auto_src = upipe_auto_src_from_upipe(upipe);

    switch (command) {
    case UPIPE_SET_URI: {
        const char *uri = va_arg(args, const char *);
        return upipe_auto_src_set_uri(upipe, uri);
    }

    case UPIPE_SET_OUTPUT: {
        struct upipe *output = va_arg(args, struct upipe *);
        return upipe_auto_src_set_output(upipe, output);
    }
    case UPIPE_GET_OUTPUT: {
        struct upipe **output_p = va_arg(args, struct upipe **);
        return upipe_auto_src_get_output(upipe, output_p);
    }
    }

    if (!upipe_auto_src->src)
        return UBASE_ERR_UNHANDLED;
    return upipe_control_va(upipe_auto_src->src, command, args);
}
