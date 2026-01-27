/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Bin pipe decoding a flow
 */

#include "upipe/ubase.h"
#include "upipe/uprobe.h"
#include "upipe/uprobe_prefix.h"
#include "upipe/uref.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_void.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_urefcount_real.h"
#include "upipe/upipe_helper_uref_mgr.h"
#include "upipe/upipe_helper_inner.h"
#include "upipe/upipe_helper_uprobe.h"
#include "upipe/upipe_helper_bin_input.h"
#include "upipe/upipe_helper_bin_output.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_flow_def.h"
#include "upipe-modules/upipe_probe_uref.h"
#include "upipe-filters/upipe_filter_decode.h"
#include "upipe-av/upipe_avcodec_decode.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

/** @internal @This is the private context of a fdec manager. */
struct upipe_fdec_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** pointer to avcdec manager */
    struct upipe_mgr *avcdec_mgr;

    /** public upipe_mgr structure */
    struct upipe_mgr mgr;
};

UBASE_FROM_TO(upipe_fdec_mgr, upipe_mgr, upipe_mgr, mgr)
UBASE_FROM_TO(upipe_fdec_mgr, urefcount, urefcount, urefcount)

static int upipe_fdec_provide(struct upipe *upipe, struct uref *unused);

/** @internal @This is the private context of a fdec pipe. */
struct upipe_fdec {
    /** real refcount management structure */
    struct urefcount urefcount_real;
    /** refcount management structure exported to the public structure */
    struct urefcount urefcount;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uref manager request */
    struct urequest uref_mgr_request;

    /** uref serving as a dictionary for options */
    struct uref *options;
    /** configured hardware device type */
    char *hw_type;
    /** hardware device, or NULL for default device */
    char *hw_device;

    /** probe for the first inner pipe */
    struct uprobe first_inner_probe;
    /** probe for the last inner pipe */
    struct uprobe last_inner_probe;

    /** list of input bin requests */
    struct uchain input_request_list;
    /** list of output bin requests */
    struct uchain output_request_list;
    /** first inner pipe of the bin (avcdec) */
    struct upipe *first_inner;
    /** last inner pipe of the bin (probe_uref) */
    struct upipe *last_inner;
    /** output */
    struct upipe *output;

    /** upump manager for watchdog timer */
    struct upump_mgr *upump_mgr;
    /** watchdog timer */
    struct upump *timer;
    /** watchdog timer timeout */
    uint64_t timeout;
    /** is currently watched? */
    bool watched;

    /** flow def attributes */
    struct uref *flow_def_attr;
    /** input flow definition */
    struct uref *flow_def_input;

    /** public upipe structure */
    struct upipe upipe;
};

/** @hidden */
static int upipe_fdec_catch_last_inner(struct uprobe *uprobe,
                                       struct upipe *upipe,
                                       int event, va_list args);

UPIPE_HELPER_UPIPE(upipe_fdec, upipe, UPIPE_FDEC_SIGNATURE)
UPIPE_HELPER_VOID(upipe_fdec)
UPIPE_HELPER_UREFCOUNT(upipe_fdec, urefcount, upipe_fdec_no_ref)
UPIPE_HELPER_UREFCOUNT_REAL(upipe_fdec, urefcount_real, upipe_fdec_free);
UPIPE_HELPER_UREF_MGR(upipe_fdec, uref_mgr, uref_mgr_request,
                      upipe_fdec_provide, upipe_throw_provide_request, NULL)
UPIPE_HELPER_INNER(upipe_fdec, first_inner)
UPIPE_HELPER_BIN_INPUT(upipe_fdec, first_inner, input_request_list)
UPIPE_HELPER_INNER(upipe_fdec, last_inner)
UPIPE_HELPER_UPROBE(upipe_fdec, urefcount_real, last_inner_probe,
                    upipe_fdec_catch_last_inner)
UPIPE_HELPER_UPROBE(upipe_fdec, urefcount_real, first_inner_probe, NULL)
UPIPE_HELPER_BIN_OUTPUT(upipe_fdec, last_inner, output, output_request_list)
UPIPE_HELPER_UPUMP_MGR(upipe_fdec, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_fdec, timer, upump_mgr);
UPIPE_HELPER_FLOW_DEF(upipe_fdec, flow_def_input, flow_def_attr);

