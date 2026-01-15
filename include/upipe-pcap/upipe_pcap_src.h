/*
 * Copyright (C) 2025 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short Upipe module to output data from a pcap
 */

#ifndef _UPIPE_PCAP_UPIPE_PCAP_SRC_H_
# define _UPIPE_PCAP_UPIPE_PCAP_SRC_H_

#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_PCAP_SRC_SIGNATURE UBASE_FOURCC('p','c','a','p')

struct upipe_mgr *upipe_pcap_src_mgr_alloc(void);

#ifdef __cplusplus
}
#endif

#endif
