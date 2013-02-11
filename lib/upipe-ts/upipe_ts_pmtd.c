/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the program map table of TS streams
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe-ts/upipe_ts_pmtd.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psid.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>

/** we only accept TS packets */
#define EXPECTED_FLOW_DEF "block.mpegtspsi.mpegtspmt."

/** @internal @This is the private context of a ts_pmtd pipe. */
struct upipe_ts_pmtd {
    /** currently in effect PMT table */
    struct uref *pmt;
    /** true if we received a compatible flow definition */
    bool flow_def_ok;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_pmtd, upipe)

/** @internal @This allocates a ts_pmtd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_ts_pmtd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd =
        malloc(sizeof(struct upipe_ts_pmtd));
    if (unlikely(upipe_ts_pmtd == NULL))
        return NULL;
    struct upipe *upipe = upipe_ts_pmtd_to_upipe(upipe_ts_pmtd);
    upipe_init(upipe, mgr, uprobe);
    upipe_ts_pmtd->pmt = NULL;
    upipe_ts_pmtd->flow_def_ok = false;
    urefcount_init(&upipe_ts_pmtd->refcount);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This sends the pmtd_header event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref triggering the event
 * @param pcrpid PCR PID detected in the PMT
 * @param desc_offset offset of the PMT descriptors array in uref
 * @param desc_size size of the PMT descriptors array in uref
 */
static void upipe_ts_pmtd_throw_header(struct upipe *upipe, struct uref *uref,
                                       unsigned int pcrpid,
                                       unsigned int desc_offset,
                                       unsigned int desc_size)
{
    upipe_throw(upipe, UPROBE_TS_PMTD_HEADER, UPIPE_TS_PMTD_SIGNATURE,
                uref, pcrpid, desc_offset, desc_size);
}

/** @internal @This sends the pmtd_add_es event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref triggering the event
 * @param pid PID for the ES
 * @param streamtype stream type of the ES
 * @param desc_offset offset of the ES descriptors array in uref
 * @param desc_size size of the ES descriptors array in uref
 */
static void upipe_ts_pmtd_add_es(struct upipe *upipe, struct uref *uref,
                                 unsigned int pid, unsigned int streamtype,
                                 unsigned int desc_offset,
                                 unsigned int desc_size)
{
    upipe_throw(upipe, UPROBE_TS_PMTD_ADD_ES, UPIPE_TS_PMTD_SIGNATURE,
                uref, pid, streamtype, desc_offset, desc_size);
}

/** @internal @This sends the pmtd_del_es event.
 *
 * @param upipe description structure of the pipe
 * @param uref uref triggering the event
 * @param pid PID for the ES
 */
static void upipe_ts_pmtd_del_es(struct upipe *upipe, struct uref *uref,
                                 unsigned int pid)
{
    upipe_throw(upipe, UPROBE_TS_PMTD_DEL_ES, UPIPE_TS_PMTD_SIGNATURE,
                uref, pid);
}

/** @internal @This reads the header of a PMT.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param header pointer to PMT header
 * @param header_desc pointer to PMT descriptors
 * @param header_desclength size of PMT descriptors
 */
#define UPIPE_TS_PMTD_HEADER(upipe, pmt, header, header_desc,               \
                             header_desclength)                             \
    uint8_t header_buffer[PMT_HEADER_SIZE];                                 \
    const uint8_t *header = uref_block_peek(pmt, 0, PMT_HEADER_SIZE,        \
                                            header_buffer);                 \
    const uint8_t *header_desc = NULL;                                      \
    uint16_t header_desclength = likely(header != NULL) ?                   \
                                 pmt_get_desclength(header) : 0;            \
    uint8_t header_desc_buffer[header_desclength + 1];                      \
    if (header_desclength) {                                                \
        header_desc = uref_block_peek(pmt, PMT_HEADER_SIZE,                 \
                              header_desclength, header_desc_buffer);       \
        if (unlikely(header_desc == NULL)) {                                \
            uref_block_peek_unmap(pmt, 0, PMT_HEADER_SIZE,                  \
                                  header_buffer, header);                   \
            header = NULL;                                                  \
        }                                                                   \
    }                                                                       \

/** @internal @This unmaps the header of a PMT.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param header pointer to PMT header
 * @param header_desc pointer to PMT descriptors
 * @param header_desclength size of PMT descriptors
 */
#define UPIPE_TS_PMTD_HEADER_UNMAP(upipe, pmt, header, header_desc,         \
                                   header_desclength)                       \
    bool ret = uref_block_peek_unmap(pmt, 0, PMT_HEADER_SIZE,               \
                                     header_buffer, header);                \
    if (header_desclength)                                                  \
        ret = uref_block_peek_unmap(pmt, PMT_HEADER_SIZE,                   \
                 header_desclength, header_desc_buffer, header_desc) && ret;\
    assert(ret);

