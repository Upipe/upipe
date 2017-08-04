/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
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

#include <stdlib.h>
#include <limits.h>
#include <libgen.h>
#include <ctype.h>

#include <upipe/ustring.h>
#include <upipe/uclock.h>
#include <upipe/uprobe_helper_uprobe.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/uprobe_prefix.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/uref_uri.h>
#include <upipe/uref_m3u.h>
#include <upipe/uref_m3u_master.h>
#include <upipe/uref_m3u_playlist.h>
#include <upipe/uref_m3u_flow.h>
#include <upipe/uref_m3u_playlist_flow.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uref_stream.h>
#include <upipe/upipe_helper_input.h>
#include <upipe-modules/upipe_m3u_reader.h>

/** @hidden */
static int duration_to_uclock(const char *str,
                              const char **endptr,
                              uint64_t *duration_p)
{
    const char *tmp = str;
    uint64_t duration = 0;

    if (unlikely(str == NULL))
        return UBASE_ERR_INVALID;

    for (; isdigit(*tmp); tmp++)
        duration = duration * 10 + *tmp - '0';
    duration *= UCLOCK_FREQ;

    if (*tmp == '.') {
        uint64_t freq = UCLOCK_FREQ / 10;
        for (tmp++; isdigit(*tmp); tmp++, freq /= 10)
                duration += (*tmp - '0') * freq;
    }

    if (endptr)
        *endptr = tmp;

    if (duration_p)
        *duration_p = duration;

    return UBASE_ERR_NONE;
}

static int attribute_iterate(const char **item,
                             struct ustring *name,
                             struct ustring *value)
{
    static const char name_set[] = { USTRING_ALPHA_UPPER, '-', '\0' };
    if (unlikely(!item) || unlikely(!*item))
        return UBASE_ERR_INVALID;

    struct ustring tmp = ustring_from_str(*item);
    if (unlikely(ustring_is_empty(tmp))) {
        *item = NULL;
        return UBASE_ERR_NONE;
    }

    /* remove whitespaces */
    tmp = ustring_shift_while(tmp, " ");
    *name = ustring_split_while(&tmp, name_set);
    if (unlikely(!ustring_match_str(tmp, "=")))
        return UBASE_ERR_INVALID;
    tmp = ustring_shift(tmp, 1);
    if (ustring_match_str(tmp, "\"")) {
        tmp = ustring_shift(tmp, 1);
        *value = ustring_split_until(&tmp, "\"");
        if (unlikely(!ustring_match_str(tmp, "\"")))
            return UBASE_ERR_INVALID;
        tmp = ustring_shift(tmp, 1);
    }
    else {
        *value = ustring_split_until(&tmp, " ,");
    }
    /* remove whitespaces */
    tmp = ustring_shift_while(tmp, " ");
    if (ustring_match_str(tmp, ","))
        tmp = ustring_shift(tmp, 1);
    else if (!ustring_is_empty(tmp))
        return UBASE_ERR_INVALID;
    *item = tmp.at;
    return UBASE_ERR_NONE;
}

#define EXPECTED_FLOW_DEF       "block."
#define M3U_FLOW_DEF            EXPECTED_FLOW_DEF "m3u."
#define PLAYLIST_FLOW_DEF       M3U_FLOW_DEF "playlist."
#define MASTER_FLOW_DEF         M3U_FLOW_DEF "master."

/** @internal @This is the private context of a m3u reader pipe. */
struct upipe_m3u_reader {
    /** urefcount management structure */
    struct urefcount urefcount;

    /** pipe acting as output */
    struct upipe *output;
    /** temporary flow format */
    struct uref *current_flow_def;
    /** output flow definition */
    struct uref *output_flow_def;
    /** output state */
    enum upipe_helper_output_state output_state;
    /** list of output requests */
    struct uchain request_list;

    /** for uref stream helper */
    struct uref *next_uref;
    size_t next_uref_size;
    struct uchain urefs;

    /** input flow format */
    struct uref *flow_def;
    /** current item */
    struct uref *item;
    /** current key */
    struct uref *key;
    /** list of items */
    struct uchain items;

