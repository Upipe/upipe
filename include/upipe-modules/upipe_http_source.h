/*
 * Copyright (C) 2013-2018 OpenHeadend S.A.R.L.
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

#include "upipe/upipe.h"

#define UPIPE_HTTP_SRC_SIGNATURE UBASE_FOURCC('h','t','t','p')

enum upipe_http_src_hook_code {
    UPIPE_HTTP_SRC_HOOK_TRANSPORT_READ  = 1 << 0,
    UPIPE_HTTP_SRC_HOOK_TRANSPORT_WRITE = 1 << 1,
    UPIPE_HTTP_SRC_HOOK_DATA_READ       = 1 << 2,
    UPIPE_HTTP_SRC_HOOK_DATA_WRITE      = 1 << 3,
};

/** @This stores a http source hook. */
struct upipe_http_src_hook {
    /** refcount on the structure */
    struct urefcount *urefcount;

    struct {
        /** called when the transport socket is ready for read */
        int (*read)(struct upipe_http_src_hook *, int);
        /** called when the transport socket is ready for write */
        int (*write)(struct upipe_http_src_hook *, int);
    } transport;
    struct {
        /** called when there is data for read */
        ssize_t (*read)(struct upipe_http_src_hook *, uint8_t *, size_t);
        /** called when there is space for data to write */
        ssize_t (*write)(struct upipe_http_src_hook *, const uint8_t *, size_t);
    } data;
};

static inline struct upipe_http_src_hook *
upipe_http_src_hook_use(struct upipe_http_src_hook *hook)
{
    if (hook)
        urefcount_use(hook->urefcount);
    return hook;
}

static inline void upipe_http_src_hook_release(struct upipe_http_src_hook *hook)
{
    if (hook)
        urefcount_release(hook->urefcount);
}

/** @This extends uprobe_event with specific events for http source. */
enum uprobe_http_src_event {
    UPROBE_HTTP_SRC_SENTINEL = UPROBE_LOCAL,

    /** request receive a HTTP 302 redirect response
     * with the url (const char *) */
    UPROBE_HTTP_SRC_REDIRECT,
    /** request receive an error code response
     * with the error code (unsigned int) */
    UPROBE_HTTP_SRC_ERROR,
    /** request for scheme hook (struct uref *, struct upipe_http_src_hook **)
     */
    UPROBE_HTTP_SRC_SCHEME_HOOK,
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
    UBASE_CASE_TO_STR(UPROBE_HTTP_SRC_ERROR);
    UBASE_CASE_TO_STR(UPROBE_HTTP_SRC_SCHEME_HOOK);
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

/** @This throw an error code event.
 *
 * @param upipe description structure of the pipe
 * @param code HTTP error code
 * @return an error code
 */
static inline int upipe_http_src_throw_error(struct upipe *upipe, unsigned code)
{
    upipe_notice_va(upipe, "throw http error code %u", code);
    return upipe_throw(upipe, UPROBE_HTTP_SRC_ERROR,
                       UPIPE_HTTP_SRC_SIGNATURE, code);
}

/** @This extends upipe_command with specific commands for http source. */
enum upipe_http_src_command {
    UPIPE_HTTP_SRC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the http proxy to use (const char *) */
    UPIPE_HTTP_SRC_SET_PROXY,
    /** set the http read/write timeout (uint64_t) */
    UPIPE_HTTP_SRC_SET_TIMEOUT,
    /** get the current user agent (const char **) */
    UPIPE_HTTP_SRC_GET_USER_AGENT,
    /** set the user agent to use (const char *) */
    UPIPE_HTTP_SRC_SET_USER_AGENT,
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
    UBASE_CASE_TO_STR(UPIPE_HTTP_SRC_SET_TIMEOUT);
    UBASE_CASE_TO_STR(UPIPE_HTTP_SRC_GET_USER_AGENT);
    UBASE_CASE_TO_STR(UPIPE_HTTP_SRC_SET_USER_AGENT);
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

/** @This gets the current user agent.
 *
 * @param upipe description structure of the pipe
 * @param user_agent_p filled with the current user agent
 * @return an error code
 */
static inline int upipe_http_src_get_user_agent(struct upipe *upipe,
                                                const char **user_agent_p)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_GET_USER_AGENT,
                         UPIPE_HTTP_SRC_SIGNATURE, user_agent_p);
}

/** @This sets the user agent to use.
 *
 * @param upipe description structure of the pipe
 * @param user_agent user agent to use
 * @return an error code
 */
static inline int upipe_http_src_set_user_agent(struct upipe *upipe,
                                                const char *user_agent)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_SET_USER_AGENT,
                         UPIPE_HTTP_SRC_SIGNATURE, user_agent);
}

/** @This sets the read/write timeout.
 *
 * @param upipe description structure of the pipe
 * @param timeout timeout value in 27MHz clock ticks
 * @return an error code
 */
static inline int upipe_http_src_set_timeout(struct upipe *upipe,
                                             uint64_t timeout)
{
    return upipe_control(upipe, UPIPE_HTTP_SRC_SET_TIMEOUT,
                         UPIPE_HTTP_SRC_SIGNATURE, timeout);
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

    /** get user agent (const char **) */
    UPIPE_HTTP_SRC_MGR_GET_USER_AGENT,
    /** set user agent (const char *) */
    UPIPE_HTTP_SRC_MGR_SET_USER_AGENT,
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

/** @This sets the user agent to use by default for newly allocated pipes.
 *
 * @param mgr pointer to upipe manager
 * @param user_agent the user_agent
 * @return an error code
 */
static inline int upipe_http_src_mgr_set_user_agent(struct upipe_mgr *mgr,
                                                    const char *user_agent)
{
    return upipe_mgr_control(mgr, UPIPE_HTTP_SRC_MGR_SET_USER_AGENT,
                             UPIPE_HTTP_SRC_SIGNATURE, user_agent);
}

/** @This gets the user agent to use by default for newly allocated pipes.
 *
 * @param mgr pointer to upipe manager
 * @param user_agent_p a pointer filled with the current user agent
 * @return an error code
 */
static inline int upipe_http_src_mgr_get_user_agent(struct upipe_mgr *mgr,
                                                    const char **user_agent_p)
{
    return upipe_mgr_control(mgr, UPIPE_HTTP_SRC_MGR_GET_USER_AGENT,
                             UPIPE_HTTP_SRC_SIGNATURE, user_agent_p);
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
