/*
 * Copyright (C) 2015 Arnaud de Turckheim
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to dump input urefs
 */

#ifndef _UPIPE_MODULES_UPIPE_DUMP_H_
# define _UPIPE_MODULES_UPIPE_DUMP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

# define UPIPE_DUMP_SIGNATURE UBASE_FOURCC('d','u','m','p')

enum upipe_dump_command {
    UPIPE_DUMP_SENTINEL = UPIPE_CONTROL_LOCAL,

    UPIPE_DUMP_SET_MAX_LEN,
    UPIPE_DUMP_SET_TEXT_MODE,
    UPIPE_DUMP_SET_FILENAME,
};

static inline int upipe_dump_set_max_len(struct upipe *upipe, size_t len)
{
    return upipe_control(upipe, UPIPE_DUMP_SET_MAX_LEN, UPIPE_DUMP_SIGNATURE,
                         len);
}

static inline int upipe_dump_set_text_mode(struct upipe *upipe)
{
    return upipe_control(upipe, UPIPE_DUMP_SET_TEXT_MODE, UPIPE_DUMP_SIGNATURE);
}

static inline int upipe_dump_set_filename(struct upipe *upipe, const char *path)
{
    return upipe_control(upipe, UPIPE_DUMP_SET_FILENAME, UPIPE_DUMP_SIGNATURE,
                         path);
}

struct upipe_mgr *upipe_dump_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
