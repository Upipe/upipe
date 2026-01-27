/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_M3U_READER_H_
# define _UPIPE_MODULES_UPIPE_M3U_READER_H_
#ifdef __cplusplus
extern "C" {
#endif

# define UPIPE_M3U_READER_SIGNATURE UBASE_FOURCC('m','3','u','r')

/** @This returns the management structure for m3u reader.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_m3u_reader_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_M3U_READER_H_ */
