/*
 * Copyright (C) 2018 Open Broadcast Systems Ltd
 *
 * Authors: Rafaël Carré
 *
 * This ts is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This ts is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this ts; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the entitlement management message table
 * Normative references:
 *   EBU TECH 3292-s1
 */

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uclock.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_block.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_subpipe.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe-ts/upipe_ts_emm_decoder.h>
#include <upipe-ts/uref_ts_flow.h>
#include "upipe_ts_psi_decoder.h"

#include <bitstream/mpeg/psi.h>
#include <bitstream/mpeg/psi/desc_09.h>
#include <bitstream/dvb/si.h>
#include <bitstream/ebu/biss.h>

#include <gcrypt.h>
#include <libtasn1.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rsa_asn1.h"

/** we only accept TS packets */
#define EXPECTED_EMM_FLOW_DEF "block.mpegtspsi.mpegtsemm."

/** @hidden */
static int upipe_ts_emmd_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_emmd pipe. */
struct upipe_ts_emmd {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** input flow definition */
    struct uref *flow_def_input;
    /** attributes in the sequence header */
    struct uref *flow_def_attr;

    uint8_t ekid[8];
    gcry_sexp_t key;
    asn1_node asn;
    gcry_cipher_hd_t aes;
    uint8_t aes_key[2][16];

    /** currently in effect EMM table */
    UPIPE_TS_PSID_TABLE_DECLARE(emm);
    /** EMM table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_emm);

    /** list of input subpipes */
    struct uchain subs;
    /** manager to create input subpipes */
    struct upipe_mgr sub_mgr;

    /** public upipe structure */
    struct upipe upipe;
};

/** we only accept TS packets */
#define EXPECTED_ECM_FLOW_DEF "block.mpegtspsi.mpegtsecm."

/** @hidden */
static int upipe_ts_emmd_ecm_check(struct upipe *upipe, struct uref *flow_format);

/** @internal @This is the private context of a ts_emmd_ecm pipe. */
struct upipe_ts_emmd_ecm {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** flow format packet */
    struct uref *flow_format;
    /** ubuf manager request */
    struct urequest ubuf_mgr_request;

    /** pipe acting as output */
    struct upipe *output;
    /** output flow definition */
    struct uref *flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;
    /** input flow definition */
    struct uref *flow_def_input;
    /** attributes in the sequence header */
    struct uref *flow_def_attr;

    gcry_sexp_t key;

    /** currently in effect ECM table */
    UPIPE_TS_PSID_TABLE_DECLARE(ecm);
    /** ECM table being gathered */
    UPIPE_TS_PSID_TABLE_DECLARE(next_ecm);

    /** structure for double-linked lists */
    struct uchain uchain;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_ts_emmd_ecm, upipe, UPIPE_TS_EMMD_ECM_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_emmd_ecm, urefcount, upipe_ts_emmd_ecm_free)
UPIPE_HELPER_VOID(upipe_ts_emmd_ecm)
UPIPE_HELPER_OUTPUT(upipe_ts_emmd_ecm, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_emmd_ecm, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_emmd_ecm_check,
                      upipe_ts_emmd_ecm_register_output_request,
                      upipe_ts_emmd_ecm_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_emmd_ecm, flow_def_input, flow_def_attr)

UPIPE_HELPER_UPIPE(upipe_ts_emmd, upipe, UPIPE_TS_EMMD_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_ts_emmd, urefcount, upipe_ts_emmd_free)
UPIPE_HELPER_VOID(upipe_ts_emmd)
UPIPE_HELPER_OUTPUT(upipe_ts_emmd, output, flow_def, output_state, request_list)
UPIPE_HELPER_UBUF_MGR(upipe_ts_emmd, ubuf_mgr, flow_format, ubuf_mgr_request,
                      upipe_ts_emmd_check,
                      upipe_ts_emmd_register_output_request,
                      upipe_ts_emmd_unregister_output_request)
UPIPE_HELPER_FLOW_DEF(upipe_ts_emmd, flow_def_input, flow_def_attr)

UPIPE_HELPER_SUBPIPE(upipe_ts_emmd, upipe_ts_emmd_ecm, ecm, sub_mgr, subs, uchain)

static int read_rsa_file(struct upipe *upipe, const char *file);
static void upipe_ts_emmd_init_sub_mgr(struct upipe *upipe);

/** @internal @This alloemmes a ts_emmd pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe alloemmor
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of alloemmion error
 */
