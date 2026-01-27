/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_DVBCSA_UPIPE_DVBCSA_ENCRYPT_H_
#define _UPIPE_DVBCSA_UPIPE_DVBCSA_ENCRYPT_H_
#ifdef __cplusplus
extern "C" {
#endif

/** @This is the dvbcsa encryption pipe signature. */
#define UPIPE_DVBCSA_ENC_SIGNATURE   UBASE_FOURCC('d','v','b','e')

/** @This returns the dvbcsa encrypt pipe management structure.
 *
 * @return a pointer to the manager
 */
struct upipe_mgr *upipe_dvbcsa_enc_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
