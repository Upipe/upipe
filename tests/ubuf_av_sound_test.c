/*
 * Copyright (C) 2025 EasyTools S.A.S.
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for ubuf_av manager for sound formats
 */

#undef NDEBUG

#include "upipe/ubuf_sound.h"
#include "upipe-av/ubuf_av.h"

#include <assert.h>
#include <libavutil/frame.h>

static void fill_in(struct ubuf *ubuf)
{
    size_t size;
    uint8_t sample_size;
    ubase_assert(ubuf_sound_size(ubuf, &size, &sample_size));
    int octets = size * sample_size;

    const char *channel;
    ubuf_sound_foreach_plane(ubuf, channel) {
        uint8_t *buffer;
        ubase_assert(ubuf_sound_plane_write_uint8_t(ubuf, channel, 0, -1,
                                                    &buffer));

        for (int x = 0; x < octets; x++)
            buffer[x] = (uint8_t)channel[0] + x;
        ubase_assert(ubuf_sound_plane_unmap(ubuf, channel, 0, -1));
    }
}

int main(int argc, char **argv)
{
    struct ubuf_mgr *mgr;
    struct ubuf *ubuf1, *ubuf2;
    const char *channel;
    size_t size;
    uint8_t sample_size;
    uint8_t *w;
    const uint8_t *r;
    AVFrame *frame;

    /* packed s16 stereo */
    mgr = ubuf_av_mgr_alloc();
    assert(mgr != NULL);

    frame = av_frame_alloc();
    assert(frame != NULL);
    frame->format = AV_SAMPLE_FMT_S16;
    frame->nb_samples = 32;
    frame->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
    assert(av_frame_get_buffer(frame, 0) == 0);

    ubuf1 = ubuf_sound_av_alloc(mgr, frame);
    assert(ubuf1 != NULL);
    av_frame_unref(frame);

    ubase_assert(ubuf_sound_size(ubuf1, &size, &sample_size));
    assert(size == 32);
    assert(sample_size == 4);

    unsigned int nb_planes = 0;
    ubuf_sound_foreach_plane(ubuf1, channel) {
        nb_planes++;
        assert(!strcmp(channel, "lr"));
    }
    assert(nb_planes == 1);

    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "lr", 0, -1, &r));
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "lr", 0, -1));

    fill_in(ubuf1);

    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "lr", 2, 1, &r));
    assert(*r == 'l' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "lr", 2, 1));

    ubuf2 = ubuf_dup(ubuf1);
    assert(ubuf2 != NULL);
    ubase_nassert(ubuf_sound_plane_write_uint8_t(ubuf1, "lr", 0, -1, &w));
    ubuf_free(ubuf2);

    ubase_nassert(ubuf_sound_resize(ubuf1, 0, 33));

    ubase_assert(ubuf_sound_resize(ubuf1, 2, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "lr", 0, -1, &r));
    assert(r[0] == 'l' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "lr", 0, -1));

    ubase_assert(ubuf_sound_resize(ubuf1, 0, 29));

    ubuf_free(ubuf1);

    ubuf_mgr_release(mgr);

    /* planar float 5.1 */
    mgr = ubuf_av_mgr_alloc();
    assert(mgr != NULL);

    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->nb_samples = 32;
    frame->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1;
    assert(av_frame_get_buffer(frame, 0) == 0);

    ubuf1 = ubuf_sound_av_alloc(mgr, frame);
    assert(ubuf1 != NULL);
    av_frame_unref(frame);

    ubase_assert(ubuf_sound_size(ubuf1, &size, &sample_size));
    assert(size == 32);
    assert(sample_size == sizeof(float));

    nb_planes = 0;
    ubuf_sound_foreach_plane(ubuf1, channel)
        nb_planes++;
    assert(nb_planes == 6);

    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "l", 0, -1, &r));
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "l", 0, -1));

    fill_in(ubuf1);

    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "l", 2, 1, &r));
    assert(*r == 'l' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "l", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "r", 2, 1, &r));
    assert(*r == 'r' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "r", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "c", 2, 1, &r));
    assert(*r == 'c' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "c", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "L", 2, 1, &r));
    assert(*r == 'L' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "L", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "R", 2, 1, &r));
    assert(*r == 'R' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "R", 2, 1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "S", 2, 1, &r));
    assert(*r == 'S' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "S", 2, 1));

    ubuf2 = ubuf_dup(ubuf1);
    assert(ubuf2 != NULL);
    ubase_nassert(ubuf_sound_plane_write_uint8_t(ubuf1, "l", 0, -1, &w));
    ubuf_free(ubuf2);

    ubase_nassert(ubuf_sound_resize(ubuf1, 0, 33));

    ubase_assert(ubuf_sound_resize(ubuf1, 2, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "l", 0, -1, &r));
    assert(r[0] == 'l' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "l", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "r", 0, -1, &r));
    assert(r[0] == 'r' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "r", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "c", 0, -1, &r));
    assert(r[0] == 'c' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "c", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "L", 0, -1, &r));
    assert(r[0] == 'L' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "L", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "R", 0, -1, &r));
    assert(r[0] == 'R' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "R", 0, -1));
    ubase_assert(ubuf_sound_plane_read_uint8_t(ubuf1, "S", 0, -1, &r));
    assert(r[0] == 'S' + 8);
    ubase_assert(ubuf_sound_plane_unmap(ubuf1, "S", 0, -1));

    ubase_assert(ubuf_sound_resize(ubuf1, 0, 29));

    ubuf_free(ubuf1);
    ubuf_mgr_release(mgr);
    av_frame_free(&frame);

    return 0;
}