static struct upipe *upipe_ts_emmd_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_emmd_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {
        uprobe_err(uprobe, upipe, "Application did not initialize libgcrypt, see "
        "https://www.gnupg.org/documentation/manuals/gcrypt/Initializing-the-library.html");
        upipe_ts_emmd_free_void(upipe);
        return NULL;
    }

    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);

    gcry_error_t err = gcry_cipher_open(&upipe_ts_emmd->aes, GCRY_CIPHER_AES,
            GCRY_CIPHER_MODE_CBC, 0);
    if (err) {
        upipe_err_va(upipe, "AES cipher failed (0x%x)", err);
        upipe_ts_emmd_free_void(upipe);
        return NULL;
    }

    /* Load ASN.1 syntax */
    int ret = asn1_array2tree(rsa_asn1_tab, &upipe_ts_emmd->asn, NULL);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "Loading RSA ASN.1 failed: %s", asn1_strerror(ret));
        gcry_cipher_close(upipe_ts_emmd->aes);
        upipe_ts_emmd_free_void(upipe);
        return NULL;
    }

    upipe_ts_emmd_init_sub_ecms(upipe);
    upipe_ts_emmd_init_urefcount(upipe);
    upipe_ts_emmd_init_output(upipe);
    upipe_ts_emmd_init_ubuf_mgr(upipe);
    upipe_ts_emmd_init_flow_def(upipe);
    upipe_ts_emmd_init_sub_mgr(upipe);
    upipe_ts_psid_table_init(upipe_ts_emmd->emm);
    upipe_ts_psid_table_init(upipe_ts_emmd->next_emm);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This validates the next EMM.
 *
 * @param upipe description structure of the pipe
 * @return false if the EMM is invalid
 */
static bool upipe_ts_emmd_table_validate(struct upipe *upipe)
{
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_emmd->next_emm, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            return false;

        if (!bissca_emm_validate(section) || !psi_check_crc(section)) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This is a helper function to parse descriptors and import
 * the relevant ones into flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param descl pointer to descriptor list
 * @param desclength length of the descriptor list
 * @return an error code
 */
static void upipe_ts_emmd_parse_descs(struct upipe *upipe,
                                      struct uref *flow_def,
                                      const uint8_t *descl, uint16_t desclength)
{
    const uint8_t *desc;
    int j = 0;
    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL) {
        bool copy = false;
        switch (desc_get_tag(desc)) {
            default:
                copy = true;
                break;
        }

        if (copy) {
            UBASE_FATAL(upipe, uref_ts_flow_add_emm_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE))
        }
    }
}

/** @internal @This is a helper function to parse session data descriptors and
 * import the relevant ones into flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param descl pointer to descriptor list
 * @param desclength length of the descriptor list
 * @return an error code
 */
static void upipe_ts_emmd_parse_sd_descs(struct upipe *upipe,
                                      struct uref *flow_def,
                                      const uint8_t *descl, uint16_t desclength)
{
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);
    const uint8_t *desc;
    int j = 0;

    bool prevent_descrambled_forward = false;
    bool prevent_decoded_forward = false;
    bool insert_watermark = false;

    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL) {
        bool copy = false;
        bool valid = true;
        uint16_t length = desc_get_length(desc);
        switch (desc_get_tag(desc)) {
            case 0x81:
                valid = length == 17;
                if (!valid)
                    break;
                uint8_t type = desc[DESC_HEADER_SIZE];
                bool odd = type & 1;
                type >>= 1;
                valid = type == 0; // AES-128-CBC
                if (!valid)
                    break;
                memcpy(upipe_ts_emmd->aes_key[odd], &desc[DESC_HEADER_SIZE+1],
                        16);
                break;
            case 0x82:
                valid = length == 1;
                if (!valid)
                    break;
                uint8_t flags = desc[DESC_HEADER_SIZE];
                prevent_descrambled_forward = flags & (1 << 7);
                prevent_decoded_forward = flags & (1 << 6);
                insert_watermark = flags & (1 << 5);
                break;
            default:
                copy = true;
                break;
        }

        if (!valid)
            upipe_warn_va(upipe, "invalid session data descriptor 0x%x",
                    desc_get_tag(desc));

        if (copy) {
            UBASE_FATAL(upipe, uref_ts_flow_add_emm_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE))
        }
    }

    if (prevent_descrambled_forward)
        uref_ts_flow_set_prevent_descrambled_forward(flow_def);
    else
        uref_ts_flow_delete_prevent_descrambled_forward(flow_def);
    if (prevent_decoded_forward)
        uref_ts_flow_set_prevent_decoded_forward(flow_def);
    else
        uref_ts_flow_delete_prevent_decoded_forward(flow_def);
    if (insert_watermark)
        uref_ts_flow_set_insert_watermark(flow_def);
    else
        uref_ts_flow_delete_insert_watermark(flow_def);
}