/** @internal @This walks through the elementary streams in a PMT.
 * This is the first part: read data from es afterwards.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param offset pointing to the current offset in uref
 * @param header_desclength size of PMT descriptors
 * @param es iterator pointing to ES definition
 * @param desc iterator pointing to descriptors of the ES
 * @param desclength pointing to size of ES descriptors
 */
#define UPIPE_TS_PMTD_PEEK(upipe, pmt, offset, header_desclength, es, desc, \
                           desclength)                                      \
    size_t size;                                                            \
    ret = uref_block_size(pmt, &size);                                      \
    assert(ret);                                                            \
                                                                            \
    int offset = PMT_HEADER_SIZE + header_desclength;                       \
    while (offset + PMT_ES_SIZE <= size - PSI_CRC_SIZE) {                   \
        uint8_t es_buffer[PMT_ES_SIZE];                                     \
        const uint8_t *es = uref_block_peek(pmt, offset, PMT_ES_SIZE,       \
                                            es_buffer);                     \
        if (unlikely(es == NULL))                                           \
            break;                                                          \
        uint16_t desclength = pmtn_get_desclength(es);                      \
        /* + 1 is to avoid [0] */                                           \
        uint8_t desc_buffer[desclength + 1];                                \
        const uint8_t *desc;                                                \
        if (desclength) {                                                   \
            desc = uref_block_peek(pmt, offset + PMT_ES_SIZE,               \
                                   desclength, desc_buffer);                \
            if (unlikely(desc == NULL)) {                                   \
                uref_block_peek_unmap(pmt, offset, PMT_ES_SIZE, es_buffer,  \
                                      es);                                  \
                break;                                                      \
            }                                                               \
        } else                                                              \
            desc = NULL;

/** @internal @This walks through the elementary streams in a PMT.
 * This is the second part: do the actions afterwards.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param offset pointing to the current offset in uref
 * @param es iterator pointing to ES definition
 * @param desc iterator pointing to descriptors of the ES
 * @param desclength pointing to size of ES descriptors
 */
#define UPIPE_TS_PMTD_PEEK_UNMAP(upipe, pmt, offset, es, desc, desclength)  \
        ret = uref_block_peek_unmap(pmt, offset, PMT_ES_SIZE,               \
                                    es_buffer, es);                         \
        if (desc != NULL)                                                   \
            ret = uref_block_peek_unmap(pmt, offset + PMT_ES_SIZE,          \
                            desclength, desc_buffer, desc) && ret;          \
        assert(ret);                                                        \
        offset += PMT_ES_SIZE + desclength;

/** @internal @This walks through the elementary streams in a PMT.
 * This is the last part.
 *
 * @param upipe description structure of the pipe
 * @param pmt PMT table
 * @param offset pointing to the current offset in uref
 */
#define UPIPE_TS_PMTD_PEEK_END(upipe, pmt, offset)                          \
    }                                                                       \
    if (unlikely(offset + PMT_ES_SIZE <= size - PSI_CRC_SIZE)) {            \
        upipe_throw_aerror(upipe);                                          \
    }                                                                       \

/** @internal @This validates the next PMT.
 *
 * @param upipe description structure of the pipe
 * @param uref PMT section
 * @return false if the PMT is invalid
 */
static bool upipe_ts_pmtd_validate(struct upipe *upipe, struct uref *pmt)
{
    if (!upipe_ts_psid_check_crc(pmt))
        return false;

    UPIPE_TS_PMTD_HEADER(upipe, pmt, header, header_desc, header_desclength)

    if (unlikely(header == NULL))
        return false;

    bool validate = psi_get_syntax(header) && !psi_get_section(header) &&
                    !psi_get_lastsection(header) &&
                    psi_get_tableid(header) == PMT_TABLE_ID &&
                    (!header_desclength ||
                     descl_validate(header_desc, header_desclength));

    UPIPE_TS_PMTD_HEADER_UNMAP(upipe, pmt, header, header_desc,
                               header_desclength)

    if (!validate)
        return false;
    
    UPIPE_TS_PMTD_PEEK(upipe, pmt, offset, header_desclength, es, desc,
                       desclength)

    validate = !desclength || descl_validate(desc, desclength);

    UPIPE_TS_PMTD_PEEK_UNMAP(upipe, pmt, offset, es, desc, desclength)

    if (!validate)
        return false;

    UPIPE_TS_PMTD_PEEK_END(upipe, pmt, offset)
    return true;
}

