/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe pipeline dumping for debug purposes
 */

#ifndef _UPIPE_UPIPE_DUMP_H_
/** @hidden */
#define _UPIPE_UPIPE_DUMP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/ubase.h"
#include "upipe/upipe.h"

/** @This represents a dumping function for pipe labels. */
typedef char *(upipe_dump_pipe_label)(struct upipe *);

/** @This represents a dumping function for flow def labels. */
typedef char *(upipe_dump_flow_def_label)(struct uref *);

/** @This converts a pipe to a label (default function).
 *
 * @param upipe upipe structure
 * @return allocated string
 */
char *upipe_dump_upipe_label_default(struct upipe *upipe);

/** @This converts a flow def to a label (default function).
 *
 * @param flow_def flow definition packet
 * @return allocated string
 */
char *upipe_dump_flow_def_label_default(struct uref *flow_def);

/** @This dumps a pipeline in dot format.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param file file pointer to write to
 * @param ulist list of sources pipes in ulist format
 * @param args list of sources pipes terminated with NULL
 */
void upipe_dump_va(upipe_dump_pipe_label pipe_label,
                   upipe_dump_flow_def_label flow_def_label,
                   FILE *file, struct uchain *ulist, va_list args);

/** @This dumps a pipeline in dot format with a variable list of arguments.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param file file pointer to write to
 * @param ulist list of sources pipes in ulist format, followed by a list of
 * source pipes terminated by NULL
 */
static inline void upipe_dump(upipe_dump_pipe_label pipe_label,
                              upipe_dump_flow_def_label flow_def_label,
                              FILE *file, struct uchain *ulist, ...)
{
    va_list args;
    va_start(args, ulist);
    upipe_dump_va(pipe_label, flow_def_label, file, ulist, args);
    va_end(args);
}

/** @This opens a file and dumps a pipeline in dot format.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param path path of the file to open
 * @param ulist list of sources pipes in ulist format
 * @param args list of sources pipes terminated with NULL
 * @return an error code
 */
int upipe_dump_open_va(upipe_dump_pipe_label pipe_label,
                       upipe_dump_flow_def_label flow_def_label,
                       const char *path, struct uchain *ulist, va_list args);

/** @This opens a file and dumps a pipeline in dot format with a variable list
 * of arguments.
 *
 * @param pipe_label function to print pipe labels
 * @param flow_def_label function to print flow_def labels
 * @param path path of the file to open
 * @param ulist list of sources pipes in ulist format, followed by a list of
 * source pipes terminated by NULL
 * @return an error code
 */
static inline int upipe_dump_open(upipe_dump_pipe_label pipe_label,
                                  upipe_dump_flow_def_label flow_def_label,
                                  const char *path, struct uchain *ulist, ...)
{
    va_list args;
    va_start(args, ulist);
    int err = upipe_dump_open_va(pipe_label, flow_def_label, path, ulist, args);
    va_end(args);
    return err;
}

#ifdef __cplusplus
}
#endif
#endif