static int base64decode(uint8_t *in, size_t inLen, uint8_t *out, size_t *outLen)
{
#define WHITESPACE 64
#define EQUALS     65
#define INVALID    66

    static const uint8_t d[] = {
    66,66,66,66,66,66,66,66,66,66,64,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,62,66,66,66,63,52,53,
    54,55,56,57,58,59,60,61,66,66,66,65,66,66,66, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,66,66,66,66,66,66,26,27,28,
    29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,66,
    66,66,66,66,66,66
    };

    uint8_t *end = in + inLen;
    uint8_t iter = 0;
    uint32_t buf = 0;
    size_t len = 0;

    while (in < end) {
        uint8_t c = d[*in++];

        switch (c) {
        case WHITESPACE:
            continue;   /* skip whitespace */
        case INVALID:
            return 1;   /* invalid input, return error */
        case EQUALS:    /* pad character, end of data */
            in = end;
            continue;
        default:
            buf = buf << 6 | c;
            iter++; // increment the number of iteration
            /* If the buffer is full, split it into bytes */
            if (iter == 4) {
                if ((len += 3) > *outLen)
                    return 1; /* buffer overflow */
                *(out++) = (buf >> 16) & 255;
                *(out++) = (buf >> 8) & 255;
                *(out++) = buf & 255;
                buf = 0; iter = 0;

            }
        }
    }

    if (iter == 3) {
        if ((len += 2) > *outLen)
            return 1; /* buffer overflow */
        *(out++) = (buf >> 10) & 255;
        *(out++) = (buf >> 2) & 255;
    } else if (iter == 2) {
        if (++len > *outLen)
            return 1; /* buffer overflow */
        *(out++) = (buf >> 4) & 255;
    }

    *outLen = len; /* modify to reflect the actual output size */
    return 0;
}

static uint8_t *read_file(struct upipe *upipe, const char *file, size_t *n)
{
    FILE *f = fopen(file, "rb");
    if (!f) {
        upipe_err_va(upipe, "Could not open %s", file);
        return NULL;
    }

    struct stat st;
    if (fstat(fileno(f), &st) < 0) {
        upipe_err_va(upipe, "Could not stat %s", file);
        fclose(f);
        return NULL;
    }

    *n = st.st_size;

    uint8_t *buf = malloc(*n);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    if (fread(buf, *n, 1, f) != 1) {
        upipe_err_va(upipe, "Could not read %s", file);
        fclose(f);
        goto bad;
    }

    fclose(f);

    static const char *begin = "-----BEGIN PRIVATE KEY-----";
    static const char *end = "-----END PRIVATE KEY-----";

    if (strncmp((char*)buf, begin, strlen(begin))) {
        upipe_err(upipe, "Missing private key header");
        goto bad;
    }

    uint8_t *inBuf = &buf[strlen(begin)];
    char *e = strstr((char*)inBuf, end);
    if (!e) {
        upipe_err(upipe, "Missing private key footer");
        goto bad;
    }

    size_t inLen = e - (char*)inBuf;

    if (base64decode(inBuf, inLen, buf, n)) {
        upipe_err(upipe, "Could not base64 decode private key");
        goto bad;
    }

    return buf;

bad:
    free(buf);
    return NULL;
}

static int read_int(struct upipe *upipe, asn1_node node, const char *name, char *buf, int *s)
{
    int ret = asn1_read_value(node, name, buf, s);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "%s : %s", name, asn1_strerror(ret));
    }

    return ret;
}

static int read_private_key(struct upipe *upipe, void *der, int der_len, char *pk, int *pk_len)
{
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);
    asn1_node node = NULL;

    int ret = asn1_create_element(upipe_ts_emmd->asn, "RSA.PrivateKeyInfo", &node);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "create_element: %s", asn1_strerror(ret));
        goto end;
    }

    ret = asn1_der_decoding(&node, der, der_len, NULL);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "der_decoding: %s", asn1_strerror(ret));
        goto end;
    }

    char v[1];
    int l = sizeof(v);
    ret = read_int(upipe, node, "version", v, &l);
    if (ret != ASN1_SUCCESS) {
        goto end;
    }

    if (l != 1 || v[0] != 0) {
        upipe_err_va(upipe, "Unknown private key version %u\n", v[0]);
        goto end;
    }

    char algo[32];
    l = sizeof(algo);
    ret = read_int(upipe, node, "algorithm.algorithm", algo, &l);
    if (ret != ASN1_SUCCESS || strcmp(algo, "1.2.840.113549.1.1.1")) {
        upipe_err_va(upipe, "Not a RSA private key");
        goto end;
    }

    ret = asn1_read_value(node, "PrivateKey", pk, pk_len);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "Could not read RSA private key: %s",
                asn1_strerror(ret));
    }

