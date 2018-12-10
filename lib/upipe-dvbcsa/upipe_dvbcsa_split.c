/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
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

#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_urefcount_real.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_uprobe.h>
#include <upipe/upipe_helper_inner.h>
#include <upipe/upipe_helper_bin_input.h>
#include <upipe/upipe_helper_bin_output.h>

#include <upipe-dvbcsa/upipe_dvbcsa_split.h>

#include <upipe-modules/upipe_dup.h>

#include <upipe-ts/upipe_ts_demux.h>

#include <upipe/uprobe_select_flows.h>
#include <upipe/uprobe_prefix.h>

struct pid {
    uint64_t value;
    uint64_t pmt;
    struct uchain uchain;
};

UBASE_FROM_TO(pid, uchain, uchain, uchain);

/** @internal @This is the private structure for dvbcsa split pipes. */
struct upipe_dvbcsa_split {
    /** public pipe structure */
    struct upipe upipe;
    /** refcount structure */
    struct urefcount urefcount;
    /** real refcount structure */
    struct urefcount urefcount_real;
    /** input urequest list */
    struct uchain input_requests;
    /** output request list */
    struct uchain output_requests;
    /** output pipe */
    struct upipe *output;
    /** first inner dup pipe */
    struct upipe *dup;
    /** inner ts demux pipe */
    struct upipe *demux;
    /** generic proxy probe */
    struct uprobe proxy_probe;
    /** inner ts demux probe */
    struct uprobe demux_probe;
    /** list of pids */
    struct uchain pids;
};

/** @hidden */
static int upipe_dvbcsa_split_catch_demux(struct uprobe *uprobe,
                                          struct upipe *upipe,
                                          int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_dvbcsa_split, upipe, UPIPE_DVBCSA_SPLIT_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_dvbcsa_split, urefcount,
                       upipe_dvbcsa_split_no_ref);
UPIPE_HELPER_UREFCOUNT_REAL(upipe_dvbcsa_split, urefcount_real,
                            upipe_dvbcsa_split_free);
UPIPE_HELPER_VOID(upipe_dvbcsa_split);
UPIPE_HELPER_UPROBE(upipe_dvbcsa_split, urefcount_real, proxy_probe, NULL);
UPIPE_HELPER_UPROBE(upipe_dvbcsa_split, urefcount_real, demux_probe,
                    upipe_dvbcsa_split_catch_demux);
UPIPE_HELPER_INNER(upipe_dvbcsa_split, dup);
UPIPE_HELPER_INNER(upipe_dvbcsa_split, demux);
UPIPE_HELPER_BIN_INPUT(upipe_dvbcsa_split, dup, input_requests);
UPIPE_HELPER_BIN_OUTPUT(upipe_dvbcsa_split, dup, output, output_requests);

static int upipe_dvbcsa_split_throw_add_pid(struct upipe *upipe,
                                            uint64_t pid)
{
    upipe_notice_va(upipe, "throw add pid %"PRIu64, pid);
    return upipe_throw(upipe, UPROBE_DVBCSA_SPLIT_ADD_PID,
                       UPIPE_DVBCSA_SPLIT_SIGNATURE, pid);
}

static int upipe_dvbcsa_split_throw_del_pid(struct upipe *upipe,
                                            uint64_t pid)
{
    upipe_notice_va(upipe, "throw del pid %"PRIu64, pid);
    return upipe_throw(upipe, UPROBE_DVBCSA_SPLIT_DEL_PID,
                       UPIPE_DVBCSA_SPLIT_SIGNATURE, pid);
}

/** @inernal @This catches event from the inner ts pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner inner ts demux pipe
 * @param event event raised
 * @param args optional arguments
 * @return an error code
 */
