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
 * @short unit tests for TS EIT decoder module
 */

#undef NDEBUG

#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_block.h>
#include <upipe/ubuf_block_mem.h>
#include <upipe/uref.h>
#include <upipe/uref_event.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_eit_decoder.h>
#include <upipe-ts/uref_ts_event.h>
#include <upipe-ts/uref_ts_flow.h>

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <bitstream/dvb/si.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG

static uint64_t sid = 41;
static uint64_t tsid = 42;
static uint64_t onid = 43;
static bool complete = false;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int event, va_list args)
{
    switch (event) {
        default:
            assert(0);
            break;
        case UPROBE_READY:
        case UPROBE_DEAD:
        case UPROBE_NEED_OUTPUT:
            break;
        case UPROBE_NEW_FLOW_DEF: {
            assert(complete);
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);

            uint64_t eitd_sid, eitd_tsid, eitd_onid;
            uint8_t last_table_id;
            uint64_t events;
            ubase_assert(uref_flow_get_id(uref, &eitd_sid));
            ubase_assert(uref_ts_flow_get_tsid(uref, &eitd_tsid));
            ubase_assert(uref_ts_flow_get_onid(uref, &eitd_onid));
            ubase_assert(uref_ts_flow_get_last_table_id(uref, &last_table_id));
            ubase_assert(uref_event_get_events(uref, &events));
            assert(eitd_sid == sid);
            assert(eitd_tsid == tsid);
            assert(eitd_onid == onid);
            assert(last_table_id == EIT_TABLE_ID_PF_ACTUAL);
            assert(events == 2);

            uint64_t event_id, start, duration;
            uint8_t running_status;
            const char *name, *description;
            struct tm tm;
            ubase_assert(uref_event_get_id(uref, &event_id, 0));
            ubase_assert(uref_event_get_start(uref, &start, 0));
            ubase_assert(uref_event_get_duration(uref, &duration, 0));
            ubase_assert(uref_ts_event_get_running_status(uref, &running_status, 0));
            ubase_nassert(uref_ts_event_get_scrambled(uref, 0));
            ubase_nassert(uref_event_get_name(uref, &name, 0));
            ubase_nassert(uref_event_get_description(uref, &description, 0));
            assert(event_id == 0);
            tm.tm_year = 93;
            tm.tm_mon = 10 - 1;
            tm.tm_mday = 13;
            tm.tm_hour = 12;
            tm.tm_min = 45;
            tm.tm_sec = 0;
            tm.tm_isdst = 0;
            assert(start == (uint64_t)mktime(&tm) * UCLOCK_FREQ);
            assert(duration == (uint64_t)6330 * UCLOCK_FREQ);
            assert(running_status == 3);

            ubase_assert(uref_event_get_id(uref, &event_id, 1));
            ubase_assert(uref_event_get_start(uref, &start, 1));
            ubase_assert(uref_event_get_duration(uref, &duration, 1));
            ubase_assert(uref_ts_event_get_running_status(uref, &running_status, 1));
            ubase_assert(uref_ts_event_get_scrambled(uref, 1));
            ubase_assert(uref_event_get_name(uref, &name, 1));
            ubase_assert(uref_event_get_description(uref, &description, 1));
            tm.tm_year = 93;
            tm.tm_mon = 10 - 1;
            tm.tm_mday = 13;
            tm.tm_hour = 14;
            tm.tm_min = 30;
            tm.tm_sec = 30;
            tm.tm_isdst = 0;
            assert(start == (uint64_t)mktime(&tm) * UCLOCK_FREQ);
            assert(duration == (uint64_t)60 * UCLOCK_FREQ);
            assert(running_status == 5);
            assert(!strcmp(name, "meuh"));
            assert(!strcmp(description, "coin"));

            complete = false;
            break;
        }
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
    setenv("TZ", "UTC", 1);
    tzset();

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    assert(uref_mgr != NULL);
    struct ubuf_mgr *ubuf_mgr = ubuf_block_mem_mgr_alloc(UBUF_POOL_DEPTH,
                                                         UBUF_POOL_DEPTH,
                                                         umem_mgr, -1, 0);
    assert(ubuf_mgr != NULL);
    struct uprobe uprobe;
    uprobe_init(&uprobe, catch, NULL);
    struct uprobe *uprobe_stdio = uprobe_stdio_alloc(&uprobe, stdout,
                                                     UPROBE_LOG_LEVEL);
    assert(uprobe_stdio != NULL);
    uprobe_stdio = uprobe_ubuf_mem_alloc(uprobe_stdio, umem_mgr,
                                         UBUF_POOL_DEPTH, UBUF_POOL_DEPTH);
    assert(uprobe_stdio != NULL);

    struct uref *uref;
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtseit.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_ts_eitd_mgr = upipe_ts_eitd_mgr_alloc();
    assert(upipe_ts_eitd_mgr != NULL);
    struct upipe *upipe_ts_eitd = upipe_void_alloc(upipe_ts_eitd_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "ts eitd"));
    assert(upipe_ts_eitd != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_eitd, uref));
    uref_free(uref);

    uint8_t *buffer, *eit_event;
    int size;

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            EIT_HEADER_SIZE + EIT_EVENT_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == EIT_HEADER_SIZE + EIT_EVENT_SIZE + PSI_CRC_SIZE);
    eit_init(buffer, true);
    eit_set_length(buffer, EIT_EVENT_SIZE);
    eit_set_sid(buffer, sid);
    eit_set_tsid(buffer, tsid);
    eit_set_onid(buffer, onid);
    eit_set_segment_last_sec_number(buffer, 0);
    eit_set_last_table_id(buffer, EIT_TABLE_ID_PF_ACTUAL);
    psi_set_version(buffer, 0);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 3);
    eit_event = eit_get_event(buffer, 0);
    eitn_init(eit_event);
    eitn_set_event_id(eit_event, 0);
    eitn_set_start_time(eit_event, 0xC079124500); /* 1993-10-13T12:45:00Z */
    eitn_set_duration_bcd(eit_event, 0x014530); /* 01:45:30 */
    eitn_set_running(eit_event, 3);
    eitn_set_desclength(eit_event, 0);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_eitd, uref, NULL);

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            EIT_HEADER_SIZE + EIT_EVENT_SIZE +
                            DESC4D_HEADER_SIZE +
                            strlen("meuh") + 1 + strlen("coin") + 1 +
                            PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == EIT_HEADER_SIZE + EIT_EVENT_SIZE + DESC4D_HEADER_SIZE +
           strlen("meuh") + 1 + strlen("coin") + 1 + PSI_CRC_SIZE);
    eit_init(buffer, true);
    eit_set_length(buffer, EIT_EVENT_SIZE + DESC4D_HEADER_SIZE +
                   strlen("meuh") + 1 + strlen("coin") + 1);
    eit_set_sid(buffer, sid);
    eit_set_tsid(buffer, tsid);
    eit_set_onid(buffer, onid);
    eit_set_segment_last_sec_number(buffer, 3);
    eit_set_last_table_id(buffer, EIT_TABLE_ID_PF_ACTUAL);
    psi_set_version(buffer, 0);
    psi_set_current(buffer);
    psi_set_section(buffer, 3);
    psi_set_lastsection(buffer, 3);
    eit_event = eit_get_event(buffer, 0);
    eitn_init(eit_event);
    eitn_set_event_id(eit_event, 1);
    eitn_set_start_time(eit_event, 0xC079143030); /* 1993-10-13T14:30:30Z */
    eitn_set_duration_bcd(eit_event, 0x000100); /* 00:01:00 */
    eitn_set_running(eit_event, 5);
    eitn_set_ca(eit_event);
    eitn_set_desclength(eit_event, DESC4D_HEADER_SIZE +
                        strlen("meuh") + 1 + strlen("coin") + 1);
    uint8_t *desc = descs_get_desc(eitn_get_descs(eit_event), 0);
    desc4d_init(desc);
    desc4d_set_lang(desc, (const uint8_t *)"fra");
    desc4d_set_event_name(desc, (const uint8_t *)"meuh", strlen("meuh"));
    desc4d_set_text(desc, (const uint8_t *)"coin", strlen("coin"));
    desc4d_set_length(desc);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    complete = true;
    upipe_input(upipe_ts_eitd, uref, NULL);
    assert(!complete);

    upipe_release(upipe_ts_eitd);

    upipe_mgr_release(upipe_ts_eitd_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