    /** public upipe structure */
    struct upipe upipe;

    /** need flush */
    bool restart;
};

UPIPE_HELPER_UPIPE(upipe_m3u_reader, upipe, UPIPE_M3U_READER_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_m3u_reader, urefcount, upipe_m3u_reader_free)
UPIPE_HELPER_VOID(upipe_m3u_reader)

UPIPE_HELPER_OUTPUT(upipe_m3u_reader, output, output_flow_def,
                    output_state, request_list)

UPIPE_HELPER_UREF_STREAM(upipe_m3u_reader, next_uref, next_uref_size,
                         urefs, NULL)

/** @internal @This allocates a m3u reader pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_m3u_reader_alloc(struct upipe_mgr *mgr,
                                            struct uprobe *uprobe,
                                            uint32_t signature,
                                            va_list args)
{
    struct upipe *upipe =
        upipe_m3u_reader_alloc_void(mgr, uprobe, signature, args);

    upipe_m3u_reader_init_urefcount(upipe);
    upipe_m3u_reader_init_output(upipe);
    upipe_m3u_reader_init_uref_stream(upipe);

    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);
    ulist_init(&upipe_m3u_reader->items);
    upipe_m3u_reader->current_flow_def = NULL;
    upipe_m3u_reader->flow_def = NULL;
    upipe_m3u_reader->item = NULL;
    upipe_m3u_reader->key = NULL;
    upipe_m3u_reader->restart = false;
    upipe_throw_ready(upipe);

    return upipe;
}

/** @internal @This cleans the m3u reader items.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_m3u_reader_flush(struct upipe *upipe)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    uref_free(upipe_m3u_reader->current_flow_def);
    uref_free(upipe_m3u_reader->item);
    upipe_m3u_reader->item = NULL;

    struct uchain *uchain;
    while ((uchain = ulist_pop(&upipe_m3u_reader->items)) != NULL)
        uref_free(uref_from_uchain(uchain));
    upipe_m3u_reader_clean_uref_stream(upipe);
    upipe_m3u_reader_init_uref_stream(upipe);
}

/** @internal @This frees a m3u pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_m3u_reader_free(struct upipe *upipe)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    upipe_throw_dead(upipe);

    uref_free(upipe_m3u_reader->key);
    upipe_m3u_reader_flush(upipe);
    uref_free(upipe_m3u_reader->flow_def);
    upipe_m3u_reader_clean_uref_stream(upipe);
    upipe_m3u_reader_clean_output(upipe);
    upipe_m3u_reader_clean_urefcount(upipe);
    upipe_m3u_reader_free_void(upipe);
}

/** @internal @This gets or allocates the current item.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param item_p pointer to the item
 * @return an error code
 */
static int upipe_m3u_reader_get_item(struct upipe *upipe,
                                     struct uref *flow_def,
                                     struct uref **item_p)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    if (unlikely(item_p == NULL) || unlikely(flow_def == NULL))
        return UBASE_ERR_INVALID;

    if (unlikely(upipe_m3u_reader->item == NULL)) {
        upipe_m3u_reader->item = uref_sibling_alloc_control(flow_def);
        if (unlikely(upipe_m3u_reader->item == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return UBASE_ERR_ALLOC;
        }
    }

    *item_p = upipe_m3u_reader->item;
    return UBASE_ERR_NONE;
}

