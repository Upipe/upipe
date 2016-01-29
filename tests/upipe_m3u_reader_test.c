#include <upipe/ubase.h>
#include <upipe/uref_m3u_flow.h>
#include <upipe/uref_m3u_playlist_flow.h>
#include <upipe/uref_m3u.h>
#include <upipe/uref_m3u_playlist.h>
#include <upipe/uref_m3u_master.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uprobe_stdio_color.h>
#include <upipe/uprobe_uref_mgr.h>
#include <upipe/uprobe_upump_mgr.h>
#include <upipe/uprobe_ubuf_mem.h>
#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upump-ev/upump_ev.h>
#include <upipe-modules/upipe_file_source.h>
#include <upipe-modules/upipe_m3u_reader.h>
#include <upipe-modules/upipe_probe_uref.h>
#include <upipe-modules/upipe_null.h>

#include <ev.h>

#include <stdlib.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 0
#define UREF_POOL_DEPTH 0
#define UBUF_POOL_DEPTH 0
#define UPUMP_POOL 0
#define UPUMP_BLOCKER_POOL 0

static int nb_files = 0;
static char **files = NULL;
static int current = 0;

static int catch_fsrc(struct uprobe *uprobe,
                      struct upipe *upipe,
                      int event, va_list args)
{
    switch (event) {
    case UPROBE_SOURCE_END:
        if (++current < nb_files)
            ubase_assert(upipe_set_uri(upipe, files[current]));
        return UBASE_ERR_NONE;
    }

    return uprobe_throw_next(uprobe, upipe, event, args);
}

static int catch_uref(struct uprobe *uprobe,
                      struct upipe *upipe,
                      int event, va_list args)
{
    switch (event) {
    case UPROBE_NEW_FLOW_DEF: {
        struct uref *uref = va_arg(args, struct uref *);

        const char *flow_def;
        ubase_assert(uref_flow_get_def(uref, &flow_def));
        printf("flow definition: %s\n", flow_def);

        uint8_t version;
        if (ubase_check(uref_m3u_flow_get_version(uref, &version)))
            printf("version: %u\n", version);

        const char *playlist_type;
        if (ubase_check(uref_m3u_playlist_flow_get_type(uref, &playlist_type)))
            printf("playlist type: %s\n", playlist_type);

        uint64_t target_duration;
        if (ubase_check(uref_m3u_playlist_flow_get_target_duration(
                    uref, &target_duration)))
            printf("playlist target duration: %"PRIu64"\n", target_duration);

        uint64_t media_sequence;
        if (ubase_check(uref_m3u_playlist_flow_get_media_sequence(
                    uref, &media_sequence)))
            printf("playlist target duration: %"PRIu64"\n", media_sequence);

        if (ubase_check(uref_m3u_playlist_flow_get_endlist(uref)))
            printf("playlist end\n");

        return UBASE_ERR_NONE;
    }

    case UPROBE_PROBE_UREF: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_PROBE_UREF_SIGNATURE)
        struct uref *uref = va_arg(args, struct uref *);

        const char *uri;
        if (ubase_check(uref_m3u_get_uri(uref, &uri)))
            printf("uri: %s\n", uri);

        uint64_t playlist_seq_duration;
        if (ubase_check(uref_m3u_playlist_get_seq_duration(
                    uref, &playlist_seq_duration)))
            printf("playlist sequence duration: %"PRIu64"\n",
                   playlist_seq_duration);

        uint64_t playlist_seq_time;
        if (ubase_check(uref_m3u_playlist_get_seq_time(
                    uref, &playlist_seq_time)))
            printf("playlist sequence time: %"PRIu64"\n",
                   playlist_seq_time);

        uint64_t playlist_byte_range_len;
        if (ubase_check(uref_m3u_playlist_get_byte_range_len(
                    uref, &playlist_byte_range_len)))
            printf("playlist byte range length: %"PRIu64"\n",
                   playlist_byte_range_len);

        uint64_t playlist_byte_range_off;
        if (ubase_check(uref_m3u_playlist_get_byte_range_off(
                    uref, &playlist_byte_range_off)))
            printf("playlist byte range offset: %"PRIu64"\n",
                   playlist_byte_range_off);

        uint64_t master_bandwidth;
        if (ubase_check(uref_m3u_master_get_bandwidth(
                    uref, &master_bandwidth)))
            printf("master bandwidth: %"PRIu64"\n", master_bandwidth);

        const char *master_codecs;
        if (ubase_check(uref_m3u_master_get_codecs(
                    uref, &master_codecs)))
            printf("master codecs: %s\n", master_codecs);

        const char *resolution;
        if (ubase_check(uref_m3u_master_get_resolution(
                    uref, &resolution)))
            printf("master resolution: %s\n", resolution);

        const char *audio;
        if (ubase_check(uref_m3u_master_get_audio(
                    uref, &audio)))
            printf("master audio: %s\n", audio);

        const char *media_type;
        if (ubase_check(uref_m3u_master_get_media_type(
                    uref, &media_type)))
            printf("master media_type: %s\n", media_type);

        const char *media_name;
        if (ubase_check(uref_m3u_master_get_media_name(
                    uref, &media_name)))
            printf("master media_name: %s\n", media_name);

        const char *media_group;
        if (ubase_check(uref_m3u_master_get_media_group(
                    uref, &media_group)))
            printf("master media_group: %s\n", media_group);

        if (ubase_check(uref_m3u_master_get_media_default(uref)))
            printf("master media_default\n");

        if (ubase_check(uref_m3u_master_get_media_autoselect(uref)))
            printf("master media_autoselect\n");

        return UBASE_ERR_NONE;
    }
    }
    return uprobe_throw_next(uprobe, upipe, event, args);
}