static int upipe_dvbcsa_split_catch_demux(struct uprobe *uprobe,
                                          struct upipe *inner,
                                          int event, va_list args)
{
    struct upipe_dvbcsa_split *upipe_dvbcsa_split =
        upipe_dvbcsa_split_from_demux_probe(uprobe);
    struct upipe *upipe = upipe_dvbcsa_split_to_upipe(upipe_dvbcsa_split);

    switch (event) {
        case UPROBE_SPLIT_UPDATE: {
            struct uref *current_flow_def;
            int ret = upipe_get_flow_def(inner, &current_flow_def);
            if (unlikely(!ubase_check(ret))) {
                UBASE_FATAL(upipe, ret);
                return ret;
            }

            uint64_t id;
            ret = uref_flow_get_id(current_flow_def, &id);
            if (unlikely(!ubase_check(ret))) {
                UBASE_FATAL(upipe, ret);
                return ret;
            }

            upipe_split_foreach(inner, flow_def) {
                const char *def;
                ret = uref_flow_get_def(flow_def, &def);
                if (unlikely(!ubase_check(ret)))
                    continue;

                if (!strstr(def, ".pic.") && !strstr(def, ".sound."))
                    continue;

                uint64_t pid;
                ret = uref_ts_flow_get_pid(flow_def, &pid);
                if (unlikely(!ubase_check(ret)))
                    continue;

                upipe_notice_va(upipe, "add pid %"PRIu64, pid);
                struct pid *item = malloc(sizeof (*item));
                item->value = pid;
                item->pmt = id;
                ulist_add(&upipe_dvbcsa_split->pids, &item->uchain);
                upipe_dvbcsa_split_throw_add_pid(upipe, pid);
            }
            return UBASE_ERR_NONE;
        }

        case UPROBE_SOURCE_END: {
            struct uref *current_flow_def;
            int ret = upipe_get_flow_def(inner, &current_flow_def);
            if (unlikely(!ubase_check(ret))) {
                UBASE_FATAL(upipe, ret);
                return ret;
            }

            uint64_t id;
            ret = uref_flow_get_id(current_flow_def, &id);
            if (unlikely(!ubase_check(ret))) {
                UBASE_FATAL(upipe, ret);
                return ret;
            }

            struct uchain *uchain, *tmp;
            ulist_delete_foreach(&upipe_dvbcsa_split->pids, uchain, tmp) {
                struct pid *item = pid_from_uchain(uchain);
                if (item->pmt != id)
                    continue;

                upipe_dvbcsa_split_throw_del_pid(upipe, item->value);
                ulist_delete(uchain);
                free(item);
            }
            return UBASE_ERR_NONE;
        }
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This frees a dvbcsa split pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dvbcsa_split_free(struct upipe *upipe)
{
    struct upipe_dvbcsa_split *upipe_dvbcsa_split =
        upipe_dvbcsa_split_from_upipe(upipe);

    upipe_throw_dead(upipe);

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_dvbcsa_split->pids))) {
        free(pid_from_uchain(uchain));
    }
    upipe_dvbcsa_split_clean_bin_output(upipe);
    upipe_dvbcsa_split_clean_bin_input(upipe);
    upipe_dvbcsa_split_clean_demux_probe(upipe);
    upipe_dvbcsa_split_clean_demux(upipe);
    upipe_dvbcsa_split_clean_proxy_probe(upipe);
    upipe_dvbcsa_split_clean_urefcount_real(upipe);
    upipe_dvbcsa_split_clean_urefcount(upipe);
    upipe_dvbcsa_split_free_void(upipe);
}

/** @internal @This is called when there is no more external reference to the
 * pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_dvbcsa_split_no_ref(struct upipe *upipe)
{
    upipe_dvbcsa_split_store_demux(upipe, NULL);
    upipe_dvbcsa_split_store_bin_output(upipe, NULL);
    upipe_dvbcsa_split_store_bin_input(upipe, NULL);
    upipe_dvbcsa_split_release_urefcount_real(upipe);
}

/** @internal @This allocates and initializes a dvbcsa split pipe.
 *
 * @param mgr pointer to pipe manager
 * @param uprobe structure used to raise events
 * @param sig signature of the pipe allocator
 * @param args optional arguments
 * @return an allocated and initialized pipe
 */
