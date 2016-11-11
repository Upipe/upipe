#include <stdio.h>
#include <stdlib.h>

#include <ev.h>

#include <upipe/uprobe.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio_color.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/upump.h>
#include <upipe/umem.h>
#include <upipe/umem_pool.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_auto_source.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/upipe_setflowdef.h>
#include <upipe-modules/uref_aes_flow.h>
#include <upipe-modules/upipe_aes_decrypt.h>
#include <upipe-modules/upipe_file_sink.h>

#define UPROBE_LOG_LEVEL UPROBE_LOG_DEBUG
#define UMEM_POOL 512
#define UDICT_POOL_DEPTH 500
#define UREF_POOL_DEPTH 500
#define UBUF_POOL_DEPTH 3000
#define UBUF_SHARED_POOL_DEPTH 50
#define UPUMP_POOL 10
#define UPUMP_BLOCKER_POOL 10

static int catch_src(struct uprobe *uprobe, struct upipe *upipe,
                     int event, va_list args)
{
    switch (event) {
    case UPROBE_SOURCE_END:
        upipe_release(upipe);
        return UBASE_ERR_NONE;
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

static void usage(const char *name, int exit_code)
{
    fprintf(stderr, "%s key iv in_file out_file\n"
            "\tkey      : a 16 bytes hexadecimal key\n"
            "\tiv       : a 16 bytes hexadecimal vector\n"
            "\tin_file  : the input file url\n"
            "\tout_file : the output file path\n", name);
    exit(exit_code);
}

int main(int argc, char *argv[])
{
    const char *name = argv[0];

    if (argc < 5)
        usage(name, EXIT_FAILURE);

    const char *key_str = argv[1];
    uint8_t key[16];
    if (strlen(key_str) != sizeof (key) * 2) {
        fprintf(stderr, "invalid key\n");
        usage(name, EXIT_FAILURE);
    }
    if (sscanf(key_str,
           "%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x",
           (unsigned *)&key[0],  (unsigned *)&key[1],  (unsigned *)&key[2],
           (unsigned *)&key[3],  (unsigned *)&key[4],  (unsigned *)&key[5],
           (unsigned *)&key[6],  (unsigned *)&key[7],  (unsigned *)&key[8],
           (unsigned *)&key[9],  (unsigned *)&key[10], (unsigned *)&key[11],
           (unsigned *)&key[12], (unsigned *)&key[13], (unsigned *)&key[14],
           (unsigned *)&key[15]) != 16) {
        fprintf(stderr, "invalid key\n");
        usage(name, EXIT_FAILURE);
    }

    const char *iv_str = argv[2];
    uint8_t iv[16];
    if (strlen(iv_str) != sizeof (iv) * 2) {
        fprintf(stderr, "invalid iv\n");
        usage(name, EXIT_FAILURE);
    }
    if (sscanf(iv_str,
           "%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x",
           (unsigned *)&iv[0],  (unsigned *)&iv[1],  (unsigned *)&iv[2],
           (unsigned *)&iv[3],  (unsigned *)&iv[4],  (unsigned *)&iv[5],
           (unsigned *)&iv[6],  (unsigned *)&iv[7],  (unsigned *)&iv[8],
           (unsigned *)&iv[9],  (unsigned *)&iv[10], (unsigned *)&iv[11],
           (unsigned *)&iv[12], (unsigned *)&iv[13], (unsigned *)&iv[14],
           (unsigned *)&iv[15]) != 16) {
        fprintf(stderr, "invalid iv\n");
        usage(name, EXIT_FAILURE);
    }

    const char *in = argv[3];
    const char *out = argv[4];

    struct ev_loop *loop = ev_default_loop(0);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);
    struct umem_mgr *umem_mgr = umem_pool_mgr_alloc_simple(UMEM_POOL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    udict_mgr_release(udict_mgr);

    /* probes */
    struct uprobe *uprobe;
    uprobe = uprobe_stdio_color_alloc(NULL, stderr, UPROBE_LOG_DEBUG);
    assert(uprobe != NULL);
    uprobe = uprobe_uref_mgr_alloc(uprobe, uref_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_upump_mgr_alloc(uprobe, upump_mgr);
    assert(uprobe != NULL);
    uprobe = uprobe_ubuf_mem_alloc(uprobe, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_SHARED_POOL_DEPTH);
    assert(uprobe != NULL);

    struct uref *dict = uref_alloc_control(uref_mgr);
    assert(dict != NULL);

    uref_mgr_release(uref_mgr);
    upump_mgr_release(upump_mgr);
    umem_mgr_release(umem_mgr);

    struct uprobe uprobe_src;
    uprobe_init(&uprobe_src, catch_src, uprobe_use(uprobe));

    struct upipe_mgr *upipe_auto_src_mgr = upipe_auto_src_mgr_alloc();
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    struct upipe_mgr *upipe_http_src_mgr = upipe_http_src_mgr_alloc();
    assert(upipe_auto_src_mgr != NULL &&
           upipe_fsrc_mgr != NULL &&
           upipe_http_src_mgr != NULL);
    upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "file", upipe_fsrc_mgr);
    upipe_auto_src_mgr_set_mgr(upipe_auto_src_mgr, "http", upipe_http_src_mgr);
    upipe_mgr_release(upipe_fsrc_mgr);
    upipe_mgr_release(upipe_http_src_mgr);
    struct upipe *src = upipe_void_alloc(
        upipe_auto_src_mgr,
        uprobe_pfx_alloc(uprobe_use(&uprobe_src),
                         UPROBE_LOG_LEVEL, "src"));
    upipe_mgr_release(upipe_auto_src_mgr);
    assert(src != NULL);
    ubase_assert(upipe_set_uri(src, in));

    struct upipe_mgr *upipe_setflowdef_mgr = upipe_setflowdef_mgr_alloc();
    assert(upipe_setflowdef_mgr != NULL);
    struct upipe *setflowdef = upipe_void_alloc_output(
        src, upipe_setflowdef_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_DEBUG, "setflowdef"));
    upipe_mgr_release(upipe_setflowdef_mgr);
    assert(setflowdef != NULL);

    ubase_assert(uref_flow_set_def(dict, "block.aes."));
    ubase_assert(uref_aes_set_method(dict, "AES-128"));
    ubase_assert(uref_aes_set_key(dict, key, sizeof (key)));
    ubase_assert(uref_aes_set_iv(dict, iv, sizeof (iv)));

    ubase_assert(upipe_setflowdef_set_dict(setflowdef, dict));
    uref_free(dict);

    struct upipe_mgr *upipe_aes_decrypt_mgr = upipe_aes_decrypt_mgr_alloc();
    assert(upipe_aes_decrypt_mgr != NULL);
    struct upipe *aes_decrypt = upipe_void_chain_output(
        setflowdef, upipe_aes_decrypt_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_DEBUG, "aes"));
    upipe_mgr_release(upipe_aes_decrypt_mgr);
    assert(aes_decrypt);

    struct upipe_mgr *upipe_fsink_mgr = upipe_fsink_mgr_alloc();
    assert(upipe_fsink_mgr != NULL);
    struct upipe *fsink = upipe_void_chain_output(
        aes_decrypt, upipe_fsink_mgr,
        uprobe_pfx_alloc(uprobe_use(uprobe), UPROBE_LOG_DEBUG, "sink"));
    upipe_mgr_release(upipe_fsink_mgr);
    ubase_assert(upipe_fsink_set_path(fsink, out, UPIPE_FSINK_OVERWRITE));
    upipe_release(fsink);

    ev_loop(loop, 0);

    uprobe_clean(&uprobe_src);

    ev_default_destroy();

    return 0;
}
