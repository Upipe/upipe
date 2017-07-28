/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include <upipe-modules/upipe_aes_decrypt.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe.h>
#include <upipe-modules/uref_aes_flow.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_block.h>
#include <upipe/urefcount.h>

#define EXPECTED_FLOW_DEF       "block.aes."

/** @internal @This is the private context of an aes pipe. */
struct upipe_aes_decrypt {
    /** pipe public structure */
    struct upipe upipe;
    /** refcounting structure */
    struct urefcount urefcount;
    /** reference to the output pipe */
    struct upipe *output;
    /** reference to the output flow format */
    struct uref *flow_def;
    /** reference to the input flow format */
    struct uref *input_flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain requests;
    /** next uref */
    struct uref *next_uref;
    /** next uref size */
    size_t next_uref_size;
    /** list of uref */
    struct uchain urefs;
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;
    /** ubuf flow format */
    struct uref *flow_format;
    /** list of holded urefs */
    struct uchain input_urefs;
    /** number of holded urefs */
    unsigned input_nb_urefs;
    /** maximum number of holded urefs before blocking */
    unsigned input_max_urefs;
    /** blockers */
    struct uchain blockers;

    /** reset aes state */
    bool restart;
    /** store round keys */
    uint8_t round_keys[11][4][4];
    /** store initialization vector */
    uint8_t iv[16];
};

static int upipe_aes_decrypt_check(struct upipe *upipe, struct uref *uref);
static bool upipe_aes_decrypt_handle(struct upipe *upipe,
                                     struct uref *uref,
                                     struct upump **upump_p);

UPIPE_HELPER_UPIPE(upipe_aes_decrypt, upipe, UPIPE_AES_DECRYPT_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_aes_decrypt, urefcount, upipe_aes_decrypt_no_ref);
UPIPE_HELPER_VOID(upipe_aes_decrypt);
UPIPE_HELPER_OUTPUT(upipe_aes_decrypt, output, flow_def, output_state,
                    requests);
UPIPE_HELPER_UBUF_MGR(upipe_aes_decrypt, ubuf_mgr, flow_format,
                      ubuf_mgr_request,
                      upipe_aes_decrypt_check,
                      upipe_aes_decrypt_register_output_request,
                      upipe_aes_decrypt_unregister_output_request);
UPIPE_HELPER_INPUT(upipe_aes_decrypt, input_urefs, input_nb_urefs,
                   input_max_urefs, blockers, upipe_aes_decrypt_handle);
UPIPE_HELPER_UREF_STREAM(upipe_aes_decrypt, next_uref, next_uref_size, urefs,
                         NULL);

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t rsbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
    0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
    0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
    0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
    0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
    0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
    0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
    0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
    0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
    0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
    0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
    0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
    0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
    0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
    0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
    0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

static const uint8_t rcon[255] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40,
    0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a,
    0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a,
    0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39,
    0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25,
    0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a,
    0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04, 0x08,
    0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8,
    0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6,
    0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef,
    0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61,
    0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc,
    0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b,
    0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e,
    0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3,
    0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4,
    0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94,
    0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8,
    0xcb, 0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20,
    0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d,
    0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35,
    0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91,
    0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f,
    0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d,
    0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04,
    0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c,
    0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63,
    0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa,
    0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd,
    0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33, 0x66,
    0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb
};

/** @internal @This generates the round keys.
 *
 * @param key the AES key
 * @param round_keys the generated round keys
 */
static void aes_key_expansion(const uint8_t key[16],
                              uint8_t round_keys[11][4][4])
{
    memcpy(round_keys[0], key, sizeof (round_keys[0]));

    for (unsigned i = 1; i < 11; i++) {
        for (unsigned j = 0; j < 4; j++) {
            uint8_t tmp[4];

            if (!j) {
                /* rotation + substitution */
                tmp[0] = sbox[round_keys[i - 1][3][1]] ^ rcon[i];
                tmp[1] = sbox[round_keys[i - 1][3][2]];
                tmp[2] = sbox[round_keys[i - 1][3][3]];
                tmp[3] = sbox[round_keys[i - 1][3][0]];
            }
            else
                memcpy(tmp, round_keys[i][j - 1], sizeof (tmp));

            round_keys[i][j][0] = round_keys[i - 1][j][0] ^ tmp[0];
            round_keys[i][j][1] = round_keys[i - 1][j][1] ^ tmp[1];
            round_keys[i][j][2] = round_keys[i - 1][j][2] ^ tmp[2];
            round_keys[i][j][3] = round_keys[i - 1][j][3] ^ tmp[3];
        }
    }
}