end:
    asn1_delete_structure(&node);
    return ret;
}

/* hash the public key to get the 64 bit ID */
static int hash(struct upipe *upipe, char *n, int n_len, char *e, int e_len, uint8_t *id)
{
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);

    uint8_t buf[1024];
    int buf_len = sizeof(buf);

    char bitstring[1024];
    int bitstring_len = sizeof(buf);


    asn1_node node = NULL;

    int ret = asn1_create_element(upipe_ts_emmd->asn, "RSA.RSAPublicKey", &node);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "pk create_element: %s", asn1_strerror(ret));
        goto end;
    }

    ret = asn1_write_value(node, "modulus", n, n_len);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "%s", asn1_strerror(ret));
        goto end;
    }

    ret = asn1_write_value(node, "publicExponent", e, e_len);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "%s", asn1_strerror(ret));
        goto end;
    }

    ret = asn1_der_coding(node, "", bitstring, &bitstring_len, NULL);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "pk der_coding: %s", asn1_strerror(ret));
        goto end;
    }

    asn1_delete_structure(&node);

    ret = asn1_create_element(upipe_ts_emmd->asn, "RSA.PublicKeyInfo", &node);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "pk create_element: %s", asn1_strerror(ret));
        goto end;
    }

    static const char *algo = "1.2.840.113549.1.1.1";
    ret = asn1_write_value(node, "algorithm.algorithm", algo, strlen(algo));
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "algo prob");
        goto end;
    }

    ret = asn1_write_value(node, "PublicKey", bitstring, bitstring_len * 8);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "pk info prob");
        goto end;
    }

    ret = asn1_der_coding(node, "", buf, &buf_len, NULL);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "pk der_coding: %s", asn1_strerror(ret));
        goto end;
    }

    asn1_delete_structure(&node);

    gcry_md_hd_t hd;
    if (gcry_md_open(&hd, GCRY_MD_SHA256, 0)) {
        upipe_err_va(upipe, "could not open sha256");
        return ASN1_GENERIC_ERROR;
    }

    gcry_md_write(hd, buf, buf_len);
    gcry_md_final(hd);
    uint8_t *h = (uint8_t*) gcry_md_read(hd, GCRY_MD_SHA256) ;
    memcpy(id, h, 8);
    gcry_md_close(hd);

    return 0;

end:
    asn1_delete_structure(&node);
    return ret;
}

static int read_rsa_private_key(struct upipe *upipe, void *pk, int pk_len)
{
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);
    asn1_node node = NULL;

    int ret = asn1_create_element(upipe_ts_emmd->asn, "RSA.RSAPrivateKey", &node);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "rsa create_element: %s", asn1_strerror(ret));
        goto end;
    }

    ret = asn1_der_decoding(&node, pk, pk_len, NULL);
    if (ret != ASN1_SUCCESS) {
        upipe_err_va(upipe, "rsa der_decoding: %s", asn1_strerror(ret));
        goto end;
    }

    char num[9][1024];
    char name[9][16] = {
        "version",
        "modulus",
        "publicExponent",
        "privateExponent",
        "prime1",
        "prime2",
        "exponent1",
        "exponent2",
        "coefficient",
    };
    int s[9];
    for (int i = 0; i < 9; i++) {
        s[i] = 1024;

        ret = read_int(upipe, node, name[i], num[i], &s[i]);
        if (ret != ASN1_SUCCESS)
            goto end;
    }

    if (s[0] != 1 || num[0][0] != 0) {
        upipe_err_va(upipe, "Unknown RSA private key version %u\n", num[0][0]);
        ret = ASN1_GENERIC_ERROR;
        goto end;
    }

    if (hash(upipe, num[1], s[1], num[2], s[2], upipe_ts_emmd->ekid)) {
        upipe_err_va(upipe, "FAIL");
        ret = ASN1_GENERIC_ERROR;
        goto end;
    }

    {
        gcry_mpi_t p, q;
        if (gcry_mpi_scan(&p, GCRYMPI_FMT_USG, num[4], s[4], NULL))
            upipe_err_va(upipe, "scan p");
        if (gcry_mpi_scan(&q, GCRYMPI_FMT_USG, num[5], s[5], NULL))
            upipe_err_va(upipe, "scan q");
        if (gcry_mpi_cmp (p, q) > 0) {
            upipe_dbg_va(upipe, "OpenSSL key");
            gcry_mpi_t u = gcry_mpi_new(1024);
            gcry_mpi_swap (p, q);
            gcry_mpi_invm (u, p, q);

            size_t q_len, p_len;
            gcry_mpi_print(GCRYMPI_FMT_USG, (unsigned char*)num[4], s[5], &p_len, p);
            gcry_mpi_print(GCRYMPI_FMT_USG, (unsigned char*)num[5], s[4], &q_len, q);
            s[4] = p_len;
            s[5] = q_len;

            size_t u_len;
            gcry_mpi_print(GCRYMPI_FMT_USG, (unsigned char*)num[8], s[8], &u_len, u);
            s[8] = u_len;

            gcry_mpi_release(u);
        }
        gcry_mpi_release(p);
        gcry_mpi_release(q);
    }

    if (gcry_sexp_build(&upipe_ts_emmd->key, NULL,
                "(private-key (rsa (n %b) (e %b) (d %b) (p %b) (q %b) (u %b)))",
                s[1], num[1], s[2], num[2], s[3], num[3],
                s[4], num[4], s[5], num[5], s[8], num[8])) {
        upipe_err_va(upipe, "sexp key fail");
        ret = ASN1_GENERIC_ERROR;
        upipe_ts_emmd->key = NULL;
        goto end;
    }