/** @internal @This allocates a fdec pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_fdec_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_fdec_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);
    upipe_fdec_init_urefcount(upipe);
    upipe_fdec_init_urefcount_real(upipe);
    upipe_fdec_init_uref_mgr(upipe);
    upipe_fdec_init_last_inner_probe(upipe);
    upipe_fdec_init_first_inner_probe(upipe);
    upipe_fdec_init_bin_input(upipe);
    upipe_fdec_init_bin_output(upipe);
    upipe_fdec_init_upump_mgr(upipe);
    upipe_fdec_init_timer(upipe);
    upipe_fdec_init_flow_def(upipe);
    upipe_fdec->options = NULL;
    upipe_fdec->hw_type = NULL;
    upipe_fdec->hw_device = NULL;
    upipe_fdec->timeout = UINT64_MAX;
    upipe_fdec->watched = false;

    upipe_throw_ready(upipe);
    upipe_fdec_demand_uref_mgr(upipe);

    /* allocate last inner pipe for probing */
    struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    if (unlikely(!upipe_probe_uref_mgr)) {
        upipe_err(upipe, "couldn't allocate probe uref manager");
        upipe_release(upipe);
        return NULL;
    }
    struct upipe *probe_uref = upipe_void_alloc(
        upipe_probe_uref_mgr,
        uprobe_pfx_alloc(
            uprobe_use(&upipe_fdec->last_inner_probe),
            UPROBE_LOG_VERBOSE, "probe"));
    upipe_mgr_release(upipe_probe_uref_mgr);
    if (unlikely(!probe_uref)) {
        upipe_err(upipe, "couldn't allocate probe uref pipe");
        upipe_release(upipe);
        return NULL;
    }

    upipe_fdec_store_bin_output(upipe, probe_uref);

    return upipe;
}

/** @internal @This allocates the options uref.
 *
 * @param upipe description structure of the pipe
 * @param unused unused argument
 * @return an error code
 */
static int upipe_fdec_provide(struct upipe *upipe, struct uref *unused)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);
    if (upipe_fdec->uref_mgr != NULL && upipe_fdec->options == NULL)
        upipe_fdec->options = uref_alloc_control(upipe_fdec->uref_mgr);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_fdec_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_fdec_mgr *fdec_mgr = upipe_fdec_mgr_from_upipe_mgr(upipe->mgr);
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    struct uref *flow_def_input = uref_dup(flow_def);
    if (unlikely(!flow_def_input))
        return UBASE_ERR_ALLOC;
    upipe_fdec_store_flow_def_input(upipe, flow_def_input);

    /* try with current decoder if it exists */
    if (upipe_fdec->first_inner != NULL) {
        if (ubase_check(upipe_set_flow_def(upipe_fdec->first_inner, flow_def)))
            return UBASE_ERR_NONE;
    }

    upipe_fdec_store_bin_input(upipe, NULL);

    struct upipe *avcdec = upipe_void_alloc(fdec_mgr->avcdec_mgr,
            uprobe_pfx_alloc(
                uprobe_use(&upipe_fdec->first_inner_probe),
                UPROBE_LOG_VERBOSE, "avcdec"));

    if (unlikely(avcdec == NULL)) {
        upipe_err_va(upipe, "couldn't allocate avcdec");
        return UBASE_ERR_UNHANDLED;
    }
    if (upipe_fdec->hw_type != NULL) {
        if (unlikely(!ubase_check(upipe_avcdec_set_hw_config(avcdec,
                                                             upipe_fdec->hw_type,
                                                             upipe_fdec->hw_device)))) {
            upipe_err_va(upipe, "couldn't set avcdec hw config");
            upipe_release(avcdec);
            return UBASE_ERR_UNHANDLED;
        }
    }
    if (unlikely(!ubase_check(upipe_set_flow_def(avcdec, flow_def)))) {
        upipe_err_va(upipe, "couldn't set avcdec flow def");
        upipe_release(avcdec);
        return UBASE_ERR_UNHANDLED;
    }
    if (upipe_fdec->options != NULL && upipe_fdec->options->udict != NULL) {
        const char *key = NULL;
        enum udict_type type = UDICT_TYPE_END;
        while (ubase_check(udict_iterate(upipe_fdec->options->udict, &key,
                                         &type)) && type != UDICT_TYPE_END) {
            const char *value;
            if (key == NULL ||
                !ubase_check(udict_get_string(upipe_fdec->options->udict,
                                              &value, type, key)))
                continue;
            if (!ubase_check(upipe_set_option(avcdec, key, value)))
                upipe_warn_va(upipe, "option %s=%s invalid", key, value);
        }
    }

    int ret = upipe_set_output(avcdec, upipe_fdec->last_inner);
    if (unlikely(!ubase_check(ret))) {
        upipe_err(upipe, "fail to link inner pipes");
        upipe_release(avcdec);
        return UBASE_ERR_UNHANDLED;
    }

    upipe_fdec_store_bin_input(upipe, avcdec);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the value of an option.
 *
 * @param upipe description structure of the pipe
 * @param key name of the option
 * @param value of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_fdec_get_option(struct upipe *upipe,
                                 const char *key, const char **value_p)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);
    assert(key != NULL);

    if (upipe_fdec->options == NULL)
        return UBASE_ERR_INVALID;

    return udict_get_string(upipe_fdec->options->udict, value_p,
                            UDICT_TYPE_STRING, key);
}

