/*
 * Copyright (C) 2026 Open Broadcast Systems Ltd
 *
 * Authors: Kieran Kunhya
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe uref handling for multiple ubufs
 * This file defines the API to attach several ubufs to a single uref, so
 * that one uref may for instance carry video, audio and subtitles at the
 * same time.
 *
 * The primary ubuf (uref->ubuf) is entry 0 and remains fully usable by
 * legacy pipes, which only ever see the primary ubuf. Additional ubufs are
 * entries 1 and above, chained to the uref by their own uchain (which is
 * unused while a ubuf is in flight). Per-entry metadata is stored in the
 * uref attributes under indexed names ("sub[i].*"), so it is transparently
 * carried and duplicated by udict handling. All entries share the dates and
 * duration of the uref by definition, since they are part of the same uref.
 *
 * @ref uref_free frees all entries, and @ref uref_dup and @ref uref_fork
 * duplicate the whole chain (ubuf_dup is a cheap reference increment), so
 * legacy pipes transparently pass additional entries through. Note however
 * that pipes deriving urefs through internal helpers (block splice/split,
 * picture field split) do not carry the chain.
 *
 * Please note that indices of entries following a detached entry shift down
 * by one, but per-entry attributes are not renumbered; it is the
 * responsibility of the caller to delete or rewrite them.
 */

#ifndef _UPIPE_UREF_SUB_H_
/** @hidden */
#define _UPIPE_UREF_SUB_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref.h"
#include "upipe/uref_attr.h"
#include "upipe/ubuf.h"
#include "upipe/ulist.h"

#include <stdint.h>
#include <inttypes.h>

UREF_ATTR_UNSIGNED_VA(sub, flow_id, "sub[%" PRIu8"].f.id", sub ubuf flow ID,
        uint8_t index, index)
UREF_ATTR_STRING_VA(sub, def, "sub[%" PRIu8"].f.def",
        sub ubuf flow definition, uint8_t index, index)

/** @This walks through the chain of additional ubufs of a uref (entries 1
 * and above). The list may not be altered during the walk.
 *
 * @param uref pointer to uref structure
 * @param uchain iterator, to be converted with @ref ubuf_from_uchain
 */
#define uref_sub_foreach(uref, uchain)                                      \
    ulist_foreach(&(uref)->sub_ubufs, uchain)

/** @This returns the number of ubuf entries of a uref, including the
 * primary ubuf.
 *
 * @param uref pointer to uref structure
 * @return number of entries (0 if there is no ubuf at all)
 */
static inline uint8_t uref_sub_count(struct uref *uref)
{
    size_t depth = ulist_depth(&uref->sub_ubufs);
    if (depth == 0 && uref->ubuf == NULL)
        return 0;
    return depth + 1;
}

/** @This returns the ubuf at the given entry of a uref, without transferring
 * ownership. Entry 0 is the primary ubuf.
 *
 * @param uref pointer to uref structure
 * @param index entry index
 * @return pointer to ubuf, or NULL if the entry does not exist
 */
static inline struct ubuf *uref_sub_get(struct uref *uref, uint8_t index)
{
    if (index == 0)
        return uref->ubuf;
    struct uchain *uchain = ulist_at(&uref->sub_ubufs, index - 1);
    return uchain != NULL ? ubuf_from_uchain(uchain) : NULL;
}

/** @This attaches an additional ubuf to a given uref, appended at the end of
 * the chain. The ubuf pointer may no longer be used by the module
 * afterwards. The primary ubuf is not modified (use @ref uref_attach_ubuf
 * for entry 0).
 *
 * @param uref pointer to uref structure
 * @param ubuf pointer to ubuf structure to attach to uref
 * @return entry index given to the ubuf
 */
static inline uint8_t uref_sub_attach_ubuf(struct uref *uref,
                                           struct ubuf *ubuf)
{
    ulist_add(&uref->sub_ubufs, ubuf_to_uchain(ubuf));
    return ulist_depth(&uref->sub_ubufs);
}

/** @This detaches the ubuf at the given entry from a uref. The returned
 * ubuf must be freed or re-attached at some point, otherwise it will leak.
 * Entry 0 detaches the primary ubuf (see @ref uref_detach_ubuf). Indices of
 * the following entries shift down by one, but per-entry attributes are not
 * renumbered.
 *
 * @param uref pointer to uref structure
 * @param index entry index
 * @return pointer to detached ubuf, or NULL if the entry does not exist
 */
static inline struct ubuf *uref_sub_detach_ubuf(struct uref *uref,
                                                uint8_t index)
{
    if (index == 0)
        return uref_detach_ubuf(uref);
    struct uchain *uchain = ulist_at(&uref->sub_ubufs, index - 1);
    if (uchain == NULL)
        return NULL;
    ulist_delete(uchain);
    return ubuf_from_uchain(uchain);
}

/** @This replaces the ubuf at the given entry of a uref, freeing the
 * previous one. The ubuf pointer may no longer be used by the module
 * afterwards, even in case of error. Per-entry attributes are left
 * untouched.
 *
 * @param uref pointer to uref structure
 * @param ubuf pointer to ubuf structure replacing the entry
 * @param index entry index
 * @return an error code
 */
static inline int uref_sub_replace_ubuf(struct uref *uref, struct ubuf *ubuf,
                                        uint8_t index)
{
    if (index == 0) {
        uref_attach_ubuf(uref, ubuf);
        return UBASE_ERR_NONE;
    }
    struct uchain *uchain = ulist_at(&uref->sub_ubufs, index - 1);
    if (uchain == NULL) {
        ubuf_free(ubuf);
        return UBASE_ERR_INVALID;
    }
    struct uchain *prev = uchain->prev;
    ulist_delete(uchain);
    ubuf_free(ubuf_from_uchain(uchain));
    ulist_insert(prev, prev->next, ubuf_to_uchain(ubuf));
    return UBASE_ERR_NONE;
}

/** @This finds the entry carrying the given flow ID (as set with @ref
 * uref_sub_set_flow_id, including on entry 0 for the primary ubuf) and
 * returns its ubuf without transferring ownership.
 *
 * @param uref pointer to uref structure
 * @param flow_id flow ID to look for
 * @param index_p filled in with the entry index, may be NULL
 * @return pointer to ubuf, or NULL if no entry carries this flow ID
 */
static inline struct ubuf *uref_sub_find_flow_id(struct uref *uref,
                                                 uint64_t flow_id,
                                                 uint8_t *index_p)
{
    uint8_t count = uref_sub_count(uref);
    for (uint8_t i = 0; i < count; i++) {
        uint64_t v;
        if (ubase_check(uref_sub_get_flow_id(uref, &v, i)) && v == flow_id) {
            if (index_p != NULL)
                *index_p = i;
            return uref_sub_get(uref, i);
        }
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif
#endif
