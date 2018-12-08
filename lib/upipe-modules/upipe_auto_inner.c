/*
 * Copyright (C) 2018 OpenHeadend S.A.R.L.
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

/** @file
 * @short Upipe module trying inner pipes to handle input flow def.
 */

#include <upipe/urefcount_helper.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe.h>
#include <upipe/uprobe_prefix.h>

#include <upipe-modules/upipe_auto_inner.h>

/** @internal @This store an attached pipe manager. */
struct upipe_autoin_mgr_item {
    /** reference to the manager */
    struct upipe_mgr *mgr;
    /** link into the list */
    struct uchain uchain;
    /** name use for prefix probe */
    char *name;
};

UBASE_FROM_TO(upipe_autoin_mgr_item, uchain, uchain, uchain);

/** @internal @This is the private structure of the pipe manager. */
struct upipe_autoin_mgr {
    /** public structure */
    struct upipe_mgr mgr;
    /** refcount structure */
    struct urefcount urefcount;
    /** list of attach pipe manager for inner allocation */
    struct uchain inner_mgrs;
};

UREFCOUNT_HELPER(upipe_autoin_mgr, urefcount, upipe_autoin_mgr_free);
UBASE_FROM_TO(upipe_autoin_mgr, upipe_mgr, mgr, mgr);

/** @internal @This is the private structure of the pipe. */
struct upipe_autoin {
    /** public structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** inner refcount */
    struct urefcount urefcount_real;
    /** inner probe */
    struct uprobe proxy_probe;
    /** current inner pipe */
    struct upipe *inner;
    /** input request list */
    struct uchain input_request_list;
    /** output request list */
    struct uchain output_request_list;
    /** bin output pipe */
    struct upipe *bin_output;
    /** flow def received at allocation */
    struct uref *alloc_flow_def;
};

UPIPE_HELPER_UPIPE(upipe_autoin, upipe, UPIPE_AUTOIN_SIGNATURE);
UPIPE_HELPER_FLOW(upipe_autoin, NULL);
UPIPE_HELPER_UREFCOUNT(upipe_autoin, urefcount, upipe_autoin_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_autoin, urefcount_real, upipe_autoin_free);
UPIPE_HELPER_UPROBE(upipe_autoin, urefcount_real, proxy_probe, NULL);
UPIPE_HELPER_INNER(upipe_autoin, inner);
UPIPE_HELPER_BIN_INPUT(upipe_autoin, inner,  input_request_list);
UPIPE_HELPER_BIN_OUTPUT(upipe_autoin, inner, bin_output, output_request_list);

/** @internal @This allocates and initializes a pipe.
 *
 * @param mgr pointer to upipe manager
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return an alloocated and initialized pipe or NULL
 */
static struct upipe *upipe_autoin_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        uint32_t signature,
                                        va_list args)
{
    struct uref *flow_def = NULL;
    struct upipe *upipe =
        upipe_autoin_alloc_flow(mgr, uprobe, signature, args, &flow_def);
    if (unlikely(!upipe))
        return NULL;
    struct upipe_autoin *upipe_autoin = upipe_autoin_from_upipe(upipe);

    upipe_autoin_init_urefcount(upipe);
    upipe_autoin_init_urefcount_real(upipe);
    upipe_autoin_init_proxy_probe(upipe);
    upipe_autoin_init_inner(upipe);
    upipe_autoin_init_bin_input(upipe);
    upipe_autoin_init_bin_output(upipe);

    upipe_autoin->alloc_flow_def = flow_def;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This is called by refcount to free the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_autoin_free(struct upipe *upipe)
{
    struct upipe_autoin *upipe_autoin = upipe_autoin_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_autoin->alloc_flow_def);
    upipe_autoin_clean_bin_output(upipe);
    upipe_autoin_clean_bin_input(upipe);
    upipe_autoin_clean_inner(upipe);
    upipe_autoin_clean_proxy_probe(upipe);
    upipe_autoin_clean_urefcount_real(upipe);
    upipe_autoin_clean_urefcount(upipe);
    upipe_autoin_free_flow(upipe);
}

