/*
 * Copyright (C) 2014-2016 OpenHeadend S.A.R.L.
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
 * @short probe catching need_upump_mgr events and providing a upump manager base on thread local storage
 */

#include <upipe/ubase.h>
#include <upipe/upump.h>
#include <upipe/uprobe.h>
#include <upipe-pthread/uprobe_pthread_upump_mgr.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

/** @This is a thread-local structure used by the probe. */
struct uprobe_pthread_upump_mgr_local {
    /** pointer to upump manager */
    struct upump_mgr *upump_mgr;
    /** true if the probe is frozen on this thread */
    unsigned int frozen;
};

/** @internal @This returns thread local storage, or allocates it if needed.
 *
 * @param uprobe pointer to probe
 * @return thread local storage, or NULL in case of error
 */
static struct uprobe_pthread_upump_mgr_local *
    uprobe_pthread_upump_mgr_tls(struct uprobe *uprobe)
{
    struct uprobe_pthread_upump_mgr *uprobe_pthread_upump_mgr =
        uprobe_pthread_upump_mgr_from_uprobe(uprobe);
    struct uprobe_pthread_upump_mgr_local *tls =
        pthread_getspecific(uprobe_pthread_upump_mgr->key);
    if (unlikely(tls == NULL)) {
        tls = malloc(sizeof(struct uprobe_pthread_upump_mgr_local));
        if (unlikely(tls == NULL))
            return NULL;
        tls->upump_mgr = NULL;
        tls->frozen = 0;
        if (unlikely(pthread_setspecific(uprobe_pthread_upump_mgr->key,
                                         tls) != 0)) {
            free(tls);
            return NULL;
        }
    }
    return tls;
}

/** @internal @This destroys thread local storage.
 *
 * @param pointer to thread local storage
 */
static void uprobe_pthread_upump_mgr_destr(void *_tls)
{
    struct uprobe_pthread_upump_mgr_local *tls = _tls;
    upump_mgr_release(tls->upump_mgr);
    free(_tls);
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_pthread_upump_mgr_throw(struct uprobe *uprobe,
                                          struct upipe *upipe,
                                          int event, va_list args)
{
    switch (event) {
        default:
            return uprobe_throw_next(uprobe, upipe, event, args);

        case UPROBE_FREEZE_UPUMP_MGR:
        case UPROBE_THAW_UPUMP_MGR: {
            struct uprobe_pthread_upump_mgr_local *tls =
                uprobe_pthread_upump_mgr_tls(uprobe);
            if (unlikely(tls == NULL))
                return UBASE_ERR_ALLOC;
            /* From now on only one thread may access *tls */
            if (event == UPROBE_FREEZE_UPUMP_MGR)
                tls->frozen++;
            else
                tls->frozen--;
            uprobe_dbg_va(uprobe, upipe, "%s upump manager (%u)",
                          event == UPROBE_FREEZE_UPUMP_MGR ? "freezing" :
                          "thawing", tls->frozen);
            return UBASE_ERR_NONE;
        }

        case UPROBE_NEED_UPUMP_MGR:
            break;
    }

    struct uprobe_pthread_upump_mgr_local *tls =
        uprobe_pthread_upump_mgr_tls(uprobe);
    if (tls == NULL)
        return UBASE_ERR_ALLOC;
    if (tls->frozen || tls->upump_mgr == NULL)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct upump_mgr **upump_mgr_p = va_arg(args, struct upump_mgr **);
    *upump_mgr_p = upump_mgr_use(tls->upump_mgr);
    return UBASE_ERR_NONE;
}

/** @This initializes an already allocated uprobe_pthread_upump_mgr structure.
 *
 * @param uprobe_pthread_upump_mgr pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_pthread_upump_mgr_init(
        struct uprobe_pthread_upump_mgr *uprobe_pthread_upump_mgr,
        struct uprobe *next)
{
    assert(uprobe_pthread_upump_mgr != NULL);
    struct uprobe *uprobe =
        uprobe_pthread_upump_mgr_to_uprobe(uprobe_pthread_upump_mgr);
    if (unlikely(pthread_key_create(&uprobe_pthread_upump_mgr->key,
                                    uprobe_pthread_upump_mgr_destr) != 0))
        return NULL;
    uprobe_init(uprobe, uprobe_pthread_upump_mgr_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_pthread_upump_mgr structure.
 *
 * @param uprobe_pthread_upump_mgr structure to clean
 */
void uprobe_pthread_upump_mgr_clean(
        struct uprobe_pthread_upump_mgr *uprobe_pthread_upump_mgr)
{
    assert(uprobe_pthread_upump_mgr != NULL);
    struct uprobe *uprobe =
        uprobe_pthread_upump_mgr_to_uprobe(uprobe_pthread_upump_mgr);
    struct uprobe_pthread_upump_mgr_local *tls =
        pthread_getspecific(uprobe_pthread_upump_mgr->key);
    if (tls != NULL)
        /* POSIX doesn't deallocate values on key deletion */
        uprobe_pthread_upump_mgr_destr(tls);
    pthread_key_delete(uprobe_pthread_upump_mgr->key);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_pthread_upump_mgr)
#undef ARGS
#undef ARGS_DECL

/** @This changes the upump_mgr set by this probe for the current thread.
 *
 * @param uprobe pointer to probe
 * @param upump_mgr new upump manager to provide to pipes
 * @return an error code
 */
int uprobe_pthread_upump_mgr_set(struct uprobe *uprobe,
                                 struct upump_mgr *upump_mgr)
{
    struct uprobe_pthread_upump_mgr_local *tls =
        uprobe_pthread_upump_mgr_tls(uprobe);
    if (unlikely(tls == NULL))
        return UBASE_ERR_ALLOC;
    upump_mgr_release(tls->upump_mgr);
    tls->upump_mgr = upump_mgr_use(upump_mgr);
    return UBASE_ERR_NONE;
}