end:
    asn1_delete_structure(&node);
    return ret;
}

static int read_rsa_file(struct upipe *upipe, const char *file)
{
    /* Load file */
    size_t der_len;
    uint8_t *der = read_file(upipe, file, &der_len);
    if (!der) {
        upipe_err_va(upipe, "could not read %s", file);
        return 1;
    }

    /* The RSA parameters are embedded one level deep */
    char key [2048];
    int key_len = sizeof(key);

    /* Extract the RSA parameters from the key */
    int ret = read_private_key(upipe, der, der_len, key, &key_len);
    free(der); /* we're done with der-encoded file */

    if (ret != ASN1_SUCCESS)
        return 1;

    /* Parse the RSA parameters */
    ret = read_rsa_private_key(upipe, key, key_len);
    if (ret != ASN1_SUCCESS)
        return 1;

    return 0;
}

static int decrypt(struct upipe *upipe, uint8_t *esd, size_t n)
{
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);
    int ret = 1;

    gcry_sexp_t data;
    if (gcry_sexp_build(&data, NULL,
                "(enc-val(flags oaep)(hash-algo sha256)(rsa(a %b)))",
                n, esd)) {
        upipe_err_va(upipe, "Could not build data sexp");
        return ret;
    }

    gcry_sexp_t plain = NULL;
    gcry_error_t err = gcry_pk_decrypt(&plain, data, upipe_ts_emmd->key);
    gcry_sexp_release(data);
    if (err) {
        upipe_err_va(upipe, "decrypt failed (0x%x)", err);
        return ret;
    }

    gcry_sexp_t l = gcry_sexp_find_token(plain, "value", 0);
    size_t len;
    const char *skd = gcry_sexp_nth_data(l, 1, &len);
    if (len > n) {
        upipe_err_va(upipe, "decrypted data too big (%zu > %zu", len, n);
    } else {
        memcpy(esd, skd, len);
        ret = 0;
    }

    gcry_sexp_release(plain);

    return ret;
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_emmd_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);
    assert(upipe_ts_emmd->flow_def_input != NULL);

    if (!upipe_ts_psid_table_section(upipe_ts_emmd->next_emm, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_emmd->emm) &&
        upipe_ts_psid_table_compare(upipe_ts_emmd->emm,
                                    upipe_ts_emmd->next_emm)) {
        /* Identical EMM. */
        upipe_ts_psid_table_clean(upipe_ts_emmd->next_emm);
        upipe_ts_psid_table_init(upipe_ts_emmd->next_emm);
        return;
    }

    if (!ubase_check(upipe_ts_psid_table_merge(upipe_ts_emmd->next_emm,
                                               upipe_ts_emmd->ubuf_mgr)) ||
        !upipe_ts_emmd_table_validate(upipe)) {
        upipe_warn(upipe, "invalid EMM section received");
        upipe_ts_psid_table_clean(upipe_ts_emmd->next_emm);
        upipe_ts_psid_table_init(upipe_ts_emmd->next_emm);
        return;
    }

    struct uref *flow_def = upipe_ts_emmd_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
    upipe_ts_psid_table_foreach (upipe_ts_emmd->next_emm, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        /* uint8_t last_table_id = */ bissca_emm_get_last_table_id(section);
        uint8_t cipher = bissca_emm_get_emm_cipher_type(section);
        if (cipher != BISSCA_EMM_CIPHER_RSA_2048_OAEP)
            continue;

        upipe_ts_emmd_parse_descs(upipe, flow_def,
                bissca_emm_get_descl_const(section), bissca_emm_get_desclength(section));

        int j = 0;
        uint8_t *emm_n;
        while ((emm_n = bissca_emm_get_emmn((uint8_t *)section, j++)) != NULL) {
            uint8_t ekid[8];
            bissca_emmn_get_ekid(emm_n, ekid);
            if (memcmp(ekid, upipe_ts_emmd->ekid, 8)) {
                upipe_dbg_va(upipe, "We do not have the private key for "
                        "%02x%02x%02x%02x%02x%02x%02x%02x", ekid[0], ekid[1],
                        ekid[2], ekid[3], ekid[4], ekid[5], ekid[6], ekid[7]);
                continue;
            }

            uint8_t esd[256];
            bissca_emmn_get_esd(emm_n, esd);

            if (decrypt(upipe, esd, sizeof(esd))) {
                upipe_err_va(upipe, "decryption failed");
                continue;
            }

            upipe_ts_emmd_parse_sd_descs(upipe, flow_def,
                    &esd[DESCS_HEADER_SIZE], descs_get_length(esd));
        }

        uref_block_unmap(section_uref, 0);
    }

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_emmd->emm))
        upipe_ts_psid_table_clean(upipe_ts_emmd->emm);
    upipe_ts_psid_table_copy(upipe_ts_emmd->emm, upipe_ts_emmd->next_emm);
    upipe_ts_psid_table_init(upipe_ts_emmd->next_emm);

    flow_def = upipe_ts_emmd_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    upipe_ts_emmd_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    upipe_ts_emmd_output(upipe, NULL, upump_p);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_emmd_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        flow_format = upipe_ts_emmd_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL) {
            upipe_ts_emmd_store_flow_def(upipe, flow_format);
            /* Force sending flow def */
            upipe_ts_emmd_output(upipe, NULL, NULL);
        }
    }

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_emmd_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_EMM_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_emmd_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_emmd_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_emmd_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_ts_emmd_control_ecms(upipe, command, args));

    switch (command) {
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_emmd_set_flow_def(upipe, flow_def);
        }
        case UPIPE_TS_EMM_SET_PRIVATE_KEY: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_TS_EMMD_SIGNATURE);
            const char *private_key = va_arg(args, const char*);
            if (!private_key)
                return UBASE_ERR_INVALID;
            read_rsa_file(upipe, private_key);
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_emmd_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);

    gcry_cipher_close(upipe_ts_emmd->aes);

    asn1_delete_structure(&upipe_ts_emmd->asn);
    if (upipe_ts_emmd->key)
        gcry_sexp_release(upipe_ts_emmd->key);

    upipe_ts_psid_table_clean(upipe_ts_emmd->emm);
    upipe_ts_psid_table_clean(upipe_ts_emmd->next_emm);
    upipe_ts_emmd_clean_output(upipe);
    upipe_ts_emmd_clean_ubuf_mgr(upipe);
    upipe_ts_emmd_clean_flow_def(upipe);
    upipe_ts_emmd_clean_urefcount(upipe);
    upipe_ts_emmd_clean_sub_ecms(upipe);
    upipe_ts_emmd_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_ts_emmd_mgr = {
    .refcount = NULL,
    .signature = UPIPE_TS_EMMD_SIGNATURE,

    .upipe_alloc = upipe_ts_emmd_alloc,
    .upipe_input = upipe_ts_emmd_input,
    .upipe_control = upipe_ts_emmd_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for all ts_emmd pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_ts_emmd_mgr_alloc(void)
{
    return &upipe_ts_emmd_mgr;
}

/** @internal @This alloecmes a ts_emmd_ecm pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe alloecmor
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of alloecmion error
 */
static struct upipe *upipe_ts_emmd_ecm_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_ts_emmd_ecm_alloc_void(mgr, uprobe, signature,
                                                   args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_ts_emmd_ecm *upipe_ts_emmd_ecm = upipe_ts_emmd_ecm_from_upipe(upipe);

    upipe_ts_emmd_ecm->key = NULL;

    upipe_ts_emmd_ecm_init_sub(upipe);
    upipe_ts_emmd_ecm_init_urefcount(upipe);
    upipe_ts_emmd_ecm_init_output(upipe);
    upipe_ts_emmd_ecm_init_ubuf_mgr(upipe);
    upipe_ts_emmd_ecm_init_flow_def(upipe);
    upipe_ts_psid_table_init(upipe_ts_emmd_ecm->ecm);
    upipe_ts_psid_table_init(upipe_ts_emmd_ecm->next_ecm);
    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This validates the next ECM.
 *
 * @param upipe description structure of the pipe
 * @return false if the ECM is invalid
 */
static bool upipe_ts_emmd_ecm_table_validate(struct upipe *upipe)
{
    struct upipe_ts_emmd_ecm *upipe_ts_emmd_ecm = upipe_ts_emmd_ecm_from_upipe(upipe);
    upipe_ts_psid_table_foreach (upipe_ts_emmd_ecm->next_ecm, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            return false;

        if (/*!ecm_validate(section) || */!psi_check_crc(section)) {
            uref_block_unmap(section_uref, 0);
            return false;
        }

        uref_block_unmap(section_uref, 0);
    }
    return true;
}

/** @internal @This is a helper function to parse descriptors and import
 * the relevant ones into flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet to fill in
 * @param descl pointer to descriptor list
 * @param desclength length of the descriptor list
 * @return an error code
 */
static void upipe_ts_emmd_ecm_parse_descs(struct upipe *upipe,
                                      struct uref *flow_def,
                                      const uint8_t *descl, uint16_t desclength)
{
    const uint8_t *desc;
    int j = 0;
    /* cast needed because biTStream expects an uint8_t * (but doesn't write
     * to it */
    while ((desc = descl_get_desc((uint8_t *)descl, desclength, j++)) != NULL) {
        bool copy = false;
        switch (desc_get_tag(desc)) {
            default:
                copy = true;
                break;
        }

        if (copy) {
            UBASE_FATAL(upipe, uref_ts_flow_add_ecm_descriptor(flow_def,
                        desc, desc_get_length(desc) + DESC_HEADER_SIZE))
        }
    }
}

/** @internal @This parses a new PSI section.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_ts_emmd_ecm_input(struct upipe *upipe, struct uref *uref,
                                struct upump **upump_p)
{
    struct upipe_ts_emmd_ecm *upipe_ts_emmd_ecm = upipe_ts_emmd_ecm_from_upipe(upipe);
    assert(upipe_ts_emmd_ecm->flow_def_input != NULL);
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_sub_mgr(upipe->mgr);


    if (!upipe_ts_psid_table_section(upipe_ts_emmd_ecm->next_ecm, uref))
        return;

    if (upipe_ts_psid_table_validate(upipe_ts_emmd_ecm->ecm) &&
        upipe_ts_psid_table_compare(upipe_ts_emmd_ecm->ecm,
                                    upipe_ts_emmd_ecm->next_ecm)) {
        /* Identical ECM. */
        upipe_ts_psid_table_clean(upipe_ts_emmd_ecm->next_ecm);
        upipe_ts_psid_table_init(upipe_ts_emmd_ecm->next_ecm);
        return;
    }

    if (!ubase_check(upipe_ts_psid_table_merge(upipe_ts_emmd_ecm->next_ecm,
                                               upipe_ts_emmd_ecm->ubuf_mgr)) ||
        !upipe_ts_emmd_ecm_table_validate(upipe)) {
        upipe_warn(upipe, "invalid ECM section received");
        upipe_ts_psid_table_clean(upipe_ts_emmd_ecm->next_ecm);
        upipe_ts_psid_table_init(upipe_ts_emmd_ecm->next_ecm);
        return;
    }

    struct uref *flow_def = upipe_ts_emmd_ecm_alloc_flow_def_attr(upipe);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    UBASE_FATAL(upipe, uref_flow_set_def(flow_def, "void."))
    upipe_ts_psid_table_foreach (upipe_ts_emmd_ecm->next_ecm, section_uref) {
        const uint8_t *section;
        int size = -1;
        if (unlikely(!ubase_check(uref_block_read(section_uref, 0, &size,
                                                  &section))))
            continue;

        upipe_ts_emmd_ecm_parse_descs(upipe, flow_def,
                bissca_ecm_get_descl_const(section),
                bissca_ecm_get_desclength(section));
        /* uint16_t esid = */ psi_get_tableidext(section);
        /* uint16_t onid = */ bissca_ecm_get_onid(section);
        uint8_t cipher = bissca_ecm_get_cipher_type(section);
        if (cipher != BISSCA_ECM_CIPHER_AES_128_CBC)
            continue;
        bool odd = bissca_ecm_get_session_key_parity(section);
        uint8_t iv[16], even_k[16], odd_k[16];
        bissca_ecm_get_iv(section, iv);
        bissca_ecm_get_even_word(section, even_k);
        bissca_ecm_get_odd_word(section, odd_k);

        uint8_t k[2][16];
        for (int i = 0; i < 2; i++) {
            gcry_error_t err = gcry_cipher_setiv(upipe_ts_emmd->aes, iv, 16);
            assert(!err);
            err = gcry_cipher_setkey(upipe_ts_emmd->aes, upipe_ts_emmd->aes_key[odd], 16);
            assert(!err);
            err = gcry_cipher_decrypt(upipe_ts_emmd->aes, k[i], 16, i ? odd_k : even_k, 16);
            assert(!err);
        }
        upipe_throw(upipe, UPROBE_TS_EMMD_ECM_KEY_UPDATE,
                UPIPE_TS_EMMD_ECM_SIGNATURE, k[0], k[1]);

        uref_block_unmap(section_uref, 0);
    }

    /* Switch tables. */
    if (upipe_ts_psid_table_validate(upipe_ts_emmd_ecm->ecm))
        upipe_ts_psid_table_clean(upipe_ts_emmd_ecm->ecm);
    upipe_ts_psid_table_copy(upipe_ts_emmd_ecm->ecm, upipe_ts_emmd_ecm->next_ecm);
    upipe_ts_psid_table_init(upipe_ts_emmd_ecm->next_ecm);

    flow_def = upipe_ts_emmd_ecm_store_flow_def_attr(upipe, flow_def);
    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        uref_free(uref);
        return;
    }
    upipe_ts_emmd_ecm_store_flow_def(upipe, flow_def);
    /* Force sending flow def */
    upipe_ts_emmd_ecm_output(upipe, NULL, upump_p);
}

