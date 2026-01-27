/*
 * Copyright (C) 2023 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_ID3V2_ENCAPS_H_
# define _UPIPE_MODULES_UPIPE_ID3V2_ENCAPS_H_
# ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uprobe.h"

#define UPIPE_ID3V2E_SUB_SIGNATURE  UBASE_FOURCC('i','d','3','e')
#define UPIPE_ID3V2E_SIGNATURE      UBASE_FOURCC('i','d','3','E')

/** @This returns the id3v2 pipe manager. */
struct upipe_mgr *upipe_id3v2e_mgr_alloc(void);

# ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_ID3V2_ENCAPS_H_ */
