/*
 * Copyright (C) 2013-2016 OpenHeadend S.A.R.L.
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
 * @short probe catching need_update events and creating subpipes
 *
 * The probe catches the need_update events, checks whether it is necessary
 * to output the flow, and allocates a subpipe.
 *
 * In case of a change of configuration, or if flows are added or deleted,
 * the selections are reconsidered.
 */

#ifndef _UPIPE_UPROBE_SELECT_FLOWS_H_
/** @hidden */
#define _UPIPE_UPROBE_SELECT_FLOWS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/uprobe.h>

/** @This defines types of flows to select from. */
enum uprobe_selflow_type {
    /** void flows (programs) */
    UPROBE_SELFLOW_VOID,
    /** picture flows, excepting sub pictures */
    UPROBE_SELFLOW_PIC,
    /** sound flows */
    UPROBE_SELFLOW_SOUND,
    /** sub picture flows */
    UPROBE_SELFLOW_SUBPIC
};

/** @This allocates a new uprobe_selflow structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param subprobe probe to set on flow subpipes
 * @param type type of flows to filter
 * @param flows comma-separated list of flows or attribute/value pairs
 * (lang=eng or name=ABC) to select, or "auto" to automatically select the
 * first flow, or "all"
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_selflow_alloc(struct uprobe *next,
                                    struct uprobe *subprobe,
                                    enum uprobe_selflow_type type,
                                    const char *flows);

/** @This allocates a new uprobe_selflow structure, with printf-style syntax.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param subprobe probe to set on flow subpipes
 * @param type type of flows to filter
 * @param format printf-style format for the flows, followed by optional
 * arguments
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_selflow_alloc_va(struct uprobe *next,
                                       struct uprobe *subprobe,
                                       enum uprobe_selflow_type type,
                                       const char *format, ...);

/** @This returns the flows selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param flows_p filled in with a comma-separated list of flows to select,
 * terminated by a comma, or "all", or "auto" if no flow has been found yet
 */
void uprobe_selflow_get(struct uprobe *uprobe, const char **flows_p);

/** @This changes the flows selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param flows comma-separated list of flows or attribute/value pairs
 * (lang=eng or name=ABC) to select, or "auto" to automatically select the
 * first flow, or "all"
 * @return an error code
 */
int uprobe_selflow_set(struct uprobe *uprobe, const char *flows);

/** @This changes the flows selected by this probe, with printf-style
 * syntax.
 *
 * @param uprobe pointer to probe
 * @param format format of the syntax, followed by optional arguments
 * @return an error code
 */
int uprobe_selflow_set_va(struct uprobe *uprobe, const char *format, ...);

#ifdef __cplusplus
}
#endif
#endif