int main(int argc, char *argv[])
{
    assert(argc >= 2);
    nb_files = argc - 1;
    files = argv + 1;

    struct ev_loop *loop = ev_default_loop(0);
    assert(loop != NULL);
    struct upump_mgr *upump_mgr = upump_ev_mgr_alloc(loop, UPUMP_POOL,
                                                     UPUMP_BLOCKER_POOL);
    assert(upump_mgr != NULL);

    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *uref_mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr,
                                                   0);
    /* main probe */
    struct uprobe *logger =
        uprobe_stdio_color_alloc(NULL, stderr, UPROBE_LOG_VERBOSE);
    assert(logger != NULL);
    logger = uprobe_uref_mgr_alloc(logger, uref_mgr);
    assert(logger != NULL);
    logger = uprobe_upump_mgr_alloc(logger, upump_mgr);
    assert(logger != NULL);
    logger = uprobe_ubuf_mem_alloc(logger, umem_mgr, UBUF_POOL_DEPTH,
                                   UBUF_POOL_DEPTH);
    assert(logger != NULL);

    /* file source */
    struct uprobe uprobe_fsrc;
    uprobe_init(&uprobe_fsrc, catch_fsrc, uprobe_use(logger));
    struct upipe_mgr *upipe_fsrc_mgr = upipe_fsrc_mgr_alloc();
    assert(upipe_fsrc_mgr != NULL);
    struct upipe *upipe_fsrc = upipe_void_alloc(upipe_fsrc_mgr,
            uprobe_pfx_alloc(uprobe_use(&uprobe_fsrc),
                             UPROBE_LOG_DEBUG, "file source"));
    assert(upipe_fsrc != NULL);
    upipe_mgr_release(upipe_fsrc_mgr);
    ubase_assert(upipe_set_uri(upipe_fsrc, files[current]));

    /* m3u reader */
    struct upipe_mgr *upipe_m3u_reader_mgr = upipe_m3u_reader_mgr_alloc();
    assert(upipe_m3u_reader_mgr != NULL);
    struct upipe *upipe_m3u_reader = upipe_void_alloc_output(
        upipe_fsrc, upipe_m3u_reader_mgr,
        uprobe_pfx_alloc(uprobe_use(logger),
                         UPROBE_LOG_VERBOSE, "m3u reader"));
    upipe_mgr_release(upipe_m3u_reader_mgr);
    assert(upipe_m3u_reader != NULL);

    struct uprobe uprobe_uref;
    uprobe_init(&uprobe_uref, catch_uref, uprobe_use(logger));
    struct upipe_mgr *upipe_probe_uref_mgr = upipe_probe_uref_mgr_alloc();
    assert(upipe_probe_uref_mgr != NULL);
    struct upipe *upipe_probe_uref = upipe_void_chain_output(
        upipe_m3u_reader, upipe_probe_uref_mgr,
        uprobe_pfx_alloc(uprobe_use(&uprobe_uref),
                         UPROBE_LOG_DEBUG, "probe uref"));
    upipe_mgr_release(upipe_probe_uref_mgr);
    assert(upipe_probe_uref != NULL);

    /* null output */
    struct upipe_mgr *upipe_null_mgr = upipe_null_mgr_alloc();
    assert(upipe_null_mgr != NULL);
    struct upipe *upipe_null = upipe_void_chain_output(
        upipe_probe_uref, upipe_null_mgr,
        uprobe_pfx_alloc(uprobe_use(logger),
                         UPROBE_LOG_DEBUG, "null"));
    upipe_mgr_release(upipe_null_mgr);
    assert(upipe_null != NULL);
    upipe_release(upipe_null);

    /* run main loop */
    ev_loop(loop, 0);

    /* release */
    upipe_release(upipe_fsrc);
    upump_mgr_release(upump_mgr);
    uref_mgr_release(uref_mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    uprobe_clean(&uprobe_fsrc);
    uprobe_clean(&uprobe_uref);
    uprobe_release(logger);
    ev_default_destroy();
    return 0;
}
