/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#ifndef _UPIPE_MODULES_UPIPE_AUTO_SOURCE_H_
# define _UPIPE_MODULES_UPIPE_AUTO_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_AUTOSRC_SIGNATURE UBASE_FOURCC('a','s','r','c')

/** @This extends @ref upipe_command with specific auto source commands. */
enum upipe_auto_src_mgr_command {
    UPIPE_AUTO_SRC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** set the source manager for the given scheme */
    UPIPE_AUTO_SRC_MGR_SET_MGR,
    /** get the source manager of the given scheme */
    UPIPE_AUTO_SRC_MGR_GET_MGR,
};

/** @This sets the @tt {struct upipe_mgr *} to use for the scheme.
 *
 * @param mgr pointer to upipe auto source manager
 * @param scheme an URI scheme (ex: "http", "file", "https", ...)
 * @param mgr_src the @tt {struct upipe_mgr *} to use for the URI beginning
 * with scheme @ref scheme
 * @return an error code
 */
static inline int upipe_auto_src_mgr_set_mgr(struct upipe_mgr *mgr,
                                             const char *scheme,
                                             struct upipe_mgr *mgr_src)
{
    return upipe_mgr_control(mgr, UPIPE_AUTO_SRC_MGR_SET_MGR,
                             UPIPE_AUTOSRC_SIGNATURE, scheme, mgr_src);
}

/** @This gets the @tt {struct upipe_mgr *} used for a given scheme.
 *
 * @param mgr pointer to upipe auto source manager
 * @param scheme an URI scheme (ex: "http", "file", "https", ...)
 * @param mgr_src_p a pointer filled with the @tt {struct upipe_mgr *}
 * used for the scheme @ref scheme
 * @return an error code
 */
static inline int upipe_auto_src_mgr_get_mgr(struct upipe_mgr *mgr,
                                             const char *scheme,
                                             struct upipe_mgr **mgr_src_p)
{
    return upipe_mgr_control(mgr, UPIPE_AUTO_SRC_MGR_GET_MGR,
                             UPIPE_AUTOSRC_SIGNATURE, scheme, mgr_src_p);
}

/** @This returns the management structure for auto source.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_auto_src_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_AUTO_SOURCE_H_ */
