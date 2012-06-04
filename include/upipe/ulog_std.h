/*****************************************************************************
 * ulog_std.h: declarations for the standard toolbox for logging
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

#ifndef _UPIPE_ULOG_STD_H_
/** @hidden */
#define _UPIPE_ULOG_STD_H_

#include <stdio.h>

#include <upipe/ulog.h>

/** @This allocates a new ulog structure.
 *
 * @param stream file stream to write to (eg. stderr)
 * @param log_level minimum level of messages printed to the console
 * @param name name of this section of pipe (informative)
 * @return pointer to ulog, or NULL in case of error
 */
struct ulog *ulog_std_alloc(FILE *stream, enum ulog_level log_level,
                            const char *name);

/** @This allocates a new ulog structure, with composite name.
 *
 * @param stream file stream to write to (eg. stderr)
 * @param log_level minimum level of messages printed to the console
 * @param format printf-format string used for this section of pipe, followed
 * by optional arguments
 * @return pointer to ulog, or NULL in case of error
 */
struct ulog *ulog_std_alloc_va(FILE *stream, enum ulog_level log_level,
                               const char *format, ...);

#endif