/** @internal @This sets the value of an option.
 *
 * @param upipe description structure of the pipe
 * @param key name of the option
 * @param value of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_fdec_set_option(struct upipe *upipe,
                                 const char *key, const char *value)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);
    assert(key != NULL);

    if (upipe_fdec->options == NULL)
        return UBASE_ERR_ALLOC;

    if (upipe_fdec->first_inner != NULL) {
        UBASE_RETURN(upipe_set_option(upipe_fdec->first_inner, key, value))
    }
    if (value != NULL)
        return udict_set_string(upipe_fdec->options->udict, value,
                                UDICT_TYPE_STRING, key);
    else
        udict_delete(upipe_fdec->options->udict, UDICT_TYPE_STRING, key);
    return UBASE_ERR_NONE;
}

/** @internal @This sets the hardware accel configuration.
 *
 * @param upipe description structure of the pipe
 * @param type hardware acceleration type (use NULL to disable)
 * @param device hardware device to open (use NULL for default)
 * @return an error code
 */
static int upipe_fdec_set_hw_config(struct upipe *upipe,
                                    const char *type,
                                    const char *device)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);

    free(upipe_fdec->hw_type);
    free(upipe_fdec->hw_device);

    upipe_fdec->hw_type = type ? strdup(type) : NULL;
    upipe_fdec->hw_device = device ? strdup(device) : NULL;

    if (upipe_fdec->first_inner != NULL) {
            UBASE_RETURN(upipe_avcdec_set_hw_config(upipe_fdec->first_inner,
                                                    type, device))
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets the watchdog timeout.
 *
 * @param upipe description structure of the pipe
 * @param timeout watchdog timeout in 27MHz ticks, UINT64_MAX to disable
 * @return an error code
 */
static int upipe_fdec_set_timeout_real(struct upipe *upipe, uint64_t timeout)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);
    if (timeout == upipe_fdec->timeout)
        return UBASE_ERR_NONE;
    upipe_fdec->timeout = timeout;
    upipe_fdec_set_timer(upipe, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This gets the configured watchdog timeout.
 *
 * @param upipe description structure of the pipe
 * @param timeout filled with the configured timeout value in 27MHz ticks,
 * UINT64_MAX means disabled
 * @return an error code
 */
static int upipe_fdec_get_timeout_real(struct upipe *upipe, uint64_t *timeout)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);
    if (timeout)
        *timeout = upipe_fdec->timeout;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a fdec pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_fdec_control_real(struct upipe *upipe,
                                   int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR:
            upipe_fdec_set_timer(upipe, NULL);
            return upipe_fdec_attach_upump_mgr(upipe);

        case UPIPE_GET_OPTION: {
            const char *key = va_arg(args, const char *);
            const char **value_p = va_arg(args, const char **);
            return upipe_fdec_get_option(upipe, key, value_p);
        }
        case UPIPE_SET_OPTION: {
            const char *key = va_arg(args, const char *);
            const char *value = va_arg(args, const char *);
            return upipe_fdec_set_option(upipe, key, value);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_fdec_set_flow_def(upipe, flow_def);
        }

        default:
            break;
    }

    if (command >= UPIPE_CONTROL_LOCAL) {
        switch (ubase_get_signature(args)) {
            case UPIPE_AVCDEC_SIGNATURE:
                UBASE_SIGNATURE_CHECK(args, UPIPE_AVCDEC_SIGNATURE);
                switch (command) {
                    case UPIPE_AVCDEC_SET_HW_CONFIG: {
                        const char *type = va_arg(args, const char *);
                        const char *device = va_arg(args, const char *);
                        return upipe_fdec_set_hw_config(upipe, type, device);
                    }
                }
                break;

            case UPIPE_FDEC_SIGNATURE: {
                switch (command) {
                    case UPIPE_FDEC_SET_TIMEOUT: {
                        UBASE_SIGNATURE_CHECK(args, UPIPE_FDEC_SIGNATURE);
                        uint64_t timeout = va_arg(args, uint64_t);
                        return upipe_fdec_set_timeout_real(upipe, timeout);
                    }
                    case UPIPE_FDEC_GET_TIMEOUT: {
                        UBASE_SIGNATURE_CHECK(args, UPIPE_FDEC_SIGNATURE);
                        uint64_t *timeout = va_arg(args, uint64_t *);
                        return upipe_fdec_get_timeout_real(upipe, timeout);
                    }
                }
                break;
            }
        }
    }

    int err = upipe_fdec_control_bin_input(upipe, command, args);
    if (err == UBASE_ERR_UNHANDLED)
        return upipe_fdec_control_bin_output(upipe, command, args);
    return err;
}

