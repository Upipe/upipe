/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_SEQUENTIAL_SOURCE_H_
# define _UPIPE_MODULES_UPIPE_SEQUENTIAL_SOURCE_H_
#ifdef __cplusplus
extern "C" {
#endif

#define UPIPE_SEQ_SRC_SIGNATURE UBASE_FOURCC('s','e','q','s')

struct upipe_mgr *upipe_seq_src_mgr_alloc(void);
int upipe_seq_src_mgr_set_source_mgr(struct upipe_mgr *mgr,
                                     struct upipe_mgr *source_mgr);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_HLS_UPIPE_SEQUENTIAL_SOURCE_H_ */