/** @internal @This compares a PMT header with the current PMT.
 *
 * @param upipe description structure of the pipe
 * @param header pointer to PMT header
 * @param header_desc pointer to PMT descriptors
 * @param header_desclength size of PMT descriptors
 * @return false if the headers are different
 */
static bool upipe_ts_pmtd_compare_header(struct upipe *upipe,
                                         const uint8_t *header,
                                         const uint8_t *header_desc,
                                         uint16_t header_desclength)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (upipe_ts_pmtd->pmt == NULL)
        return false;

    UPIPE_TS_PMTD_HEADER(upipe, upipe_ts_pmtd->pmt, old_header, old_header_desc,
                         old_header_desclength)

    if (unlikely(old_header == NULL))
        return false;

    bool compare = pmt_get_pcrpid(header) == pmt_get_pcrpid(old_header) &&
                   header_desclength == old_header_desclength &&
                   (!header_desclength ||
                    !memcmp(header_desc, old_header_desc, header_desclength));

    UPIPE_TS_PMTD_HEADER_UNMAP(upipe, upipe_ts_pmtd->pmt, old_header,
                               old_header_desc, old_header_desclength)

    return compare;
}

/** @internal @This compares an elementary stream definition with the current
 * PMT.
 *
 * @param upipe description structure of the pipe
 * @param es iterator pointing to ES definition
 * @param desc iterator pointing to descriptors of the ES
 * @param desclength pointing to size of ES descriptors
 * @return false if there is no such ES, or it has a different description
 */
static bool upipe_ts_pmtd_compare_es(struct upipe *upipe, const uint8_t *es,
                                     const uint8_t *desc, uint16_t desclength)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (upipe_ts_pmtd->pmt == NULL)
        return false;

    UPIPE_TS_PMTD_HEADER(upipe, upipe_ts_pmtd->pmt, old_header, old_header_desc,
                         old_header_desclength)
    UPIPE_TS_PMTD_HEADER_UNMAP(upipe, upipe_ts_pmtd->pmt, old_header,
                               old_header_desc, old_header_desclength)

    UPIPE_TS_PMTD_PEEK(upipe, upipe_ts_pmtd->pmt, offset, old_header_desclength,
                       old_es, old_desc, old_desclength)

    uint16_t pid = pmtn_get_pid(old_es);
    bool compare = pmtn_get_streamtype(es) == pmtn_get_streamtype(old_es) &&
                   desclength == old_desclength &&
                   (!desclength || !memcmp(desc, old_desc, desclength));

    UPIPE_TS_PMTD_PEEK_UNMAP(upipe, upipe_ts_pmtd->pmt, offset, old_es,
                             old_desc, old_desclength)

    if (pid == pmtn_get_pid(es))
        return compare;

    UPIPE_TS_PMTD_PEEK_END(upipe, upipe_ts_pmtd->pmt, offset)
    return false;
}

/** @internal @This checks whether a given ES is still present in the PMT.
 *
 * @param upipe description structure of the pipe
 * @param wanted_pid elementary stream to check
 * @return false if the PID is not present
 */