/** @internal @This catches the events of the last inner pipe.
 *
 * @param uprobe structure used to raise events
 * @param inner description structure of the last inner pipe
 * @param event event triggered by the inner pipe
 * @param args arguments of the event
 * @return an error code
 */
static int upipe_fdec_catch_last_inner(struct uprobe *uprobe,
                                       struct upipe *inner,
                                       int event, va_list args)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_last_inner_probe(uprobe);
    struct upipe *upipe = upipe_fdec_to_upipe(upipe_fdec);

    if (event == UPROBE_PROBE_UREF &&
        ubase_get_signature(args) == UPIPE_PROBE_UREF_SIGNATURE) {
        if (upipe_fdec->timer && upipe_fdec->watched)
            upump_restart(upipe_fdec->timer);
        return UBASE_ERR_NONE;
    }
    return upipe_throw_proxy(upipe, inner, event, args);
}

/** @internal @This is called when the watchdog timer timeout.
 *
 * @param timer watchdog timer
 */
static void upipe_fdec_timeout(struct upump *timer)
{
    struct upipe *upipe = upump_get_opaque(timer, struct upipe *);
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);

    upipe_warn(upipe, "watchdog timer timeout");

    upipe_fdec->watched = false;
    upipe_fdec_store_bin_input(upipe, NULL);
    struct uref *flow_def_input = upipe_fdec->flow_def_input;
    upipe_fdec->flow_def_input = NULL;
    upipe_fdec_set_flow_def(upipe, flow_def_input);
    uref_free(flow_def_input);
}

/** @internal @This checks the internal pipe state.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_fdec_check(struct upipe *upipe)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);

    /* enable timeout? */
    if (upipe_fdec->timeout == UINT64_MAX) {
        upipe_fdec_set_timer(upipe, NULL);
        return UBASE_ERR_NONE;
    }

    upipe_fdec_check_upump_mgr(upipe);
    if (!upipe_fdec->upump_mgr)
        return UBASE_ERR_NONE;

    if (!upipe_fdec->timer) {
        struct upump *timer = upump_alloc_timer(upipe_fdec->upump_mgr,
                                                upipe_fdec_timeout,
                                                upipe, upipe->refcount,
                                                upipe_fdec->timeout, 0);
        if (!timer)
            return UBASE_ERR_UPUMP;

        upipe_fdec_set_timer(upipe, timer);
        if (upipe_fdec->watched)
            upump_start(timer);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a fdec pipe and checks the
 * internal pipe state.
 *
 * @param upipe description structure of the pipe
 * @param cmd type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_fdec_control(struct upipe *upipe, int cmd, va_list args)
{
    UBASE_RETURN(upipe_fdec_control_real(upipe, cmd, args));
    return upipe_fdec_check(upipe);
}

/** @internal @This handles input buffers.
 *
 * @param upipe description structure of the pipe
 * @param uref input buffer to handle
 * @param upump_p reference to upump that generated the buffer
 */
static void upipe_fdec_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);
    struct upump *timer = upipe_fdec->timer;

    if (!upipe_fdec->watched) {
        upipe_fdec->watched = true;
        if (timer)
            upump_restart(timer);
    }

    upipe_fdec_bin_input(upipe, uref, upump_p);
}

