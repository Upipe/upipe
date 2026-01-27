/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_DVBCSA_UPIPE_DVBCSA_DECRYPT_H_
#define _UPIPE_DVBCSA_UPIPE_DVBCSA_DECRYPT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

/** @This is the dvbcsa decryption pipe signature. */
#define UPIPE_DVBCSA_DEC_SIGNATURE  UBASE_FOURCC('d','v','b','d')

/** @This returns the dvbcsa decrypt pipe management structure.
 *
 * @return a pointer to the manager
 */
struct upipe_mgr *upipe_dvbcsa_dec_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
