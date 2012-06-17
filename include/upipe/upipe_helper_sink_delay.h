/*****************************************************************************
 * upipe_helper_sink_delay.h: upipe helper functions for delay (sink pipes)
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

#ifndef _UPIPE_UPIPE_HELPER_SINK_DELAY_H_
/** @hidden */
#define _UPIPE_UPIPE_HELPER_SINK_DELAY_H_

#include <upipe/ubase.h>
#include <upipe/upipe.h>

#include <stdint.h>
#include <stdbool.h>

/** @This declares four functions dealing with the sink delay. The sink delay
 * is the offset between the system timestamp written in incoming uref
 * packets, and the real system time the packet must be output at.
 *
 * You must add one member to your private upipe structure, for instance:
 * @code
 *  uint64_t delay;
 * @end code
 *
 * You must also declare @ref #UPIPE_HELPER_UPUMP_MGR prior to using this
 * macro.
 *
 * Supposing the name of your structure is upipe_foo, it declares:
 * @list
 * @item @code
 *  void upipe_foo_init_delay(struct upipe *upipe, uint64_t delay)
 * @end code
 * Typically called in your upipe_foo_alloc() function. The @tt delay
 * parameter is used for initialization.
 *
 * @item @code
 *  bool upipe_foo_get_delay(struct upipe *upipe, uint64_t *p)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SINK_GET_DELAY: {
 *      uint64_t *p = va_arg(args, uint64_t *);
 *      return upipe_foo_get_delay(upipe, p);
 *  }
 * @end code
 *
 * @item @code
 *  bool upipe_foo_set_delay(struct upipe *upipe, uint64_t delay)
 * @end code
 * Typically called from your upipe_foo_control() handler, such as:
 * @code
 *  case UPIPE_SINK_SET_DELAY: {
 *      uint64_t delay = va_arg(args, uint64_t);
 *      return upipe_foo_set_delay(upipe, delay);
 *  }
 * @end code
 *
 * @item @code
 *  void upipe_foo_clean_delay(struct upipe *upipe)
 * @end code
 * Typically called from your upipe_foo_free() function.
 * @end list
 *
 * @param STRUCTURE name of your private upipe structure 
 * @param DELAY name of the @tt {uint64_t} field of
 * your private upipe structure
 */
#define UPIPE_HELPER_SINK_DELAY(STRUCTURE, DELAY)                           \
/** @internal @This initializes the private members for this helper.        \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param delay initial delay value                                         \
 */                                                                         \
static void STRUCTURE##_init_delay(struct upipe *upipe, uint64_t delay)     \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->DELAY = delay;                                               \
}                                                                           \
/** @internal @This gets the current delay.                                 \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param p filled in with the delay                                        \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_get_delay(struct upipe *upipe, uint64_t *p)         \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    assert(p != NULL);                                                      \
    *p = STRUCTURE->DELAY;                                                  \
    return true;                                                            \
}                                                                           \
/** @internal @This sets the delay.                                         \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 * @param delay new delay                                                   \
 * @return false in case of error                                           \
 */                                                                         \
static bool STRUCTURE##_set_delay(struct upipe *upipe, uint64_t delay)      \
{                                                                           \
    struct STRUCTURE *STRUCTURE = STRUCTURE##_from_upipe(upipe);            \
    STRUCTURE->DELAY = delay;                                               \
    STRUCTURE##_set_upump(upipe, NULL);                                     \
    return true;                                                            \
}                                                                           \
/** @internal @This cleans up the private members for this helper.          \
 *                                                                          \
 * @param upipe description structure of the pipe                           \
 */                                                                         \
static void STRUCTURE##_clean_delay(struct upipe *upipe)                    \
{                                                                           \
}

#endif
