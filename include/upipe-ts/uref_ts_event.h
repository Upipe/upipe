/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
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
 * @short Upipe event attributes for TS
 */

#ifndef _UPIPE_UREF_TS_EVENT_H_
/** @hidden */
#define _UPIPE_UREF_TS_EVENT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref.h>
#include <upipe/uref_attr.h>

#include <string.h>
#include <stdint.h>

UREF_ATTR_SMALL_UNSIGNED_VA(ts_event, running_status, "te.run[%"PRIu64"]",
        event running status, uint64_t event, event)
UREF_ATTR_VOID_VA(ts_event, scrambled, "te.ca[%"PRIu64"]", scrambled event,
        uint64_t event, event)
UREF_ATTR_SMALL_UNSIGNED_VA(ts_event, descriptors, "te.descs[%"PRIu64"]",
        number of event descriptors, uint64_t event, event)
/* te.desc is defined below */

/** @This returns the event descriptor attribute of a uref.
 *
 * @param uref pointer to the uref
 * @param p pointer to the retrieved value (modified during execution)
 * @param event number of the event
 * @param nb number of the descriptor
 * @return an error code
 */
static inline int uref_ts_event_get_descriptor(struct uref *uref,
        const uint8_t **p, size_t *size_p, uint64_t event, uint8_t nb)
{
    struct udict_opaque opaque;
    int err = uref_attr_get_opaque_va(uref, &opaque, UDICT_TYPE_OPAQUE,
                                      "te.desc[%"PRIu64"][%"PRIu8"]",
                                      event, nb);
    if (ubase_check(err)) {
        *p = opaque.v;
        *size_p = opaque.size;
    }
    return err;
}

/** @This sets the event descriptor attribute of a uref.
 *
 * @param uref pointer to the uref
 * @param v value to set
 * @param event number of the event
 * @param nb number of the descriptor
 * @return an error code
 */
static inline int uref_ts_event_set_descriptor(struct uref *uref,
        const uint8_t *v, size_t size, uint64_t event, uint8_t nb)
{
    struct udict_opaque opaque;
    opaque.v = v;
    opaque.size = size;
    return uref_attr_set_opaque_va(uref, opaque, UDICT_TYPE_OPAQUE,
                                   "te.desc[%"PRIu64"][%"PRIu8"]", event, nb);
}

/** @This deletes the event descriptor attribute of a uref.
 *
 * @param uref pointer to the uref
 * @param event number of the event
 * @param nb number of the descriptor
 * @return an error code
 */
static inline int uref_ts_event_delete_descriptor(struct uref *uref,
        uint64_t event, uint8_t nb)
{
    return uref_attr_delete_va(uref, UDICT_TYPE_OPAQUE,
                               "te.desc[%"PRIu64"][%"PRIu8"]", event, nb);
}

/** @This registers a new event descritpr in the TS flow definition packet.
 *
 * @param uref pointer to the uref
 * @param desc descriptor
 * @param desc_len size of name
 * @param event number of the event
 * @return an error code
 */
static inline int uref_ts_event_add_descriptor(struct uref *uref,
        const uint8_t *desc, size_t desc_len, uint64_t event)
{
    uint8_t descriptors = 0;
    uref_ts_event_get_descriptors(uref, &descriptors, event);
    UBASE_RETURN(uref_ts_event_set_descriptors(uref, descriptors + 1, event))
    UBASE_RETURN(uref_ts_event_set_descriptor(uref, desc, desc_len,
                event, descriptors))
    return UBASE_ERR_NONE;
}

/** @This gets the total size of descriptors.
 *
 * @param uref pointer to the uref
 * @param event number of the event
 * @return the size of descriptors
 */
static inline size_t uref_ts_event_size_descriptors(struct uref *uref,
                                                    uint64_t event)
{
    uint8_t descriptors = 0;
    uref_ts_event_get_descriptors(uref, &descriptors, event);
    size_t descs_len = 0;
    for (uint8_t j = 0; j < descriptors; j++) {
        const uint8_t *desc;
        size_t desc_len;
        if (ubase_check(uref_ts_event_get_descriptor(uref, &desc, &desc_len,
                        event, j)))
            descs_len += desc_len;
    }
    return descs_len;
}

/** @This extracts all descriptors.
 *
 * @param uref pointer to the uref
 * @param descs_p filled in with the descriptors (size to be calculated with
 * @ref uref_ts_event_size_descriptors)
 * @param event number of the event
 */
static inline void uref_ts_event_extract_descriptors(struct uref *uref,
        uint8_t *descs_p, uint64_t event)
{
    uint8_t descriptors = 0;
    uref_ts_event_get_descriptors(uref, &descriptors, event);
    for (uint8_t j = 0; j < descriptors; j++) {
        const uint8_t *desc;
        size_t desc_len;
        if (ubase_check(uref_ts_event_get_descriptor(uref, &desc, &desc_len,
                        event, j))) {
            memcpy(descs_p, desc, desc_len);
            descs_p += desc_len;
        }
    }
}

#ifdef __cplusplus
}
#endif
#endif