/** @internal @This checks a "#EXTM3U" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_process_m3u(struct upipe *upipe,
                                        struct uref *flow_def,
                                        const char *line)
{
    while (isspace(*line))
        line++;

    if (strlen(line)) {
        upipe_err(upipe, "invalid EXTM3U tag");
        return UBASE_ERR_INVALID;
    }
    upipe_verbose(upipe, "found EXTM3U tag");

    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def));
    if (strcmp(def, EXPECTED_FLOW_DEF))
        return UBASE_ERR_INVALID;
    return uref_flow_set_def(flow_def, M3U_FLOW_DEF);
}

/** @internal @This checks and parses a "#EXT-X-VERSION" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_process_version(struct upipe *upipe,
                                            struct uref *flow_def,
                                            const char *line)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, M3U_FLOW_DEF));

    char *endptr;
    unsigned long int version = strtoul(line, &endptr, 10);
    if (line == endptr || *endptr != '\0' || version > UINT8_MAX) {
        upipe_warn_va(upipe, "invalid version %s", line);
        return UBASE_ERR_INVALID;
    }
    upipe_dbg_va(upipe, "version: %u", version);
    return uref_m3u_flow_set_version(flow_def, version);
}

/** @internal @This checks and parses a "#EXT-X-TARGETDURATION" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_process_target_duration(struct upipe *upipe,
                                                    struct uref *flow_def,
                                                    const char *line)
{
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def));
    if (strcmp(def, M3U_FLOW_DEF) && strcmp(def, PLAYLIST_FLOW_DEF))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, PLAYLIST_FLOW_DEF));

    const char *endptr;
    uint64_t duration;
    UBASE_RETURN(duration_to_uclock(line, &endptr, &duration));
    if (line == endptr || strlen(endptr)) {
        upipe_warn_va(upipe, "invalid target duration %s", line);
        return UBASE_ERR_INVALID;
    }
    upipe_dbg_va(upipe, "target duration: %"PRIu64, duration);
    return uref_m3u_playlist_flow_set_target_duration(flow_def, duration);
}

/** @internal @This checks and parses a "#EXT-X-PLAYLIST-TYPE" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_process_playlist_type(struct upipe *upipe,
                                                  struct uref *flow_def,
                                                  const char *line)
{
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def));
    if (strcmp(def, M3U_FLOW_DEF) && strcmp(def, PLAYLIST_FLOW_DEF))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, PLAYLIST_FLOW_DEF));

    static const char *types[] = { "VOD", "EVENT" };
    const char *type = NULL;
    for (unsigned i = 0; !type && i < UBASE_ARRAY_SIZE(types); i++)
        if (!strcmp(line, types[i]))
            type = types[i];
    if (unlikely(type == NULL)) {
        upipe_warn_va(upipe, "invalid playlist type `%s'", line);
        return UBASE_ERR_INVALID;
    }
    upipe_dbg_va(upipe, "playlist type %s", type);
    return uref_m3u_playlist_flow_set_type(flow_def, type);
}

/** @internal @This checks and parses a "#EXT-X-MEDIA-SEQUENCE" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_ext_x_media_sequence(struct upipe *upipe,
                                                 struct uref *flow_def,
                                                 const char *line)
{
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def));
    if (strcmp(def, M3U_FLOW_DEF) && strcmp(def, PLAYLIST_FLOW_DEF))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, PLAYLIST_FLOW_DEF));

    char *endptr;
    uint64_t media_sequence = strtoull(line, &endptr, 10);
    if (endptr == line || strlen(endptr)) {
        upipe_warn_va(upipe, "invalid media sequence %s", line);
        return UBASE_ERR_INVALID;
    }
    upipe_dbg_va(upipe, "media sequence %"PRIu64, media_sequence);
    return uref_m3u_playlist_flow_set_media_sequence(flow_def, media_sequence);
}

/** @internal @This checks a "#EXT-X-ENDLIST" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_ext_x_endlist(struct upipe *upipe,
                                          struct uref *flow_def,
                                          const char *line)
{
    const char *def;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def));
    if (strcmp(def, M3U_FLOW_DEF) && strcmp(def, PLAYLIST_FLOW_DEF))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, PLAYLIST_FLOW_DEF));

    upipe_verbose(upipe, "endlist tag");
    return uref_m3u_playlist_flow_set_endlist(flow_def);
}

/** @internal @This checks and parses a "#EXTINF" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_process_extinf(struct upipe *upipe,
                                           struct uref *flow_def,
                                           const char *line)
{
    const char *def;
    struct uref *item;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def));
    if (strcmp(def, M3U_FLOW_DEF) && strcmp(def, PLAYLIST_FLOW_DEF))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, PLAYLIST_FLOW_DEF));
    UBASE_RETURN(upipe_m3u_reader_get_item(upipe, flow_def, &item));

    const char *endptr;
    uint64_t duration;
    UBASE_RETURN(duration_to_uclock(line, &endptr, &duration));
    if (line == endptr || *endptr != ',') {
        upipe_err_va(upipe, "invalid segment duration `%s'", line);
        return UBASE_ERR_INVALID;
    }
    upipe_verbose_va(upipe, "segment duration: %"PRIu64, duration);
    return uref_m3u_playlist_set_seq_duration(item, duration);
}

/** @internal @This checks and parses a "#EXT-X-STREAM-INF" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_ext_x_stream_inf(struct upipe *upipe,
                                             struct uref *flow_def,
                                             const char *line)
{
    if (!ubase_check(uref_flow_match_def(flow_def, M3U_FLOW_DEF)) &&
        !ubase_check(uref_flow_match_def(flow_def, MASTER_FLOW_DEF)))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, MASTER_FLOW_DEF));

    struct uref *item;
    UBASE_RETURN(upipe_m3u_reader_get_item(upipe, flow_def, &item));

    const char *iterator = line;
    bool bandwidth_present = false;
    while (true) {
        struct ustring name, value;
        int ret;

        ret = attribute_iterate(&iterator, &name, &value);
        if (!ubase_check(ret)) {
            upipe_err_va(upipe, "fail to parse attribute list at %s",
                         iterator);
            return ret;
        }

        if (iterator == NULL)
            break;

        char value_str[value.len + 1];
        int err = ustring_cpy(value, value_str, sizeof (value_str));
        if (unlikely(!ubase_check(err))) {
            upipe_err_va(upipe, "fail to copy ustring %.*s",
                         (int)value.len, value.at);
            continue;
        }

        if (!ustring_cmp_str(name, "BANDWIDTH")) {
            char *endptr;
            uint64_t bandwidth = strtoull(value_str, &endptr, 10);
            if (endptr == value_str)
                return UBASE_ERR_INVALID;
            err = uref_m3u_master_set_bandwidth(item, bandwidth);
            if (unlikely(!ubase_check(err)))
                upipe_err_va(upipe, "fail to set bandwidth to %s", value_str);
            else
                bandwidth_present = true;
        }
        else if (!ustring_cmp_str(name, "CODECS")) {
            err = uref_m3u_master_set_codecs(item, value_str);
            if (unlikely(!ubase_check(err)))
                upipe_err_va(upipe, "fail to set codecs to %s", value_str);
        }
        else if (!ustring_cmp_str(name, "AUDIO")) {
            err = uref_m3u_master_set_audio(item, value_str);
            if (unlikely(!ubase_check(err)))
                upipe_err_va(upipe, "fail to set audio to %s", value_str);
        }
        else if (!ustring_cmp_str(name, "RESOLUTION")) {
            err = uref_m3u_master_set_resolution(item, value_str);
            if (unlikely(!ubase_check(err)))
                upipe_err_va(upipe, "fail to set resolution to %s", value_str);
        }
        else {
            upipe_warn_va(upipe, "ignoring attribute %.*s (%.*s)",
                          (int)name.len, name.at,
                          (int)value.len, value.at);
        }
    }

    if (!bandwidth_present)
        upipe_warn(upipe, "no bandwidth attribute found");

    return UBASE_ERR_NONE;
}

/** @internal @This checks and parses a "#EXT-X-BYTERANGE" tag.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param line the trailing characters of the line
 * @return an error code
 */