static uint16_t upipe_ts_pmtd_pid(struct upipe *upipe, uint16_t wanted_pid)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (upipe_ts_pmtd->pmt == NULL)
        return false;

    UPIPE_TS_PMTD_HEADER(upipe, upipe_ts_pmtd->pmt, header, header_desc,
                         header_desclength)
    UPIPE_TS_PMTD_HEADER_UNMAP(upipe, upipe_ts_pmtd->pmt, header,
                               header_desc, header_desclength)

    UPIPE_TS_PMTD_PEEK(upipe, upipe_ts_pmtd->pmt, offset, header_desclength,
                       es, desc, desclength)

    uint16_t pid = pmtn_get_pid(es);

    UPIPE_TS_PMTD_PEEK_UNMAP(upipe, upipe_ts_pmtd->pmt, offset, es,
                             desc, desclength)

    if (pid == wanted_pid)
        return true;

    UPIPE_TS_PMTD_PEEK_END(upipe, upipe_ts_pmtd->pmt, offset)
    return false;
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static void upipe_ts_pmtd_work(struct upipe *upipe, struct uref *uref)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (upipe_ts_pmtd->pmt != NULL &&
        uref_block_compare(upipe_ts_pmtd->pmt, uref)) {
        /* Identical PMT. */
        uref_free(uref);
        return;
    }

    if (!upipe_ts_pmtd_validate(upipe, uref)) {
        upipe_warn(upipe, "invalid PMT section received");
        uref_free(uref);
        return;
    }

    UPIPE_TS_PMTD_HEADER(upipe, uref, header, header_desc, header_desclength)

    if (unlikely(header == NULL)) {
        upipe_warn(upipe, "invalid PMT section received");
        uref_free(uref);
        return;
    }

    uint16_t pcrpid = pmt_get_pcrpid(header);
    bool compare = upipe_ts_pmtd_compare_header(upipe, header, header_desc,
                                                header_desclength);

    UPIPE_TS_PMTD_HEADER_UNMAP(upipe, uref, header, header_desc,
                               header_desclength)

    if (!compare)
        upipe_ts_pmtd_throw_header(upipe, uref, pcrpid, PMT_HEADER_SIZE,
                                   header_desclength);

    UPIPE_TS_PMTD_PEEK(upipe, uref, offset, header_desclength, es, desc,
                       desclength)

    uint16_t pid = pmtn_get_pid(es);
    uint16_t streamtype = pmtn_get_streamtype(es);
    bool compare = upipe_ts_pmtd_compare_es(upipe, es, desc, desclength);
    int desc_offset = offset + PMT_ES_SIZE;

    UPIPE_TS_PMTD_PEEK_UNMAP(upipe, uref, offset, es, desc, desclength)

    if (!compare)
        upipe_ts_pmtd_add_es(upipe, uref, pid, streamtype, desc_offset,
                             desclength);

    UPIPE_TS_PMTD_PEEK_END(upipe, uref, offset)

    /* Switch tables. */
    struct uref *old_pmt = upipe_ts_pmtd->pmt;
    upipe_ts_pmtd->pmt = uref;

    if (old_pmt) {
        UPIPE_TS_PMTD_HEADER(upipe, old_pmt, old_header, old_header_desc,
                             old_header_desclength)
        UPIPE_TS_PMTD_HEADER_UNMAP(upipe, old_pmt, old_header,
                                   old_header_desc, old_header_desclength)

        UPIPE_TS_PMTD_PEEK(upipe, old_pmt, offset,
                           old_header_desclength, old_es, old_desc,
                           old_desclength)

        uint16_t pid = pmtn_get_pid(old_es);

        UPIPE_TS_PMTD_PEEK_UNMAP(upipe, old_pmt, offset, old_es,
                                 old_desc, old_desclength)

        if (!upipe_ts_pmtd_pid(upipe, pid))
            upipe_ts_pmtd_del_es(upipe, uref, pid);

        UPIPE_TS_PMTD_PEEK_END(upipe, old_pmt, offset)

        uref_free(old_pmt);
    }
}

/** @internal @This receives data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump pump that generated the buffer
 */
static void upipe_ts_pmtd_input(struct upipe *upipe, struct uref *uref,
                                struct upump *upump)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    const char *def;
    if (unlikely(uref_flow_get_def(uref, &def))) {
        if (unlikely(ubase_ncmp(def, EXPECTED_FLOW_DEF))) {
            uref_free(uref);
            upipe_ts_pmtd->flow_def_ok = false;
            upipe_throw_flow_def_error(upipe, uref);
            return;
        }

        upipe_dbg_va(upipe, "flow definition: %s", def);
        upipe_ts_pmtd->flow_def_ok = true;
        uref_free(uref);
        return;
    }

    if (unlikely(!upipe_ts_pmtd->flow_def_ok)) {
        uref_free(uref);
        upipe_throw_flow_def_error(upipe, uref);
        return;
    }

    if (unlikely(uref->ubuf == NULL)) {
        uref_free(uref);
        return;
    }

    upipe_ts_pmtd_work(upipe, uref);
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pmtd_use(struct upipe *upipe)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    urefcount_use(&upipe_ts_pmtd->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_pmtd_release(struct upipe *upipe)
{
    struct upipe_ts_pmtd *upipe_ts_pmtd = upipe_ts_pmtd_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_ts_pmtd->refcount))) {
        upipe_throw_dead(upipe);

        uref_free(upipe_ts_pmtd->pmt);

        upipe_clean(upipe);
        urefcount_clean(&upipe_ts_pmtd->refcount);
        free(upipe_ts_pmtd);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_pmtd_mgr = {
    .signature = UPIPE_TS_PMTD_SIGNATURE,

    .upipe_alloc = upipe_ts_pmtd_alloc,
    .upipe_input = upipe_ts_pmtd_input,
    .upipe_control = NULL,
    .upipe_use = upipe_ts_pmtd_use,
    .upipe_release = upipe_ts_pmtd_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all ts_pmtd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_pmtd_mgr_alloc(void)
{
    return &upipe_ts_pmtd_mgr;
}
