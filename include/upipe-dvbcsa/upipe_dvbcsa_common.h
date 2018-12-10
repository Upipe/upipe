/*
 * Copyright (C) 2017 OpenHeadend S.A.R.L.
 *
 * Authors: Arnaud de Turckheim
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

#ifndef _UPIPE_DVBCSA_UPIPE_DVBCSA_COMMON_H_
#define _UPIPE_DVBCSA_UPIPE_DVBCSA_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ustring.h>
#include <upipe/ubase.h>
#include <upipe/upipe.h>
#include <dvbcsa/dvbcsa.h>

/** @This is the signature for common dvbcsa pipe operations. */
#define UPIPE_DVBCSA_COMMON_SIGNATURE   UBASE_FOURCC('d','v','b',' ')

/** @This enumerates the common custom control commands. */
enum upipe_dvbcsa_command {
    /** sentinel */
    UPIPE_DVBCSA_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** set the dvbcsa key (const char *) */
    UPIPE_DVBCSA_SET_KEY,
    /** set the maximum latency (uint64_t) */
    UPIPE_DVBCSA_SET_MAX_LATENCY,

    /** add a pid to the encryption/decryption list (uint64_t) */
    UPIPE_DVBCSA_ADD_PID,
    /** delete a pid from the encryption/decryption list (uint64_t) */
    UPIPE_DVBCSA_DEL_PID,

    /** custom dvbcsa commands start here */
    UPIPE_DVBCSA_CONTROL_LOCAL,
};

/** @This sets the dvbcsa key.
 *
 * @param upipe description structure of the pipe
 * @param key dvbcsa key
 * @return an error code
 */
static inline int upipe_dvbcsa_set_key(struct upipe *upipe, const char *key)
{
    return upipe_control(upipe, UPIPE_DVBCSA_SET_KEY,
                         UPIPE_DVBCSA_COMMON_SIGNATURE, key);
}

/** @This sets the maximum latency of the pipe.
 *
 * @param upipe description structure of the pipe
 * @param latency maximum latency
 * @return an error code
 */
static inline int upipe_dvbcsa_set_max_latency(struct upipe *upipe,
                                               uint64_t latency)
{
    return upipe_control(upipe, UPIPE_DVBCSA_SET_MAX_LATENCY,
                         UPIPE_DVBCSA_COMMON_SIGNATURE, latency);
}

/** @This adds a pid to the encryption/decryption list.
 *
 * @param upipe description structure of the pipe
 * @param pid pid to add
 * @return an error code
 */
static inline int upipe_dvbcsa_add_pid(struct upipe *upipe,
                                       uint64_t pid)
{
    return upipe_control(upipe, UPIPE_DVBCSA_ADD_PID,
                         UPIPE_DVBCSA_COMMON_SIGNATURE, pid);
}

/** @This deletes a pid from the encryption/decryption list.
 *
 * @param upipe description structure of the pipe
 * @param pid pid to delete
 * @return an error code
 */
static inline int upipe_dvbcsa_del_pid(struct upipe *upipe,
                                       uint64_t pid)
{
    return upipe_control(upipe, UPIPE_DVBCSA_DEL_PID,
                         UPIPE_DVBCSA_COMMON_SIGNATURE, pid);
}

/** @This stores a parsed dvbcsa control word. */
struct ustring_dvbcsa_cw {
    /** matching part of the string */
    struct ustring str;
    /** value of the parsed control word */
    dvbcsa_cw_t value;
};

/** @This parse a 64 bits dvbcsa control word from an ustring.
 *
 * @param str string to parse from
 * @return a parsed control word
 */
static inline struct ustring_dvbcsa_cw
ustring_to_dvbcsa_cw64(const struct ustring str)
{
    struct ustring tmp = str;
    struct ustring_dvbcsa_cw ret;
    ret.str = ustring_null();
    for (uint8_t i = 0; i < 8; i++) {
        struct ustring_byte b = ustring_to_byte(tmp);
        if (b.str.len != 2)
            return ret;

        if (i == 3 || i == 7) {
            ret.value[i] =
                ret.value[i - 3] + ret.value[i - 2] + ret.value[i - 1];
            if (b.value != ret.value[i])
                return ret;
        }
        else
            ret.value[i] = b.value;
        tmp = ustring_shift(tmp, 2);
    }
    ret.str = ustring_truncate(str, 16);
    return ret;
}

/** @This parsed a 48 bits dvbcsa control word from an ustring.
 *
 * @param str string to parse from
 * @return a parsed control word
 */
static inline struct ustring_dvbcsa_cw
ustring_to_dvbcsa_cw48(const struct ustring str)
{
    struct ustring tmp = str;
    struct ustring_dvbcsa_cw ret;
    ret.str = ustring_null();
    for (uint8_t i = 0; i < 8; i++) {
        if (i == 3 || i == 7) {
            ret.value[i] =
                ret.value[i - 3] + ret.value[i - 2] + ret.value[i - 1];
        }
        else {
            struct ustring_byte b = ustring_to_byte(tmp);
            if (b.str.len != 2)
                return ret;
            tmp = ustring_shift(tmp, 2);
            ret.value[i] = b.value;
        }
    }
    ret.str = ustring_truncate(str, 6 * 2);
    return ret;
}

/** @This parsed a dvbcsa control word from an ustring.
 *
 * @param str string to parse from
 * @return a parsed control word
 */
static inline struct ustring_dvbcsa_cw
ustring_to_dvbcsa_cw(const struct ustring str)
{
    if (str.len >= 16)
        return ustring_to_dvbcsa_cw64(str);
    return ustring_to_dvbcsa_cw48(str);
}

#ifdef __cplusplus
}
#endif
#endif
