/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 * Copyright (C) 2023 EasyTools
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_ID3V2_H_
# define _UPIPE_MODULES_UPIPE_ID3V2_H_
# ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_ID3V2_SIGNATURE   UBASE_FOURCC('i','d','3','2')

/** @This returns the id3v2 pipe manager. */
struct upipe_mgr *upipe_id3v2_mgr_alloc(void);

# ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_ID3V2_H_ */