/** @internal @This receives an ubuf manager.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_ts_emmd_ecm_check(struct upipe *upipe, struct uref *flow_format)
{
    if (flow_format != NULL) {
        flow_format = upipe_ts_emmd_ecm_store_flow_def_input(upipe, flow_format);
        if (flow_format != NULL) {
            upipe_ts_emmd_ecm_store_flow_def(upipe, flow_format);
            /* Force sending flow def */
            upipe_ts_emmd_ecm_output(upipe, NULL, NULL);
        }
    }

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_ts_emmd_ecm_set_flow_def(struct upipe *upipe,
                                      struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_ECM_FLOW_DEF))
    struct uref *flow_def_dup;
    if (unlikely((flow_def_dup = uref_dup(flow_def)) == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }
    upipe_ts_emmd_ecm_demand_ubuf_mgr(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_ts_emmd_ecm_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_ts_emmd_ecm_control_output(upipe, command, args));
    UBASE_HANDLED_RETURN(upipe_ts_emmd_ecm_control_super(upipe, command, args));

    switch (command) {
        case UPIPE_GET_SUB_MGR: {
            struct upipe_mgr **p = va_arg(args, struct upipe_mgr **);
            return upipe_ts_emmd_get_sub_mgr(upipe, p);
        }
        case UPIPE_ITERATE_SUB: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_ts_emmd_iterate_ecm(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_ts_emmd_ecm_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_ts_emmd_ecm_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    struct upipe_ts_emmd_ecm *upipe_ts_emmd_ecm = upipe_ts_emmd_ecm_from_upipe(upipe);


    upipe_ts_psid_table_clean(upipe_ts_emmd_ecm->ecm);
    upipe_ts_psid_table_clean(upipe_ts_emmd_ecm->next_ecm);
    upipe_ts_emmd_ecm_clean_output(upipe);
    upipe_ts_emmd_ecm_clean_ubuf_mgr(upipe);
    upipe_ts_emmd_ecm_clean_flow_def(upipe);
    upipe_ts_emmd_ecm_clean_urefcount(upipe);
    upipe_ts_emmd_ecm_clean_sub(upipe);
    upipe_ts_emmd_ecm_free_void(upipe);
}

/** @internal @This initializes the input manager for a ecm pipe.
*
* @param upipe description structure of the pipe
*/
static void upipe_ts_emmd_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_ts_emmd *upipe_ts_emmd = upipe_ts_emmd_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_ts_emmd->sub_mgr;
    sub_mgr->refcount = upipe_ts_emmd_to_urefcount(upipe_ts_emmd);
    sub_mgr->signature = UPIPE_TS_EMMD_ECM_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_ts_emmd_ecm_alloc;
    sub_mgr->upipe_input = upipe_ts_emmd_ecm_input;
    sub_mgr->upipe_control = upipe_ts_emmd_ecm_control;
    sub_mgr->upipe_mgr_control = NULL;
}