/** @internal @This is called when there is no external reference on the pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_autoin_no_ref(struct upipe *upipe)
{
    upipe_autoin_store_bin_output(upipe, NULL);
    upipe_autoin_store_bin_input(upipe, NULL);
    upipe_autoin_release_urefcount_real(upipe);
}

/** @internal @This sets the input flow def of the pipe
 *
 * @param upipe description structure of the pipe
 * @param flow_def new input flow def to set
 * @return an error code
 */
static int upipe_autoin_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_autoin *upipe_autoin = upipe_autoin_from_upipe(upipe);
    struct upipe_mgr *mgr = upipe->mgr;
    struct upipe_autoin_mgr *upipe_autoin_mgr = upipe_autoin_mgr_from_mgr(mgr);

    upipe_autoin_store_bin_output(upipe, NULL);
    upipe_autoin_store_bin_input(upipe, NULL);

    struct upipe *inner = NULL;
    struct uchain *uchain;
    ulist_foreach(&upipe_autoin_mgr->inner_mgrs, uchain) {
        struct upipe_autoin_mgr_item *item =
            upipe_autoin_mgr_item_from_uchain(uchain);
        inner = upipe_flow_alloc(
            item->mgr,
            uprobe_pfx_alloc(uprobe_use(&upipe_autoin->proxy_probe),
                             UPROBE_LOG_VERBOSE, item->name),
            upipe_autoin->alloc_flow_def);
        if (unlikely(!inner)) {
            inner = upipe_void_alloc(
                item->mgr,
                uprobe_pfx_alloc(
                    uprobe_use(&upipe_autoin->proxy_probe),
                    UPROBE_LOG_VERBOSE, item->name));
            if (unlikely(!inner)) {
                upipe_warn(upipe, "fail to allocate inner pipe");
                continue;
            }
        }
        int ret = upipe_set_flow_def(inner, flow_def);
        if (unlikely(!ubase_check(ret))) {
            upipe_dbg(inner, "flow def rejected, trying next manager");
            upipe_release(inner);
            inner = NULL;
            continue;
        }
        else {
            upipe_dbg(inner, "flow def accepted");
        }
        break;
    }

    if (unlikely(!inner))
        return UBASE_ERR_INVALID;

    upipe_autoin_store_bin_output(upipe, inner);
    upipe_autoin_store_bin_input(upipe, upipe_use(inner));
    return UBASE_ERR_NONE;
}

/** @internal @This handles control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_autoin_control(struct upipe *upipe, int command, va_list args)
{
    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_autoin_set_flow_def(upipe, flow_def);
        }

        case UPIPE_REGISTER_REQUEST:
        case UPIPE_UNREGISTER_REQUEST:
        case UPIPE_BIN_GET_FIRST_INNER:
            return upipe_autoin_control_bin_input(upipe, command, args);
        case UPIPE_GET_OUTPUT:
        case UPIPE_SET_OUTPUT:
        case UPIPE_BIN_GET_LAST_INNER:
            return upipe_autoin_control_bin_output(upipe, command, args);
    }
    return upipe_autoin_control_inner(upipe, command, args);
}

/** @internal @This finds an inner pipe manager item already added.
 *
 * @param mgr auto inner pipe manager
 * @param inner_mgr inner pipe manager to find
 * @return the inner manager item if found, NULL otherwise
 */
static struct upipe_autoin_mgr_item *
upipe_autoin_mgr_find(struct upipe_mgr *mgr, struct upipe_mgr *inner_mgr)
{
    struct upipe_autoin_mgr *upipe_autoin_mgr = upipe_autoin_mgr_from_mgr(mgr);

    struct uchain *uchain;
    ulist_foreach(&upipe_autoin_mgr->inner_mgrs, uchain) {
        struct upipe_autoin_mgr_item *item =
            upipe_autoin_mgr_item_from_uchain(uchain);
        if (item->mgr == inner_mgr)
            return item;
    }
    return NULL;
}

/** @internal @This adds a pipe manager to the auto inner pipe manager.
 *
 * @param mgr auto inner pipe manager
 * @param name name use for inner uprobe prefix
 * @param inner_mgr pipe manager to attach
 * @return an error code
 */
static int _upipe_autoin_mgr_add_mgr(struct upipe_mgr *mgr,
                                     const char *name,
                                     struct upipe_mgr *inner_mgr)
{
    struct upipe_autoin_mgr *upipe_autoin_mgr = upipe_autoin_mgr_from_mgr(mgr);