static int upipe_m3u_reader_process_byte_range(struct upipe *upipe,
                                               struct uref *flow_def,
                                               const char *line)
{
    const char *def;
    struct uref *item;
    UBASE_RETURN(uref_flow_get_def(flow_def, &def));
    if (strcmp(def, M3U_FLOW_DEF) && strcmp(def, PLAYLIST_FLOW_DEF))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, PLAYLIST_FLOW_DEF));
    UBASE_RETURN(upipe_m3u_reader_get_item(upipe, flow_def, &item));

    char *endptr = NULL;
    unsigned long long byte_range_len = strtoull(line, &endptr, 10);
    if (endptr == line || (*endptr != '\0' && *endptr != '@')) {
        upipe_warn_va(upipe, "invalid byte range %s", line);
        return UBASE_ERR_INVALID;
    }

    upipe_verbose_va(upipe, "byte range length: %"PRIu64, byte_range_len);
    if (*endptr == '@') {
        line = endptr + 1;
        unsigned long long byte_range_off = strtoull(line, &endptr, 10);
        if (endptr == line || *endptr != '\0') {
            upipe_warn_va(upipe, "invalid byte range %s", line);
            return UBASE_ERR_INVALID;
        }

        upipe_verbose_va(upipe, "byte range offset: %"PRIu64, byte_range_off);
        UBASE_RETURN(uref_m3u_playlist_set_byte_range_off(
                item, byte_range_off));
    }

    return uref_m3u_playlist_set_byte_range_len(item, byte_range_len);
}

