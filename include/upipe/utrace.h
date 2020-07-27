/*
 * Copyright (C) 2020-2026 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_UTRACE_H_
/** @hidden */
#define _UPIPE_UTRACE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/config.h"

#include <stdarg.h>

/** @hidden */
struct uprobe;
/** @hidden */
struct upipe;
/** @hidden */
struct upipe_mgr;
/** @hidden */
struct urequest;
/** @hidden */
struct upump;
/** @hidden */
struct upump_mgr;
/** @hidden */
struct uref;
/** @hidden */
struct ulog;
/** @hidden */
struct ulog_pfx;

#ifdef UPIPE_HAVE_UTRACE

# define utrace_va_copy(Args) \
    va_list utrace_args; \
    va_copy(utrace_args, Args); \
    va_list Args; \
    va_copy(Args, utrace_args); \
    va_end(utrace_args)

# define utrace_va_end(Args) \
    va_end(Args)

void utrace_uprobe_init(struct uprobe *uprobe);
void utrace_uprobe_clean(struct uprobe *uprobe);
void utrace_uprobe_throw_enter(struct uprobe *uprobe,
                               struct upipe *upipe,
                               int event, va_list args);
void utrace_uprobe_throw_leave(int err);

void utrace_upipe_alloc_enter(struct upipe_mgr *mgr,
                              struct uprobe *uprobe,
                              uint32_t signature);
void utrace_upipe_alloc_leave(struct upipe *upipe);
void utrace_upipe_init(struct upipe *upipe);
void utrace_upipe_clean(struct upipe *upipe);
void utrace_upipe_control_enter(struct upipe *upipe, int command, va_list args);
void utrace_upipe_control_leave(int err, int command, va_list args);
void utrace_upipe_throw_enter(struct upipe *upipe, int event, va_list args);
void utrace_upipe_throw_leave(int err, int event, va_list args);
void utrace_upipe_input_enter(struct upipe *upipe, struct uref *uref);
void utrace_upipe_input_leave(void);

void utrace_urequest_init(struct urequest *urequest);
void utrace_urequest_clean(struct urequest *urequest);
void utrace_urequest_free(struct urequest *urequest);
void utrace_urequest_provide_enter(struct urequest *urequest, va_list args);
void utrace_urequest_provide_leave(int err);

void utrace_upump_alloc_enter(struct upump_mgr *mgr, int event, va_list args);
void utrace_upump_alloc_leave(struct upump *upump);
void utrace_upump_control_enter(struct upump *upump, int command, va_list args);
void utrace_upump_control_leave(int err, int command, va_list args);

void utrace_ulog_init(struct ulog *ulog);
void utrace_ulog_add_prefix(struct ulog *ulog, struct ulog_pfx *prefix);

void utrace_dump_graph(const char *name);

#else

# define utrace_va_copy(Args)
# define utrace_va_end(Args)

# define utrace_uprobe_init(Uprobe)
# define utrace_uprobe_clean(Uprobe)
# define utrace_uprobe_throw_enter(Uprobe, Upipe, Event, Args)
# define utrace_uprobe_throw_leave(Err)

# define utrace_upipe_alloc_enter(Mgr, Uprobe, Signature)
# define utrace_upipe_alloc_leave(Err)
# define utrace_upipe_init(Upipe)
# define utrace_upipe_clean(Upipe)
# define utrace_upipe_control_enter(Upipe, Command, Args)
# define utrace_upipe_control_leave(Err, Command, Args)
# define utrace_upipe_throw_enter(Upipe, Event, Args)
# define utrace_upipe_throw_leave(Err, Event, Args)
# define utrace_upipe_input_enter(Upipe, Uref)
# define utrace_upipe_input_leave()

# define utrace_urequest_init(Urequest)
# define utrace_urequest_clean(Urequest)
# define utrace_urequest_free(Urequest)
# define utrace_urequest_provide_enter(Urequest, Args)
# define utrace_urequest_provide_leave(Err)

# define utrace_upump_alloc_enter(Mgr, Event, Args)
# define utrace_upump_alloc_leave(Upump)
# define utrace_upump_control_enter(Upump, Command, Args)
# define utrace_upump_control_leave(Err, Command, Args)

# define utrace_ulog_init(Ulog)
# define utrace_ulog_add_prefix(Ulog, Prefix)

# define utrace_dump_graph(Name)

#endif

#ifdef __cplusplus
}
#endif
#endif
