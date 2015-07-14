/*
 * Copyright (C) 2015 Open Broadcast Systems Ltd
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Kieran Kunhya
 *          Christophe Massiot
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
 * @short probe outputting all log events to syslog
 */

#ifndef _UPIPE_UPROBE_SYSLOG_H_
/** @hidden */
#define _UPIPE_UPROBE_SYSLOG_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_syslog {
    /** syslog ident */
    char *ident;
    /** syslog facility */
    int facility;
    /** true if openlog was called */
    bool inited;

    /** minimum level of printed messages */
    enum uprobe_log_level min_level;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_syslog, uprobe)

/** @This initializes an already allocated uprobe_syslog structure.
 *
 * @param uprobe_syslog pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param ident syslog ident string (may be NULL)
 * @param option syslog option (see syslog(3)), or -1 to not call openlog
 * @param facility syslog facility (see syslog(3))
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_syslog_init(struct uprobe_syslog *uprobe_syslog,
                                  struct uprobe *next, const char *ident,
                                  int option, int facility,
                                  enum uprobe_log_level min_level);

/** @This cleans a uprobe_syslog structure.
 *
 * @param uprobe_syslog structure to clean
 */
void uprobe_syslog_clean(struct uprobe_syslog *uprobe_syslog);

/** @This allocates a new uprobe syslog structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param ident syslog ident string (may be NULL)
 * @param option syslog option (see syslog(3)), or -1 to not call openlog
 * @param facility syslog facility (see syslog(3))
 * @param init true if openlog(3) must be called
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_syslog_alloc(struct uprobe *next, const char *ident,
                                  int option, int facility,
                                  enum uprobe_log_level min_level);

#ifdef __cplusplus
}
#endif
#endif