static int upipe_m3u_reader_process_media(struct upipe *upipe,
                                          struct uref *flow_def,
                                          const char *line)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    if (!ubase_check(uref_flow_match_def(flow_def, M3U_FLOW_DEF)) &&
        !ubase_check(uref_flow_match_def(flow_def, MASTER_FLOW_DEF)))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, MASTER_FLOW_DEF));

    struct uref *item = uref_sibling_alloc_control(flow_def);
    if (unlikely(item == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    struct ustring name, value;
    while (ubase_check(attribute_iterate(&line, &name, &value)) && line) {
        if (!ustring_cmp_str(name, "URI")) {
            value = ustring_unframe(value, '"');
            char val[value.len + 1];
            int err = ustring_cpy(value, val, sizeof (val));
            if (unlikely(!ubase_check(err))) {
                upipe_warn_va(upipe, "fail to copy %.*s",
                              (int)value.len, value.at);
                continue;
            }
            err = uref_m3u_set_uri(item, val);
            if (unlikely(!ubase_check(err))) {
                upipe_warn_va(upipe, "fail to set uri %s", val);
                continue;
            }
        }
        else if (!ustring_cmp_str(name, "TYPE")) {
            char val[value.len + 1];
            int err = ustring_cpy(value, val, sizeof (val));
            if (unlikely(!ubase_check(err))) {
                upipe_warn_va(upipe, "fail to copy %.*s",
                              (int)value.len, value.at);
                continue;
            }
            err = uref_m3u_master_set_media_type(item, val);
            if (unlikely(!ubase_check(err))) {
                upipe_warn_va(upipe, "fail to set media type %s", val);
                continue;
            }
        }
        else if (!ustring_cmp_str(name, "DEFAULT")) {
            if (!ustring_cmp_str(value, "YES")) {
                int err = uref_m3u_master_set_media_default(item);
                if (unlikely(!ubase_check(err)))
                    continue;
            }
            else if (ustring_cmp_str(value, "NO")) {
                upipe_warn_va(upipe, "invalid DEFAULT value %.*s",
                              (int)value.len, value.at);
                continue;
            }
        }
        else if (!ustring_cmp_str(name, "AUTOSELECT")) {
            if (!ustring_cmp_str(value, "YES")) {
                int err = uref_m3u_master_set_media_autoselect(item);
                if (unlikely(!ubase_check(err)))
                    continue;
            }
            else if (ustring_cmp_str(value, "NO")) {
                upipe_warn_va(upipe, "invalid AUTOSELECT value %.*s",
                              (int)value.len, value.at);
                continue;
            }
        }
        else if (!ustring_cmp_str(name, "NAME")) {
            value = ustring_unframe(value, '"');
            char val[value.len + 1];
            int err = ustring_cpy(value, val, sizeof (val));
            if (unlikely(!ubase_check(err))) {
                upipe_warn_va(upipe, "fail to copy %.*s",
                              (int)value.len, value.at);
                continue;
            }
            err = uref_m3u_master_set_media_name(item, val);
            if (unlikely(!ubase_check(err))) {
                upipe_warn_va(upipe, "fail to set media name %s", val);
                continue;
            }
        }
        else if (!ustring_cmp_str(name, "GROUP-ID")) {
            value = ustring_unframe(value, '"');
            char val[value.len + 1];
            int err = ustring_cpy(value, val, sizeof (val));
            if (unlikely(!ubase_check(err))) {
                upipe_warn_va(upipe, "fail to copy %.*s",
                              (int)value.len, value.at);
                continue;
            }
            err = uref_m3u_master_set_media_group(item, val);
            if (unlikely(!ubase_check(err))) {
                upipe_warn_va(upipe, "fail to set group id %s", val);
                continue;
            }
        }
        else {
            upipe_warn_va(upipe, "ignoring attribute %.*s (%.*s)",
                          (int)name.len, name.at,
                          (int)value.len, value.at);
        }
    }
    ulist_add(&upipe_m3u_reader->items, uref_to_uchain(item));
    return UBASE_ERR_NONE;
}

static int upipe_m3u_reader_key(struct upipe *upipe,
                                struct uref *flow_def,
                                const char *line)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    if (unlikely(!ubase_check(uref_flow_match_def(flow_def,
                                                  M3U_FLOW_DEF))) &&
        unlikely(!ubase_check(uref_flow_match_def(flow_def,
                                                  PLAYLIST_FLOW_DEF))))
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_set_def(flow_def, PLAYLIST_FLOW_DEF));

    struct uref *key = uref_sibling_alloc_control(flow_def);
    if (unlikely(key == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    if (upipe_m3u_reader->key)
        uref_free(upipe_m3u_reader->key);
    upipe_m3u_reader->key = key;

    const char *iterator = line;
    struct ustring name, value;
    while (ubase_check(attribute_iterate(&iterator, &name, &value)) &&
           iterator != NULL) {
        char value_str[value.len + 1];
        int err = ustring_cpy(value, value_str, sizeof (value_str));
        if (unlikely(!ubase_check(err))) {
            upipe_err_va(upipe, "fail to copy ustring %.*s",
                         (int)value.len, value.at);
            continue;
        }

        if (!ustring_cmp_str(name, "METHOD")) {
            err = uref_m3u_playlist_key_set_method(key, value_str);
            if (unlikely(!ubase_check(err)))
                upipe_err_va(upipe, "fail to set key method to %s", value_str);
        }
        else if (!ustring_cmp_str(name, "URI")) {
            err = uref_m3u_playlist_key_set_uri(key, value_str);
            if (unlikely(!ubase_check(err)))
                upipe_err_va(upipe, "fail to set uri to %s", value_str);
        }
        else if (!ustring_cmp_str(name, "IV")) {
            size_t len = strlen(value_str);
            if (unlikely(len > 32)) {
                upipe_warn_va(upipe, "invalid initialization vector %s",
                              value_str);
                continue;
            }
            for (unsigned i = 0; i < len; i += 2) {
                if (unlikely(!isxdigit(value_str[i])) ||
                    unlikely(!isxdigit(value_str[i + 1]))) {
                    upipe_warn_va(upipe, "invalid initialization vector %s",
                                  value_str);
                    continue;
                }
                //FIXME
                //iv[] = value_str[i]
            }

        }
        else {
            upipe_warn_va(upipe, "ignoring attribute %.*s (%.*s)",
                          (int)name.len, name.at, (int)value.len, value.at);
        }
    }

    return UBASE_ERR_NONE;
}

/** @internal @This checks an URI.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param uri the uri
 * @return an error code
 */
static int upipe_m3u_reader_process_uri(struct upipe *upipe,
                                        struct uref *flow_def,
                                        const char *uri)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    upipe_verbose_va(upipe, "uri %s", uri);
    UBASE_RETURN(uref_flow_match_def(flow_def, M3U_FLOW_DEF));
    struct uref *item;
    UBASE_RETURN(upipe_m3u_reader_get_item(upipe, flow_def, &item));
    UBASE_RETURN(uref_m3u_set_uri(item, uri));
    if (upipe_m3u_reader->key)
        UBASE_RETURN(uref_m3u_playlist_key_copy(item, upipe_m3u_reader->key));
    upipe_m3u_reader->item = NULL;
    ulist_add(&upipe_m3u_reader->items, uref_to_uchain(item));
    return UBASE_ERR_NONE;
}

