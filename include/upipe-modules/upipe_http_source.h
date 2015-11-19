/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation https (the
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
 * @short Upipe source module for http GET requests
 */

#ifndef _UPIPE_MODULES_UPIPE_HTTP_SOURCE_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_HTTP_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_HTTP_SRC_SIGNATURE UBASE_FOURCC('h','t','t','p')

/** @This extends uprobe_event with specific events for http source. */
enum uprobe_http_src_event {
    UPROBE_HTTP_SRC_SENTINEL = UPROBE_LOCAL,

    /** request receive a redirect (302) response
     * with the url (const char *) */
    UPROBE_HTTP_SRC_REDIRECT,
};

/** @This converts an enum uprobe_http_src_event to a string.
 *
 * @param event the enum to convert
 * @return a string
 */
static inline const char *uprobe_http_src_event_str(int event)
{
    switch ((enum uprobe_http_src_event)event) {
    UBASE_CASE_TO_STR(UPROBE_HTTP_SRC_REDIRECT);
    case UPROBE_HTTP_SRC_SENTINEL: break;
    }
    return NULL;
}

/** @This throw a redirect event.
 *
 * @param upipe description structure of the pipe
 * @param uri the temporary uri
 * @return an error code
 */
static inline int upipe_http_src_throw_redirect(struct upipe *upipe,
                                                const char *uri)
{
    upipe_notice_va(upipe, "throw redirect to %s", uri);
    return upipe_throw(upipe, UPROBE_HTTP_SRC_REDIRECT,
                       UPIPE_HTTP_SRC_SIGNATURE, uri);
}

/** @This extends upipe_command with specific commands for http source. */
enum upipe_http_src_command {
    UPIPE_HTTP_SRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the http proxy to use (const char *) */
    UPIPE_HTTP_SRC_SET_PROXY,
};

/** @This converts an enum upipe_http_src_command to a string.
 *
 * @param cmd the enum to convert
 * @return a string
 */
static inline const char *upipe_http_src_command_str(int cmd)
{
    switch ((enum upipe_http_src_command)cmd) {
    UBASE_CASE_TO_STR(UPIPE_HTTP_SRC_SET_PROXY);
    case UPIPE_HTTP_SRC_SENTINEL: break;
    }
    return NULL;
}

/** @This sets the http proxy to use.
 *
 * @param upipe description structure of the pipe
 * @param proxy the proxy url
 * @return an error code
 */
static inline int upipe_http_src_set_proxy(struct upipe *upipe,
                                           const char *proxy)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_SET_PROXY,
                         UPIPE_HTTP_SRC_SIGNATURE, proxy);
}

/** @This extends upipe_mgr_command with specific commands for http source. */
enum upipe_http_src_mgr_command {
    UPIPE_HTTP_SRC_MGR_SENTINEL = UPIPE_MGR_CONTROL_LOCAL,

    /** get the proxy url (const char **) */
    UPIPE_HTTP_SRC_MGR_GET_PROXY,
    /** set the proxy url (const char *) */
    UPIPE_HTTP_SRC_MGR_SET_PROXY,

    /** add a cookie (const char *) */
    UPIPE_HTTP_SRC_MGR_SET_COOKIE,
    /** iterate over cookies */
    UPIPE_HTTP_SRC_MGR_ITERATE_COOKIE,
};

/** @This sets the proxy url to use by default for the new allocated pipes.
 *
 * @param mgr pointer to upipe manager
 * @param proxy the proxy url
 * @return an error code
 */
static inline int upipe_http_src_mgr_set_proxy(struct upipe_mgr *mgr,
                                               const char *proxy)
{
    return upipe_mgr_control(mgr, UPIPE_HTTP_SRC_MGR_SET_PROXY,
                             UPIPE_HTTP_SRC_SIGNATURE, proxy);
}

/** @This gets the proxy url to use by default for the new allocated pipes.
 *
 * @param mgr pointer to upipe manager
 * @param proxy_p a pointer the proxy url
 * @return an error code
 */
static inline int upipe_http_src_mgr_get_proxy(struct upipe_mgr *mgr,
                                               const char **proxy_p)
{
    return upipe_mgr_control(mgr, UPIPE_HTTP_SRC_MGR_GET_PROXY,
                             UPIPE_HTTP_SRC_SIGNATURE, proxy_p);
}

/** @This adds a cookie in the manager cookie list.
 *
 * @param mgr pointer to upipe manager
 * @param string the cookie string to add
 * @return an error code
 */
static inline int upipe_http_src_mgr_set_cookie(struct upipe_mgr *mgr,
                                                const char *string)
{
    return upipe_mgr_control(mgr, UPIPE_HTTP_SRC_MGR_SET_COOKIE,
                             UPIPE_HTTP_SRC_SIGNATURE, string);
}

/** @This iterates over the manager cookie list.
 *
 * @param mgr pointer to upipe manager
 * @param domain the domain to match
 * @param path the path to match
 * @param uchain_p iterator
 * @return an error code
 */
static inline int upipe_http_src_mgr_iterate_cookie(struct upipe_mgr *mgr,
                                                    const char *domain,
                                                    const char *path,
                                                    struct uchain **uchain_p)
{
    return upipe_mgr_control(mgr, UPIPE_HTTP_SRC_MGR_ITERATE_COOKIE,
                             UPIPE_HTTP_SRC_SIGNATURE, domain, path, uchain_p);
}

/** @This returns the management structure for all http sources.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_http_src_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
