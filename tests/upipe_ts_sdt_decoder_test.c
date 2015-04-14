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
 * @short unit tests for TS SDT decoder module
 */

#undef NDEBUG

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
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_sdt_decoder.h>
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

static uint64_t tsid = 42;
static uint64_t onid = 43;
static unsigned int sid_sum = 0;
static unsigned int eitschedule_sum = 0;
static unsigned int eitpresent_sum = 0;
static unsigned int running_sum = 0;
static unsigned int ca_sum = 0;
static uint64_t provider_sum = 0;
static uint64_t service_sum = 0;

/** utility function adding letters in a string */
static unsigned int string_to_sum(const char *string)
{
    unsigned int sum = 0;
    while (*string)
        sum += *string++;
    return sum;
}

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
            struct uref *uref = va_arg(args, struct uref *);
            assert(uref != NULL);
            uint64_t sdtd_tsid, sdtd_onid;
            ubase_assert(uref_flow_get_id(uref, &sdtd_tsid));
            ubase_assert(uref_ts_flow_get_onid(uref, &sdtd_onid));
            assert(sdtd_tsid == tsid);
            assert(sdtd_onid == onid);
            break;
        }
        case UPROBE_SPLIT_UPDATE: {
            struct uref *flow_def = NULL;
            while (ubase_check(upipe_split_iterate(upipe, &flow_def)) &&
                   flow_def != NULL) {
                uint64_t id;
                ubase_assert(uref_flow_get_id(flow_def, &id));
                sid_sum += id;
                if (ubase_check(uref_ts_flow_get_eit(flow_def)))
                    eitpresent_sum++;
                if (ubase_check(uref_ts_flow_get_eit_schedule(flow_def)))
                    eitschedule_sum++;
                uint8_t running;
                if (ubase_check(uref_ts_flow_get_running_status(flow_def,
                                                                &running)))
                    running_sum += running;
                if (ubase_check(uref_ts_flow_get_scrambled(flow_def)))
                    ca_sum++;
                const char *string;
                if (ubase_check(uref_ts_flow_get_provider_name(flow_def,
                                                               &string)))
                    provider_sum += string_to_sum(string);
                if (ubase_check(uref_flow_get_name(flow_def, &string)))
                    service_sum += string_to_sum(string);
            }
            break;
        }
    }
    return UBASE_ERR_NONE;
}

