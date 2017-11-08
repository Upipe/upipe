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

#ifndef _UPIPE_DVBCSA_COMMON_H_
#define _UPIPE_DVBCSA_COMMON_H_

#include <upipe/ubase.h>
#include <upipe/uclock.h>

/** default maximum latency */
#define UPIPE_DVBCSA_MAX_LATENCY UCLOCK_FREQ

/** @This is the item of a pid list. */
struct upipe_dvbcsa_common_pid {
    /** link into the list */
    struct uchain uchain;
    /** value */
    uint64_t value;
};

/** @hidden */
UBASE_FROM_TO(upipe_dvbcsa_common_pid, uchain, uchain, uchain);

/** @This is the common structure for dvbcsa pipes. */
struct upipe_dvbcsa_common {
    /** list of pids */
    struct uchain pids;
    /** maximum latency */
    uint64_t latency;
};

/** @This initializes the common structure.
 *
 * @param common pointer to the common structure
 */
static inline void upipe_dvbcsa_common_init(struct upipe_dvbcsa_common *common)
{
    common->latency = UPIPE_DVBCSA_MAX_LATENCY;
    ulist_init(&common->pids);
}

/** @This cleans the common structure.
 *
 * @param common pointer to the common structure
 */
static inline void
upipe_dvbcsa_common_clean(struct upipe_dvbcsa_common *common)
{
    struct uchain *uchain;
    while ((uchain = ulist_pop(&common->pids)))
        free(upipe_dvbcsa_common_pid_from_uchain(uchain));
}

/** @This gets a pid item from the list.
 *
 * @param common pointer to the common structure
 * @param value pid value to look for
 * @return a pointer to the item or NULL if not found
 */
static inline struct upipe_dvbcsa_common_pid *
upipe_dvbcsa_common_get_pid(struct upipe_dvbcsa_common *common, uint64_t value)
{
    struct uchain *uchain;
    ulist_foreach(&common->pids, uchain) {
        struct upipe_dvbcsa_common_pid *pid =
            upipe_dvbcsa_common_pid_from_uchain(uchain);
        if (pid->value == value)
            return pid;
    }
    return NULL;
}

/** @This adds a pid into the list if needed.
 *
 * @param common pointer to the common structure
 * @param value pid value to add into the list
 * @return an error code
 */
static inline int
upipe_dvbcsa_common_add_pid(struct upipe_dvbcsa_common *common, uint64_t value)
{
    struct upipe_dvbcsa_common_pid *pid =
        upipe_dvbcsa_common_get_pid(common, value);
    if (pid)
        return UBASE_ERR_NONE;

    pid = malloc(sizeof (*pid));
    UBASE_ALLOC_RETURN(pid);
    pid->value = value;
    ulist_add(&common->pids, &pid->uchain);
    return UBASE_ERR_NONE;
}

/** @This removes a pid from the list if needed.
 *
 * @param common pointer to the common structure
 * @param value pid value to remove from the list
 */
static inline void
upipe_dvbcsa_common_del_pid(struct upipe_dvbcsa_common *common, uint64_t value)
{
    struct upipe_dvbcsa_common_pid *pid =
        upipe_dvbcsa_common_get_pid(common, value);
    if (pid) {
        ulist_delete(&pid->uchain);
        free(pid);
    }
}

/** @This checks if a pid is present in the list.
 *
 * @param common pointer to the common structure
 * @param value pid value to check for
 * @return true if the pid is present, false otherwise
 */
static inline bool
upipe_dvbcsa_common_check_pid(struct upipe_dvbcsa_common *common,
                              uint64_t value)
{
    return upipe_dvbcsa_common_get_pid(common, value) ? true : false;
}

/** @This sets the maximum latency of a dvbcsa pipe.
 *
 * @param common pointer to the common structure
 * @param latency maximum latency
 * @return an error code
 */
static inline int
upipe_dvbcsa_common_set_max_latency(struct upipe_dvbcsa_common *common,
                                    uint64_t latency)
{
    common->latency = latency;
    return UBASE_ERR_NONE;
}

/** @This handles common control commands.
 *
 * @param common pointer to the common structure
 * @param command control command to handle
 * @param args optional arguments
 * @return an error code
 */
static inline int
upipe_dvbcsa_common_control(struct upipe_dvbcsa_common *common,
                            int command, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int ret = UBASE_ERR_UNHANDLED;

    switch (command) {
        case UPIPE_DVBCSA_ADD_PID: {
            UBASE_SIGNATURE_CHECK(args_copy, UPIPE_DVBCSA_COMMON_SIGNATURE);
            uint64_t pid = va_arg(args_copy, uint64_t);
            ret = upipe_dvbcsa_common_add_pid(common, pid);
            break;
        }

        case UPIPE_DVBCSA_DEL_PID: {
            UBASE_SIGNATURE_CHECK(args_copy, UPIPE_DVBCSA_COMMON_SIGNATURE);
            uint64_t pid = va_arg(args_copy, uint64_t);
            upipe_dvbcsa_common_del_pid(common, pid);
            ret = UBASE_ERR_NONE;
            break;
        }

        case UPIPE_DVBCSA_SET_MAX_LATENCY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_DVBCSA_COMMON_SIGNATURE);
            uint64_t latency = va_arg(args, uint64_t);
            return upipe_dvbcsa_common_set_max_latency(common, latency);
        }
    }

    va_end(args_copy);
    return ret;
}

#endif