/** @internal @This checks and parses a line of a m3u file.
 *
 * @param upipe description structure of the pipe
 * @param flow_def the current flow definition
 * @param uref pointer to uref carrying the line to parse
 * @return an error code
 */
static int upipe_m3u_reader_process_line(struct upipe *upipe,
                                         struct uref *flow_def,
                                         struct uref *uref)
{
    static const struct {
        const char *pfx;
        int (*cb)(struct upipe *, struct uref *, const char *);
    } ext_cb[] = {
        { "#EXTM3U", upipe_m3u_reader_process_m3u },
        { "#EXT-X-VERSION:", upipe_m3u_reader_process_version },
        { "#EXT-X-TARGETDURATION:", upipe_m3u_reader_process_target_duration },
        { "#EXT-X-PLAYLIST-TYPE:", upipe_m3u_reader_process_playlist_type },
        { "#EXTINF:", upipe_m3u_reader_process_extinf },
        { "#EXT-X-BYTERANGE:", upipe_m3u_reader_process_byte_range },
        { "#EXT-X-MEDIA:", upipe_m3u_reader_process_media },
        { "#EXT-X-STREAM-INF:", upipe_m3u_reader_ext_x_stream_inf },
        { "#EXT-X-MEDIA-SEQUENCE:", upipe_m3u_reader_ext_x_media_sequence },
        { "#EXT-X-ENDLIST", upipe_m3u_reader_ext_x_endlist },
        { "#EXT-X-KEY:", upipe_m3u_reader_key },
    };