int main(int argc, char *argv[])
{
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
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtssdt.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_ts_sdtd_mgr = upipe_ts_sdtd_mgr_alloc();
    assert(upipe_ts_sdtd_mgr != NULL);
    struct upipe *upipe_ts_sdtd = upipe_void_alloc(upipe_ts_sdtd_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "ts sdtd"));
    assert(upipe_ts_sdtd != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_sdtd, uref));
    uref_free(uref);

    uint8_t *buffer, *sdt_service;
    int size;

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            SDT_HEADER_SIZE + SDT_SERVICE_SIZE + PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == SDT_HEADER_SIZE + SDT_SERVICE_SIZE + PSI_CRC_SIZE);
    sdt_init(buffer, true);
    sdt_set_length(buffer, SDT_SERVICE_SIZE);
    sdt_set_tsid(buffer, tsid);
    sdt_set_onid(buffer, onid);
    psi_set_version(buffer, 0);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    sdt_service = sdt_get_service(buffer, 0);
    sdtn_init(sdt_service);
    sdtn_set_sid(sdt_service, 12);
    sdtn_set_eitpresent(sdt_service);
    sdtn_set_running(sdt_service, 3);
    sdtn_set_desclength(sdt_service, 0);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_sdtd, uref, NULL);
    assert(sid_sum == 12);
    assert(eitschedule_sum == 0);
    assert(eitpresent_sum == 1);
    assert(running_sum == 3);
    assert(ca_sum == 0);
    assert(provider_sum == 0);
    assert(service_sum == 0);

    sid_sum = 0;
    eitschedule_sum = 0;
    eitpresent_sum = 0;
    running_sum = 0;
    ca_sum = 0;
    provider_sum = 0;
    service_sum = 0;
    tsid++;
    onid++;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            SDT_HEADER_SIZE + SDT_SERVICE_SIZE +
                            DESC48_HEADER_SIZE +
                            strlen("meuh") + 1 + strlen("coin") + 1 +
                            PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == SDT_HEADER_SIZE + SDT_SERVICE_SIZE + DESC48_HEADER_SIZE +
           strlen("meuh") + 1 + strlen("coin") + 1 + PSI_CRC_SIZE);
    sdt_init(buffer, true);
    sdt_set_length(buffer, SDT_SERVICE_SIZE + DESC48_HEADER_SIZE +
                   strlen("meuh") + 1 + strlen("coin") + 1);
    sdt_set_tsid(buffer, tsid);
    sdt_set_onid(buffer, onid);
    psi_set_version(buffer, 1);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    sdt_service = sdt_get_service(buffer, 0);
    sdtn_init(sdt_service);
    sdtn_set_sid(sdt_service, 13);
    sdtn_set_eitpresent(sdt_service);
    sdtn_set_eitschedule(sdt_service);
    sdtn_set_running(sdt_service, 5);
    sdtn_set_ca(sdt_service);
    sdtn_set_desclength(sdt_service, DESC48_HEADER_SIZE +
                        strlen("meuh") + 1 + strlen("coin") + 1);
    uint8_t *desc = descs_get_desc(sdtn_get_descs(sdt_service), 0);
    desc48_init(desc);
    desc48_set_type(desc, 0x42);
    desc48_set_provider(desc, (const uint8_t *)"meuh", strlen("meuh"));
    desc48_set_service(desc, (const uint8_t *)"coin", strlen("coin"));
    desc48_set_length(desc);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_sdtd, uref, NULL);
    assert(sid_sum == 13);
    assert(eitschedule_sum == 1);
    assert(eitpresent_sum == 1);
    assert(running_sum == 5);
    assert(ca_sum == 1);
    assert(provider_sum == string_to_sum("meuh"));
    assert(service_sum == string_to_sum("coin"));

    sid_sum = 0;
    eitschedule_sum = 0;
    eitpresent_sum = 0;
    running_sum = 0;
    ca_sum = 0;
    provider_sum = 0;
    service_sum = 0;
    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            SDT_HEADER_SIZE + 2 *
                            (SDT_SERVICE_SIZE + DESC48_HEADER_SIZE +
                             strlen("meuh") + 1 + strlen("coin") + 1) +
                            PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == SDT_HEADER_SIZE + 2 *
           (SDT_SERVICE_SIZE + DESC48_HEADER_SIZE +
           strlen("meuh") + 1 + strlen("coin") + 1) + PSI_CRC_SIZE);
    sdt_init(buffer, true);
    sdt_set_length(buffer, 2 * (SDT_SERVICE_SIZE + DESC48_HEADER_SIZE +
                           strlen("meuh") + 1 + strlen("coin") + 1));
    sdt_set_tsid(buffer, tsid);
    sdt_set_onid(buffer, onid);
    psi_set_version(buffer, 2);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    sdt_service = sdt_get_service(buffer, 0);
    sdtn_init(sdt_service);
    sdtn_set_sid(sdt_service, 13);
    sdtn_set_eitpresent(sdt_service);
    sdtn_set_eitschedule(sdt_service);
    sdtn_set_running(sdt_service, 5);
    sdtn_set_ca(sdt_service);
    sdtn_set_desclength(sdt_service, DESC48_HEADER_SIZE +
                        strlen("meuh") + 1 + strlen("coin") + 1);
    desc = descs_get_desc(sdtn_get_descs(sdt_service), 0);
    desc48_init(desc);
    desc48_set_type(desc, 0x42);
    desc48_set_provider(desc, (const uint8_t *)"meuh", strlen("meuh"));
    desc48_set_service(desc, (const uint8_t *)"coin", strlen("coin"));
    desc48_set_length(desc);
    sdt_service = sdt_get_service(buffer, 1);
    sdtn_init(sdt_service);
    sdtn_set_sid(sdt_service, 14);
    sdtn_set_running(sdt_service, 1);
    sdtn_set_desclength(sdt_service, DESC48_HEADER_SIZE +
                        strlen("meuh") + 1 + strlen("coin") + 1);
    desc = descs_get_desc(sdtn_get_descs(sdt_service), 0);
    desc48_init(desc);
    desc48_set_type(desc, 0x43);
    desc48_set_provider(desc, (const uint8_t *)"coin", strlen("coin"));
    desc48_set_service(desc, (const uint8_t *)"meuh", strlen("meuh"));
    desc48_set_length(desc);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_sdtd, uref, NULL);
    assert(sid_sum == 13 + 14);
    assert(eitschedule_sum == 1);
    assert(eitpresent_sum == 1);
    assert(running_sum == 5 + 1);
    assert(ca_sum == 1);
    assert(provider_sum == string_to_sum("meuh") + string_to_sum("coin"));
    assert(service_sum == string_to_sum("coin") + string_to_sum("meuh"));

    upipe_release(upipe_ts_sdtd);

    upipe_mgr_release(upipe_ts_sdtd_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