/** @internal @This add a round key.
 *
 * @param round_keys the generated round keys
 * @param round the round number
 * @param state a block
 */
static inline void aes_add_round_key(uint8_t round_keys[11][4][4],
                                     uint8_t round,
                                     uint8_t state[4][4])
{
    assert(round < 11);
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 4; j++)
            state[i][j] ^= round_keys[round][i][j];
}

/** @internal @This reverses the AES shift rows stage.
 *
 * param state a block
 */
static void aes_inv_shift_rows(uint8_t state[4][4])
{
    uint8_t tmp;

    // Rotate first row 1 columns to right  
    tmp = state[3][1];
    state[3][1] = state[2][1];
    state[2][1] = state[1][1];
    state[1][1] = state[0][1];
    state[0][1] = tmp;

    // Rotate second row 2 columns to right 
    tmp = state[0][2];
    state[0][2] = state[2][2];
    state[2][2] = tmp;

    tmp = state[1][2];
    state[1][2] = state[3][2];
    state[3][2] = tmp;

    // Rotate third row 3 columns to right
    tmp = state[0][3];
    state[0][3] = state[1][3];
    state[1][3] = state[2][3];
    state[2][3] = state[3][3];
    state[3][3] = tmp;
}

/** @internal @This reverses the AES sub bytes stage.
 *
 * @param state a block
 */
static inline void aes_inv_sub_bytes(uint8_t state[4][4])
{
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 4; j++)
            state[j][i] = rsbox[state[j][i]];
}