    size_t block_size;
    UBASE_RETURN(uref_block_size(uref, &block_size));

    uint8_t buffer[block_size + 1];
    memset(buffer, 0, sizeof (buffer));
    UBASE_RETURN(uref_block_extract(uref, 0, block_size, buffer));

    char *line = (char *)buffer;
    /* remove end of line */
    if (strlen(line) && line[strlen(line) - 1] == '\n') {
        line[strlen(line) - 1] = '\0';
        if (strlen(line) && line[strlen(line) - 1] == '\r')
            line[strlen(line) - 1] = '\0';
    }

    if (!strlen(line))
        return UBASE_ERR_NONE;

    if (*line == '#') {
        for (unsigned i = 0; i < UBASE_ARRAY_SIZE(ext_cb); i++) {
            if (strncmp(line, ext_cb[i].pfx, strlen(ext_cb[i].pfx)))
                continue;

            return ext_cb[i].cb(upipe, flow_def,
                                line + strlen(ext_cb[i].pfx));
        }
        upipe_dbg_va(upipe, "ignore `%s'", line);
        return UBASE_ERR_NONE;
    }

    return upipe_m3u_reader_process_uri(upipe, flow_def, line);
}

/** @internal @This parses and outputs a m3u file.
 *
 * @param upipe description structure of the pipe
 * @return an error code
 */
static void upipe_m3u_reader_process(struct upipe *upipe)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    if (unlikely(upipe_m3u_reader->current_flow_def == NULL)) {
        upipe_m3u_reader->current_flow_def =
            uref_dup(upipe_m3u_reader->flow_def);
        if (unlikely(upipe_m3u_reader->current_flow_def == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
            return;
        }
    }

    /* parse m3u */
    int ret = UBASE_ERR_NONE;
    struct uref *uref = upipe_m3u_reader->next_uref;
    for (size_t offset = 0;
         uref &&
         ubase_check(ret) &&
         ubase_check(uref_block_scan(uref, &offset, '\n'));
         offset = 0, uref = upipe_m3u_reader->next_uref) {
        struct uref *line =
            upipe_m3u_reader_extract_uref_stream(upipe, offset + 1);
        ret = upipe_m3u_reader_process_line(
            upipe, upipe_m3u_reader->current_flow_def, line);
        uref_free(line);
    }

    if (!ubase_check(ret))
        upipe_throw_error(upipe, ret);
}