static struct upipe *upipe_dvbcsa_split_alloc(struct upipe_mgr *mgr,
                                              struct uprobe *uprobe,
                                              uint32_t sig,
                                              va_list args)
{
    struct upipe *upipe = upipe_dvbcsa_split_alloc_void(mgr, uprobe, sig, args);
    if (unlikely(!upipe))
        return NULL;
    struct upipe_dvbcsa_split *upipe_dvbcsa_split =
        upipe_dvbcsa_split_from_upipe(upipe);

    upipe_dvbcsa_split_init_urefcount(upipe);
    upipe_dvbcsa_split_init_urefcount_real(upipe);
    upipe_dvbcsa_split_init_proxy_probe(upipe);
    upipe_dvbcsa_split_init_demux_probe(upipe);
    upipe_dvbcsa_split_init_demux(upipe);
    upipe_dvbcsa_split_init_bin_input(upipe);
    upipe_dvbcsa_split_init_bin_output(upipe);
    ulist_init(&upipe_dvbcsa_split->pids);

    upipe_throw_ready(upipe);

    struct upipe_mgr *upipe_dup_mgr = upipe_dup_mgr_alloc();
    if (unlikely(!upipe_dup_mgr)) {
        upipe_release(upipe);
        return NULL;
    }
    struct upipe *dup =
        upipe_void_alloc(upipe_dup_mgr,
                         uprobe_pfx_alloc(
                             uprobe_use(&upipe_dvbcsa_split->proxy_probe),
                             UPROBE_LOG_VERBOSE, "dup"));
    upipe_mgr_release(upipe_dup_mgr);
    if (unlikely(!dup)) {
        upipe_release(upipe);
        return NULL;
    }
    upipe_dvbcsa_split_store_bin_output(upipe, upipe_use(dup));
    upipe_dvbcsa_split_store_bin_input(upipe, dup);

    struct upipe *demux =
        upipe_void_alloc_sub(dup,
                             uprobe_pfx_alloc(
                                 uprobe_use(&upipe_dvbcsa_split->proxy_probe),
                                 UPROBE_LOG_VERBOSE, "sub demux"));
    if (unlikely(!demux)) {
        upipe_release(upipe);
        return NULL;
    }
    upipe_dvbcsa_split_store_demux(upipe, demux);

    struct upipe_mgr *upipe_ts_demux_mgr = upipe_ts_demux_mgr_alloc();
    if (unlikely(!upipe_ts_demux_mgr)) {
        upipe_release(upipe);
        return NULL;
    }
    demux =
        upipe_void_alloc_output(
            demux, upipe_ts_demux_mgr,
            uprobe_pfx_alloc(
                uprobe_selflow_alloc(
                    uprobe_use(&upipe_dvbcsa_split->proxy_probe),
                    uprobe_use(&upipe_dvbcsa_split->demux_probe),
                    UPROBE_SELFLOW_VOID, "all"),
                UPROBE_LOG_VERBOSE, "demux"));
    upipe_mgr_release(upipe_ts_demux_mgr);
    if (unlikely(!demux)) {
        upipe_release(upipe);
        return NULL;
    }
    upipe_release(demux);

    return upipe;
}

/** @internal @This handles control commands.
 *
 * @param upipe description structure of the pipe
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static int upipe_dvbcsa_split_control(struct upipe *upipe,
                                      int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_dvbcsa_split_control_bin_input(upipe, command, args));
    UBASE_HANDLED_RETURN(
        upipe_dvbcsa_split_control_bin_output(upipe, command, args));

    return UBASE_ERR_NONE;
}

/** @internal @This is the static dvbcsa split pipe manager. */
static struct upipe_mgr upipe_dvbcsa_split_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DVBCSA_SPLIT_SIGNATURE,
    .upipe_alloc = upipe_dvbcsa_split_alloc,
    .upipe_input = upipe_dvbcsa_split_bin_input,
    .upipe_control = upipe_dvbcsa_split_control,
};

/** @This returns the dvbcsa split pipe manager.
 *
 * @return a pointer to the manager
 */
struct upipe_mgr *upipe_dvbcsa_split_mgr_alloc(void)
{
    return &upipe_dvbcsa_split_mgr;
}