/** @This frees a upipe.
 *
 * @param urefcount_real pointer to urefcount_real structure
 */
static void upipe_fdec_free(struct upipe *upipe)
{
    struct upipe_fdec *upipe_fdec = upipe_fdec_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_fdec->options);
    free(upipe_fdec->hw_type);
    free(upipe_fdec->hw_device);
    upipe_fdec_clean_flow_def(upipe);
    upipe_fdec_clean_timer(upipe);
    upipe_fdec_clean_upump_mgr(upipe);
    upipe_fdec_clean_first_inner_probe(upipe);
    upipe_fdec_clean_last_inner_probe(upipe);
    upipe_fdec_clean_uref_mgr(upipe);
    upipe_fdec_clean_urefcount(upipe);
    upipe_fdec_clean_urefcount_real(upipe);
    upipe_fdec_free_void(upipe);
}

/** @This is called when there is no external reference to the pipe anymore.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_fdec_no_ref(struct upipe *upipe)
{
    upipe_fdec_set_timer(upipe, NULL);
    upipe_fdec_clean_bin_input(upipe);
    upipe_fdec_clean_bin_output(upipe);
    upipe_fdec_release_urefcount_real(upipe);
}

/** @This frees a upipe manager.
 *
 * @param urefcount pointer to urefcount structure
 */
static void upipe_fdec_mgr_free(struct urefcount *urefcount)
{
    struct upipe_fdec_mgr *fdec_mgr = upipe_fdec_mgr_from_urefcount(urefcount);
    upipe_mgr_release(fdec_mgr->avcdec_mgr);

    urefcount_clean(urefcount);
    free(fdec_mgr);
}

/** @This processes control commands on a fdec manager.
 *
 * @param mgr pointer to manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_fdec_mgr_control(struct upipe_mgr *mgr,
                                  int command, va_list args)
{
    struct upipe_fdec_mgr *fdec_mgr = upipe_fdec_mgr_from_upipe_mgr(mgr);

    switch (command) {
#define GET_SET_MGR(name, NAME)                                             \
        case UPIPE_FDEC_MGR_GET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_FDEC_SIGNATURE)               \
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);       \
            *p = fdec_mgr->name##_mgr;                                      \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
        case UPIPE_FDEC_MGR_SET_##NAME##_MGR: {                             \
            UBASE_SIGNATURE_CHECK(args, UPIPE_FDEC_SIGNATURE)               \
            if (!urefcount_single(&fdec_mgr->urefcount))                    \
                return UBASE_ERR_BUSY;                                      \
            struct upipe_mgr *m = va_arg(args, struct upipe_mgr *);         \
            upipe_mgr_release(fdec_mgr->name##_mgr);                        \
            fdec_mgr->name##_mgr = upipe_mgr_use(m);                        \
            return UBASE_ERR_NONE;                                          \
        }

        GET_SET_MGR(avcdec, AVCDEC)
#undef GET_SET_MGR

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This returns the management structure for all fdec pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_fdec_mgr_alloc(void)
{
    struct upipe_fdec_mgr *fdec_mgr = malloc(sizeof(struct upipe_fdec_mgr));
    if (unlikely(fdec_mgr == NULL))
        return NULL;

    memset(fdec_mgr, 0, sizeof(*fdec_mgr));
    fdec_mgr->avcdec_mgr = NULL;

    urefcount_init(upipe_fdec_mgr_to_urefcount(fdec_mgr),
                   upipe_fdec_mgr_free);
    fdec_mgr->mgr.refcount = upipe_fdec_mgr_to_urefcount(fdec_mgr);
    fdec_mgr->mgr.signature = UPIPE_FDEC_SIGNATURE;
    fdec_mgr->mgr.upipe_alloc = upipe_fdec_alloc;
    fdec_mgr->mgr.upipe_input = upipe_fdec_input;
    fdec_mgr->mgr.upipe_control = upipe_fdec_control;
    fdec_mgr->mgr.upipe_mgr_control = upipe_fdec_mgr_control;
    return upipe_fdec_mgr_to_upipe_mgr(fdec_mgr);
}
