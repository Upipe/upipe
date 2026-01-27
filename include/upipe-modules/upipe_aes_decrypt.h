/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_MODULES_UPIPE_AES_DECRYPT_H_
# define _UPIPE_MODULES_UPIPE_AES_DECRYPT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/upipe.h"

#define UPIPE_AES_DECRYPT_SIGNATURE     UBASE_FOURCC('a','e','s','d')

/** @This enumerates the padding options. */
enum upipe_aes_decrypt_padding {
    /** no padding */
    UPIPE_AES_DECRYPT_PADDING_NONE,
    /** PKCS-7 padding */
    UPIPE_AES_DECRYPT_PADDING_PKCS7,
};

/** @This extends upipe_command with specific commands for upipe_aes_decrypt
 * pipes.
 */
enum upipe_aes_decrypt_command {
    UPIPE_AES_DECRYPT_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set PKCS-7 padding (enum upipe_aes_decrypt_padding) */
    UPIPE_AES_DECRYPT_SET_PADDING,
};

/** @This sets padding support.
 *
 * @param upipe description structure of the pipe
 * @param type padding type to use
 * @return an error code
 */
static inline int
upipe_aes_decrypt_set_padding(struct upipe *upipe,
                              enum upipe_aes_decrypt_padding type)
{
    return upipe_control(upipe, UPIPE_AES_DECRYPT_SET_PADDING,
                         UPIPE_AES_DECRYPT_SIGNATURE, type);
}

struct upipe_mgr *upipe_aes_decrypt_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_MODULES_UPIPE_AES_DECRYPT_H_ */
