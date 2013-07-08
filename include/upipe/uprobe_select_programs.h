/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
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
 * @short probe catching add_flow events and forwarding flows of some programs
 *
 * The probe catches the add_flow events for optional program flows (of flow
 * definition "program."), meaning that it is necessary to decode the program
 * description, and decides whether to allocate such a demux void subpipe.
 *
 * It also catches add_flow events for elementary streams and only exports
 * (ie. forwards upstream) those that are in the selected programs.
 *
 * In case of a change of configuration, or if programs are added or deleted,
 * the selections are reconsidered and appropriate del_flows/add_flows are
 * emitted.
 */

#ifndef _UPIPE_UPROBE_SELECT_PROGRAMS_H_
/** @hidden */
#define _UPIPE_UPROBE_SELECT_PROGRAMS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>

/** @This allocates a new uprobe_selprog structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param programs comma-separated list of programs or attribute/value pairs
 * (name=ABC) to select, terminated by a comma, or "auto" to automatically
 * select the first program carrying elementary streams, or "all"
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_selprog_alloc(struct uprobe *next, const char *programs);

/** @This frees a uprobe_selprog structure.
 *
 * @param uprobe structure to free
 * @return next probe
 */
struct uprobe *uprobe_selprog_free(struct uprobe *uprobe);

/** @This returns the programs selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param programs_p filled in with a comma-separated list of programs or
 * attribute/value pairs (name=ABC) to select, terminated by a comma, or "all",
 * or "auto" if no program has been found yet
 */
void uprobe_selprog_get(struct uprobe *uprobe, const char **programs_p);

/** @This returns a list of all the programs available.
 *
 * @param uprobe pointer to probe
 * @param programs_p filled in with a comma-separated list of all programs,
 * terminated by a comma
 */
void uprobe_selprog_list(struct uprobe *uprobe, const char **programs_p);

/** @This changes the programs selected by this probe.
 *
 * @param uprobe pointer to probe
 * @param programs comma-separated list of programs or attribute/value pairs
 * (name=ABC) to select, terminated by a comma, or "auto" to automatically
 * select the first program carrying elementary streams, or "all"
 */
void uprobe_selprog_set(struct uprobe *uprobe, const char *programs);

/** @This changes the programs selected by this probe, with printf-style
 * syntax.
 *
 * @param uprobe pointer to probe
 * @param format format of the syntax, followed by optional arguments
 */
void uprobe_selprog_set_va(struct uprobe *uprobe, const char *format, ...);

#ifdef __cplusplus
}
#endif
#endif
