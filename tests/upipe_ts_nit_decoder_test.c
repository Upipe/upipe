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
 * IN NO TS SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short unit tests for TS NIT decoder module
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
#include <upipe/uref_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_std.h>
#include <upipe/upipe.h>
#include <upipe-ts/upipe_ts_nit_decoder.h>
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

static uint64_t nid = 41;
static uint64_t tsid = 42;
static uint64_t onid = 43;
static uint64_t sid = 44;
static uint8_t type = 1;
static bool complete = true;

/** definition of our uprobe */
static int catch(struct uprobe *uprobe, struct upipe *upipe,
                 int ts, va_list args)
{
    switch (ts) {
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

            uint64_t nitd_nid;
            const char *network_name;
            uint64_t ts_number;
            uint64_t descs;
            ubase_assert(uref_ts_flow_get_nid(uref, &nitd_nid));
            ubase_assert(uref_ts_flow_get_network_name(uref, &network_name));
            ubase_assert(uref_ts_flow_get_nit_ts(uref, &ts_number));
            ubase_nassert(uref_ts_flow_get_nit_descriptors(uref, &descs));
            assert(nitd_nid == nid);
            assert(!strcmp(network_name, "meuh"));
            assert(ts_number == 1);

            uint64_t nitd_tsid, nitd_onid;
            ubase_assert(uref_ts_flow_get_nit_ts_tsid(uref, &nitd_tsid, 0));
            ubase_assert(uref_ts_flow_get_nit_ts_onid(uref, &nitd_onid, 0));
            assert(nitd_tsid == tsid);
            assert(nitd_onid == onid);

            ubase_assert(uref_ts_flow_get_nit_ts_descriptors(uref, &descs, 0));
            assert(descs == 1);

            const uint8_t *desc;
            size_t size;
            ubase_assert(uref_ts_flow_get_nit_ts_descriptor(uref, &desc, &size,
                        0, 0));
            assert(size == DESC_HEADER_SIZE + DESC41_SERVICE_SIZE);
            assert(desc_get_tag(desc) == 0x41);
            assert(desc_get_length(desc) == DESC41_SERVICE_SIZE);

            const uint8_t *service = desc41_get_service((uint8_t *)desc, 0);
            assert(desc41n_get_sid(service) == sid);
            assert(desc41n_get_type(service) == type);

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
    uref = uref_block_flow_alloc_def(uref_mgr, "mpegtspsi.mpegtsnit.");
    assert(uref != NULL);

    struct upipe_mgr *upipe_ts_nitd_mgr = upipe_ts_nitd_mgr_alloc();
    assert(upipe_ts_nitd_mgr != NULL);
    struct upipe *upipe_ts_nitd = upipe_void_alloc(upipe_ts_nitd_mgr,
            uprobe_pfx_alloc(uprobe_use(uprobe_stdio), UPROBE_LOG_LEVEL,
                             "ts nitd"));
    assert(upipe_ts_nitd != NULL);
    ubase_assert(upipe_set_flow_def(upipe_ts_nitd, uref));
    uref_free(uref);

    uint8_t *buffer, *nit_ts, *desc, *nith, *service;
    int size;

    uref = uref_block_alloc(uref_mgr, ubuf_mgr,
                            NIT_HEADER_SIZE + DESC40_HEADER_SIZE +
                            strlen("meuh") + NIT_HEADER2_SIZE + NIT_TS_SIZE +
                            DESC41_HEADER_SIZE + DESC41_SERVICE_SIZE +
                            PSI_CRC_SIZE);
    assert(uref != NULL);
    size = -1;
    ubase_assert(uref_block_write(uref, 0, &size, &buffer));
    assert(size == NIT_HEADER_SIZE + DESC40_HEADER_SIZE + strlen("meuh") +
                   NIT_HEADER2_SIZE + NIT_TS_SIZE +
                   DESC41_HEADER_SIZE + DESC41_SERVICE_SIZE + PSI_CRC_SIZE);
    nit_init(buffer, true);
    nit_set_length(buffer, DESC40_HEADER_SIZE + strlen("meuh") +
                           NIT_HEADER2_SIZE + NIT_TS_SIZE +
                           DESC41_HEADER_SIZE + DESC41_SERVICE_SIZE);
    nit_set_nid(buffer, nid);
    psi_set_version(buffer, 0);
    psi_set_current(buffer);
    psi_set_section(buffer, 0);
    psi_set_lastsection(buffer, 0);
    nit_set_desclength(buffer, DESC40_HEADER_SIZE + strlen("meuh"));
    desc = descs_get_desc(nit_get_descs(buffer), 0);
    desc40_init(desc);
    desc40_set_networkname(desc, (uint8_t *)"meuh", strlen("meuh"));
    nith = nit_get_header2(buffer);
    nith_init(nith);
    nith_set_tslength(nith, NIT_TS_SIZE +
                            DESC41_HEADER_SIZE + DESC41_SERVICE_SIZE);
    nit_ts = nit_get_ts(buffer, 0);
    nitn_init(nit_ts);
    nitn_set_tsid(nit_ts, tsid);
    nitn_set_onid(nit_ts, onid);
    nitn_set_desclength(nit_ts, DESC41_HEADER_SIZE + DESC41_SERVICE_SIZE);
    desc = descs_get_desc(nitn_get_descs(nit_ts), 0);
    desc41_init(desc);
    desc_set_length(desc, DESC41_SERVICE_SIZE);
    service = desc41_get_service(desc, 0);
    desc41n_set_sid(service, sid);
    desc41n_set_type(service, type);
    psi_set_crc(buffer);
    uref_block_unmap(uref, 0);
    upipe_input(upipe_ts_nitd, uref, NULL);
    assert(!complete);

    upipe_release(upipe_ts_nitd);

    upipe_mgr_release(upipe_ts_nitd_mgr); // nop

    uref_mgr_release(uref_mgr);
    ubuf_mgr_release(ubuf_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe_stdio);
    uprobe_clean(&uprobe);

    return 0;
}