    if (unlikely(inner_mgr == NULL))
        return UBASE_ERR_INVALID;

    if (unlikely(upipe_autoin_mgr_find(mgr, inner_mgr)))
        return UBASE_ERR_NONE;

    struct upipe_autoin_mgr_item *item = malloc(sizeof (*item));
    UBASE_ALLOC_RETURN(item);
    item->name = name ? strdup(name) : NULL;
    if (unlikely(name && !item->name)) {
        free(item);
        return UBASE_ERR_ALLOC;
    }
    item->mgr = upipe_mgr_use(inner_mgr);
    ulist_add(&upipe_autoin_mgr->inner_mgrs, &item->uchain);
    return UBASE_ERR_NONE;
}

/** @internal @This deletes an inner pipe manager.
 *
 * @param mgr auto inner pipe manager
 * @param inner_mgr inner manager to delete
 */
static void _upipe_autoin_mgr_del_mgr(struct upipe_mgr *mgr,
                                      struct upipe_mgr *inner_mgr)
{
    struct upipe_autoin_mgr_item *item =
        upipe_autoin_mgr_find(mgr, inner_mgr);
    if (item) {
        ulist_delete(&item->uchain);
        upipe_mgr_release(item->mgr);
        free(item->name);
        free(item);
    }
}

/** @internal @This frees an auto inner pipe manager.
 *
 * @param upipe_autoin_mgr pipe manager to free
 */
static void upipe_autoin_mgr_free(struct upipe_autoin_mgr *upipe_autoin_mgr)
{
    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_autoin_mgr->inner_mgrs))) {
        struct upipe_autoin_mgr_item *item =
            upipe_autoin_mgr_item_from_uchain(uchain);
        upipe_mgr_release(item->mgr);
        free(item->name);
        free(item);
    }
    upipe_autoin_mgr_clean_urefcount(upipe_autoin_mgr);
    free(upipe_autoin_mgr);
}

/** @internal @This handles control commands of the pipe manager.
 *
 * @param mgr pipe manager
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_autoin_mgr_control(struct upipe_mgr *mgr,
                                    int command, va_list args)
{
    struct upipe_autoin_mgr *upipe_autoin_mgr = upipe_autoin_mgr_from_mgr(mgr);

    if (unlikely(!upipe_autoin_mgr_single_urefcount(upipe_autoin_mgr)))
        return UBASE_ERR_BUSY;

    switch (command) {
        case UPIPE_AUTOIN_MGR_ADD_MGR: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUTOIN_SIGNATURE);
            const char *name = va_arg(args, const char *);
            struct upipe_mgr *inner_mgr = va_arg(args, struct upipe_mgr *);
            return _upipe_autoin_mgr_add_mgr(mgr, name, inner_mgr);
        }
        case UPIPE_AUTOIN_MGR_DEL_MGR: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_AUTOIN_SIGNATURE);
            struct upipe_mgr *inner_mgr = va_arg(args, struct upipe_mgr *);
            _upipe_autoin_mgr_del_mgr(mgr, inner_mgr);
            return UBASE_ERR_NONE;
        }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @This allocates and initializes a pipe manager.
 *
 * @return an alloocated and initialized pipe manager or NULL
 */
struct upipe_mgr *upipe_autoin_mgr_alloc(void)
{
    struct upipe_autoin_mgr *upipe_autoin_mgr =
        malloc(sizeof (*upipe_autoin_mgr));
    if (unlikely(!upipe_autoin_mgr))
        return NULL;
    struct upipe_mgr *mgr = upipe_autoin_mgr_to_mgr(upipe_autoin_mgr);

    upipe_autoin_mgr_init_urefcount(upipe_autoin_mgr);
    ulist_init(&upipe_autoin_mgr->inner_mgrs);
    mgr->refcount = upipe_autoin_mgr_to_urefcount(upipe_autoin_mgr);
    mgr->signature = UPIPE_AUTOIN_SIGNATURE;
    mgr->upipe_err_str = NULL;
    mgr->upipe_command_str = NULL;
    mgr->upipe_event_str = NULL;
    mgr->upipe_alloc = upipe_autoin_alloc;
    mgr->upipe_input = upipe_autoin_bin_input;
    mgr->upipe_control = upipe_autoin_control;
    mgr->upipe_mgr_control = upipe_autoin_mgr_control;
    return mgr;
}