/** @internal @This outputs the m3u.
 *
 * @param upipe description structure of the pipe
 * @param upump_p reference to source pump to block
 */
static void upipe_m3u_reader_output_all(struct upipe *upipe,
                                        struct upump **upump_p)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    struct uref *flow_def = upipe_m3u_reader->current_flow_def;
    upipe_m3u_reader->current_flow_def = NULL;
    if (unlikely(!ubase_check(uref_flow_match_def(flow_def, M3U_FLOW_DEF)))) {
        uref_free(flow_def);
        upipe_throw_error(upipe, UBASE_ERR_INVALID);
        return;
    }

    /* force new flow def */
    upipe_m3u_reader_store_flow_def(upipe, NULL);
    /* set output flow def */
    upipe_m3u_reader_store_flow_def(upipe, flow_def);

    /* output */
    struct uchain *uchain;
    bool first = true;
    upipe_use(upipe);
    while ((uchain = ulist_pop(&upipe_m3u_reader->items)) != NULL) {
        struct uref *uref = uref_from_uchain(uchain);

        if (first)
            uref_block_set_start(uref);
        first = false;

        if (ulist_empty(&upipe_m3u_reader->items))
            uref_block_set_end(uref);

        upipe_m3u_reader_output(upipe, uref, upump_p);
    }
    upipe_release(upipe);
}

/** @internal @This is called when there is new data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref carrying the data
 * @param upump_p reference to pump that generated the buffer
 */
static void upipe_m3u_reader_input(struct upipe *upipe, struct uref *uref,
                                   struct upump **upump_p)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    if (ubase_check(uref_block_get_start(uref)))
        upipe_m3u_reader->restart = true;
    if (upipe_m3u_reader->restart) {
        upipe_m3u_reader_flush(upipe);
        upipe_m3u_reader->restart = false;
    }

    bool end = false;
    if (unlikely(ubase_check(uref_block_get_end(uref))))
        end = true;

    upipe_m3u_reader_append_uref_stream(upipe, uref);
    upipe_m3u_reader_process(upipe);

    if (unlikely(end)) {
        upipe_m3u_reader_output_all(upipe, upump_p);
        upipe_m3u_reader->restart = true;
    }
}

/** @internal @This sets the input flow def of the m3u pipe.
 *
 * @param upipe description structure of the pipe
 * @param flow_def new input flow definition
 * @return an error code
 */
static int upipe_m3u_reader_set_flow_def(struct upipe *upipe,
                                         struct uref *flow_def)
{
    struct upipe_m3u_reader *upipe_m3u_reader =
        upipe_m3u_reader_from_upipe(upipe);

    UBASE_RETURN(uref_flow_match_def(flow_def, EXPECTED_FLOW_DEF));

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    uref_free(upipe_m3u_reader->flow_def);
    upipe_m3u_reader->flow_def = flow_def_dup;
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a m3u reader pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_m3u_reader_control(struct upipe *upipe,
                                    int command,
                                    va_list args)
{
    UBASE_HANDLED_RETURN(upipe_m3u_reader_control_output(upipe, command, args));
    switch (command) {
    case UPIPE_SET_FLOW_DEF: {
        struct uref *p = va_arg(args, struct uref *);
        return upipe_m3u_reader_set_flow_def(upipe, p);
    }

    default:
        return UBASE_ERR_UNHANDLED;
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_m3u_reader_mgr = {
    .refcount = NULL,
    .signature = UPIPE_M3U_READER_SIGNATURE,

    .upipe_alloc = upipe_m3u_reader_alloc,
    .upipe_input = upipe_m3u_reader_input,
    .upipe_control = upipe_m3u_reader_control,

    .upipe_mgr_control = NULL,
};

/** @This returns the management structure for m3u reader pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_m3u_reader_mgr_alloc(void)
{
    return &upipe_m3u_reader_mgr;
}