static inline uint8_t aes_xtime(uint8_t x)
{
    return ((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

/** @internal @This implements multiply in GF(2^8).
 */
static inline uint8_t aes_multiply(uint8_t x, uint8_t y)
{
    assert((y >> 4) == 0);
    return (((y >> 0 & 1) * x) ^
            ((y >> 1 & 1) * aes_xtime(x)) ^
            ((y >> 2 & 1) * aes_xtime(aes_xtime(x))) ^
            ((y >> 3 & 1) * aes_xtime(aes_xtime(aes_xtime(x)))) ^
            ((y >> 4 & 1) * aes_xtime(aes_xtime(aes_xtime(aes_xtime(x))))));
}

/** @internal @This reverses the AES mix columns state.
 *
 * @param state a block
 */
static void aes_inv_mix_columns(uint8_t state[4][4])
{
    static const uint8_t matrix[4][4] = {
        { 0x0e, 0x0b, 0x0d, 0x09 },
        { 0x09, 0x0e, 0x0b, 0x0d },
        { 0x0d, 0x09, 0x0e, 0x0b },
        { 0x0b, 0x0d, 0x09, 0x0e },
    };

    uint8_t tmp[4][4];
    memcpy(tmp, state, sizeof (tmp));
    for(unsigned i = 0; i < 4; ++i)
        for (unsigned j = 0; j < 4; j++)
            state[i][j] =
                aes_multiply(tmp[i][0], matrix[j][0]) ^
                aes_multiply(tmp[i][1], matrix[j][1]) ^
                aes_multiply(tmp[i][2], matrix[j][2]) ^
                aes_multiply(tmp[i][3], matrix[j][3]);
}

/** @internal @This reverses the AES crypto.
 *
 * @param state a block
 * @param round_keys the generated round keys
 */
static void aes_inv_cipher(uint8_t state[4][4],
                           uint8_t round_keys[11][4][4])
{
    uint8_t round = 10;

    aes_add_round_key(round_keys, round, state);
    for (round = round - 1; round > 0; round--) {
        aes_inv_shift_rows(state);
        aes_inv_sub_bytes(state);
        aes_add_round_key(round_keys, round, state);
        aes_inv_mix_columns(state);
    }
    aes_inv_shift_rows(state);
    aes_inv_sub_bytes(state);
    aes_add_round_key(round_keys, round, state);
}

static inline void aes_xor_iv(uint8_t state[4][4],
                              const uint8_t iv[16])
{
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 4; j++)
            state[i][j] ^= iv[i * 4 + j];
}

/** @internal @This decrypts an AES block.
 *
 * @param buffer the block to decrypt inplace
 * @param round_keys the generated round keys
 * @param iv the initialization vector
 */
static inline void aes_cbc_decrypt(uint8_t buffer[16],
                                   uint8_t round_keys[11][4][4],
                                   const uint8_t iv[16])
{
    aes_inv_cipher((uint8_t (*)[])buffer, round_keys);
    aes_xor_iv((uint8_t (*)[])buffer, iv);
}

/** @internal @This allocates an aes decryption pipe.
 *
 * @param mgr reference to the aes decryption pipe manager.
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args arguments
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static struct upipe *upipe_aes_decrypt_alloc(struct upipe_mgr *mgr,
                                             struct uprobe *uprobe,
                                             uint32_t signature,
                                             va_list args)
{
    struct upipe *upipe =
        upipe_aes_decrypt_alloc_void(mgr, uprobe, signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_aes_decrypt *upipe_aes_decrypt =
        upipe_aes_decrypt_from_upipe(upipe);

    upipe_aes_decrypt_init_urefcount(upipe);
    upipe_aes_decrypt_init_output(upipe);
    upipe_aes_decrypt_init_ubuf_mgr(upipe);
    upipe_aes_decrypt_init_input(upipe);
    upipe_aes_decrypt_init_uref_stream(upipe);
    upipe_aes_decrypt->input_flow_def = NULL;
    upipe_aes_decrypt->restart = true;

    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This frees an aes decryption pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_aes_decrypt_no_ref(struct upipe *upipe)
{
    struct upipe_aes_decrypt *upipe_aes_decrypt =
        upipe_aes_decrypt_from_upipe(upipe);

    upipe_throw_dead(upipe);
    uref_free(upipe_aes_decrypt->input_flow_def);
    upipe_aes_decrypt_clean_uref_stream(upipe);
    upipe_aes_decrypt_clean_input(upipe);
    upipe_aes_decrypt_clean_ubuf_mgr(upipe);
    upipe_aes_decrypt_clean_output(upipe);
    upipe_aes_decrypt_clean_urefcount(upipe);
    upipe_aes_decrypt_free_void(upipe);
}

/** @internal @This restarts decryption algorithm.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static int upipe_aes_decrypt_restart(struct upipe *upipe)
{
    struct upipe_aes_decrypt *upipe_aes_decrypt =
        upipe_aes_decrypt_from_upipe(upipe);
    struct uref *input_flow_def = upipe_aes_decrypt->input_flow_def;

    const uint8_t *key;
    size_t key_size;
    int ret = uref_aes_get_key(input_flow_def, &key, &key_size);
    if (unlikely(!ubase_check(ret))) {
        upipe_warn(upipe, "no aes key");
        return ret;
    }
    if (unlikely(key_size != 16)) {
        upipe_warn(upipe, "invalid aes key");
        return ret;
    }

    const uint8_t *iv;
    size_t iv_size;
    ret = uref_aes_get_iv(input_flow_def, &iv, &iv_size);
    if (unlikely(!ubase_check(ret))) {
        upipe_warn(upipe, "no aes initialization vector");
        return ret;
    }
    if (unlikely(iv_size != 16)) {
        upipe_warn(upipe, "invalid aes initialization vector");
        return ret;
    }

    aes_key_expansion(key, upipe_aes_decrypt->round_keys);
    return UBASE_ERR_NONE;
}

/** @internal @This outputs the decrypted blocks.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_aes_decrypt_worker(struct upipe *upipe,
                                     struct upump **upump_p)
{
    struct upipe_aes_decrypt *upipe_aes_decrypt =
        upipe_aes_decrypt_from_upipe(upipe);

    if (upipe_aes_decrypt->restart) {
        if (unlikely(!ubase_check(upipe_aes_decrypt_restart(upipe)))) {
            upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
            return;
        }
        upipe_aes_decrypt->restart = false;
    }

    size_t block_size;
    ubase_assert(uref_block_size(upipe_aes_decrypt->next_uref, &block_size));
    for (size_t size = block_size; size >= 16; size -= 16) {
        int ret;

        struct uref *uref = upipe_aes_decrypt_extract_uref_stream(upipe, 16);
        if (unlikely(!uref)) {
            upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
            return;
        }

        uint8_t iv[16];
        ret = uref_block_extract(uref, 0, 16, iv);
        if (unlikely(!ubase_check(ret))) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
            return;
        }

        struct ubuf *ubuf =
            ubuf_block_alloc(upipe_aes_decrypt->ubuf_mgr, 16);
        if (unlikely(!ubuf)) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
        uref_attach_ubuf(uref, ubuf);

        int wsize = 16;
        uint8_t *wbuf;
        ret = uref_block_write(uref, 0, &wsize, &wbuf);
        if (unlikely(!ubase_check(ret)) || wsize != 16) {
            uref_free(uref);
            upipe_throw_fatal(upipe, UBASE_ERR_INVALID);
            return;
        }
        memcpy(wbuf, iv, wsize);
        aes_cbc_decrypt(wbuf,
                        upipe_aes_decrypt->round_keys,
                        upipe_aes_decrypt->iv);
        ubase_assert(uref_block_unmap(uref, 0));
        memcpy(upipe_aes_decrypt->iv, iv, sizeof (upipe_aes_decrypt->iv));
        upipe_aes_decrypt_output(upipe, uref, upump_p);
    }
}

/** @internal @This outputs the last block.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_aes_decrypt_flush(struct upipe *upipe)
{
    struct upipe_aes_decrypt *upipe_aes_decrypt =
        upipe_aes_decrypt_from_upipe(upipe);

    upipe_aes_decrypt_clean_uref_stream(upipe);
    upipe_aes_decrypt_init_uref_stream(upipe);
    upipe_aes_decrypt->restart = true;
}

static bool upipe_aes_decrypt_handle(struct upipe *upipe,
                                     struct uref *uref,
                                     struct upump **upump_p)
{
    struct upipe_aes_decrypt *upipe_aes_decrypt =
        upipe_aes_decrypt_from_upipe(upipe);

    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_aes_decrypt_flush(upipe);
        upipe_aes_decrypt_store_flow_def(upipe, NULL);
        upipe_aes_decrypt_require_ubuf_mgr(upipe, uref);
        return true;
    }

    if (upipe_aes_decrypt->flow_def == NULL)
        return false;

    upipe_aes_decrypt_append_uref_stream(upipe, uref);
    upipe_aes_decrypt_worker(upipe, upump_p);
    return true;
}

/** @internal @This is called when there is new data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to the pump that generated the buffer
 */
static void upipe_aes_decrypt_input(struct upipe *upipe,
                                    struct uref *uref,
                                    struct upump **upump_p)
{
    if (!upipe_aes_decrypt_check_input(upipe)) {
        upipe_aes_decrypt_hold_input(upipe, uref);
    }
    else if (!upipe_aes_decrypt_handle(upipe, uref, upump_p)) {
        upipe_aes_decrypt_hold_input(upipe, uref);
        upipe_aes_decrypt_block_input(upipe, upump_p);
        /* Increment upipe refcount to avoid disappearing before all packets
         * have been sent. */
        upipe_use(upipe);
    }
}

/** @internal @This checks if uref and ubuf manager need to be required.
 *
 * @param upipe description structure of the pipe
 * @param flow_format requested flow format
 * @return an error code
 */
static int upipe_aes_decrypt_check(struct upipe *upipe,
                                   struct uref *flow_format)
{
    struct upipe_aes_decrypt *upipe_aes_decrypt =
        upipe_aes_decrypt_from_upipe(upipe);

    if (flow_format != NULL)
        upipe_aes_decrypt_store_flow_def(upipe, flow_format);

    if (upipe_aes_decrypt->flow_def == NULL)
        return UBASE_ERR_NONE;

    bool was_buffered = !upipe_aes_decrypt_check_input(upipe);
    upipe_aes_decrypt_output_input(upipe);
    upipe_aes_decrypt_unblock_input(upipe);
    if (was_buffered && upipe_aes_decrypt_check_input(upipe)) {
        /* All packets have been output, release again the pipe that has been
         * used in @ref upipe_aes_decrypt_input. */
        upipe_release(upipe);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This stores the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the input flow format to store
 */
static void upipe_aes_decrypt_store_input_flow_def(struct upipe *upipe,
                                                   struct uref *flow_def)
{
    struct upipe_aes_decrypt *upipe_aes_decrypt =
        upipe_aes_decrypt_from_upipe(upipe);

    if (likely(upipe_aes_decrypt->input_flow_def != NULL))
        uref_free(upipe_aes_decrypt->input_flow_def);
    upipe_aes_decrypt->input_flow_def = flow_def;
}

/** @internal @This sets the output flow format.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the flow format to set
 * @return an error code
 */
static int upipe_aes_decrypt_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));
    UBASE_RETURN(uref_aes_match_method(flow_def, "AES-128"));

    struct uref *flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    upipe_aes_decrypt_store_input_flow_def(upipe, flow_def_dup);

    flow_def_dup = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def_dup);
    int ret = uref_flow_set_def(flow_def_dup, "block.");
    if (unlikely(!ubase_check(ret))) {
        uref_free(flow_def_dup);
        return ret;
    }
    uref_aes_delete(flow_def_dup);
    upipe_input(upipe, flow_def_dup, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This dispatches commands.
 *
 * @param upipe description structure of the pipe
 * @param command command to dispatch
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_aes_decrypt_control(struct upipe *upipe,
                                     int command,
                                     va_list args)
{
    switch (command) {
    case UPIPE_REGISTER_REQUEST: {
        struct urequest *urequest = va_arg(args, struct urequest *);
        if (urequest->type == UREQUEST_UBUF_MGR ||
            urequest->type == UREQUEST_FLOW_FORMAT)
            return upipe_throw_provide_request(upipe, urequest);
        return upipe_aes_decrypt_alloc_output_proxy(upipe, urequest);
    }
    case UPIPE_UNREGISTER_REQUEST: {
        struct urequest *urequest = va_arg(args, struct urequest *);
        if (urequest->type == UREQUEST_UBUF_MGR ||
            urequest->type == UREQUEST_FLOW_FORMAT)
            return UBASE_ERR_NONE;
        return upipe_aes_decrypt_free_output_proxy(upipe, urequest);
    }
    case UPIPE_GET_OUTPUT:
    case UPIPE_SET_OUTPUT:
    case UPIPE_GET_FLOW_DEF:
        return upipe_aes_decrypt_control(upipe, command, args);
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_aes_decrypt_set_flow_def(upipe, flow_def);
    }
    }
    return UBASE_ERR_UNHANDLED;
}

/** @internal @This is the static aes decryption pipe manager. */
static struct upipe_mgr upipe_aes_decrypt_mgr = {
    .signature = UPIPE_AES_DECRYPT_SIGNATURE,
    .refcount = NULL,
    .upipe_alloc = upipe_aes_decrypt_alloc,
    .upipe_input = upipe_aes_decrypt_input,
    .upipe_control = upipe_aes_decrypt_control,
};

/** @This returns the static aes decryption pipe manager.
 *
 * @return a reference to the static aes decryption pipe manager
 */
struct upipe_mgr *upipe_aes_decrypt_mgr_alloc(void)
{
    return &upipe_aes_decrypt_mgr;
}
