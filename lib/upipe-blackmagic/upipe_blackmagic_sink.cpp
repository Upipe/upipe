/*
 * Copyright (C) 2014-2016 Open Broadcast Systems Ltd.
 *
 * Authors: Kieran Kunhya
 *          Rafaël Carré
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe bmd_sink module
 */

#define __STDC_LIMIT_MACROS    1
#define __STDC_FORMAT_MACROS   1
#define __STDC_CONSTANT_MACROS 1

#include "upipe/config.h"
#include "upipe/uatomic.h"
#include "upipe/ulist.h"
#include "upipe/uqueue.h"
#include "upipe/uprobe.h"
#include "upipe/uclock.h"
#include "upipe/uref.h"
#include "upipe/uref_attr_s12m.h"
#include "upipe/uref_block.h"
#include "upipe/uref_pic.h"
#include "upipe/uref_pic_flow.h"
#include "upipe/uref_sound.h"
#include "upipe/uref_sound_flow.h"
#include "upipe/uref_clock.h"
#include "upipe/uref_dump.h"
#include "upipe/upipe.h"
#include "upipe/upipe_helper_flow.h"
#include "upipe/upipe_helper_upipe.h"
#include "upipe/upipe_helper_subpipe.h"
#include "upipe/upipe_helper_upump_mgr.h"
#include "upipe/upipe_helper_upump.h"
#include "upipe/upipe_helper_urefcount.h"
#include "upipe/upipe_helper_uclock.h"
#include "upipe/upipe_helper_sync.h"
#include "upipe-blackmagic/upipe_blackmagic_sink.h"

#include <arpa/inet.h>
#include <assert.h>

#ifdef UPIPE_HAVE_LIBZVBI_H
#include <libzvbi.h>
#endif

#include <pthread.h>

#include <bitstream/dvb/vbi.h>

#include "include/DeckLinkAPI.h"

extern "C" {
    #include "sdi.h"
}

#define PREROLL_FRAMES 3

#define DECKLINK_CHANNELS 16

#define OP47_PACKETS_PER_FIELD 5

static const unsigned max_samples = (uint64_t)48000 * 1001 / 24000;
static const size_t audio_buf_size = max_samples * DECKLINK_CHANNELS * sizeof(int32_t);

inline static unsigned bcd2uint(uint8_t bcd)
{
   unsigned low  = bcd & 0xf;
   unsigned high = bcd >> 4;
   if (low > 9 || high > 9)
       return 0;
   return low + 10*high;
}

class upipe_bmd_sink_timecode : public IDeckLinkTimecode
{
public:
    upipe_bmd_sink_timecode(uint32_t _BCD) : BCD(_BCD) { }

    virtual BMDTimecodeBCD STDMETHODCALLTYPE GetBCD (void) {
       return BCD;
    }

    virtual HRESULT STDMETHODCALLTYPE GetComponents(uint8_t *hours, uint8_t *minutes, uint8_t *seconds, uint8_t *frames) {
        *hours   = bcd2uint( BCD & 0x3f);
        *minutes = bcd2uint((BCD >> 8) & 0x7f);
        *seconds = bcd2uint((BCD >> 16) & 0x7f);
        *frames  = bcd2uint((BCD >> 24) & 0x3f);
        return S_OK;
    }

    virtual BMDTimecodeFlags STDMETHODCALLTYPE GetFlags() {
       return !!(BCD & (1 << 30)) ? bmdTimecodeIsDropFrame : bmdTimecodeFlagDefault;
    }

    virtual HRESULT STDMETHODCALLTYPE GetTimecodeUserBits(BMDTimecodeUserBits *userBits) {
         *userBits = GetBCD();
         return S_OK;
    }

    virtual HRESULT GetString (const char **timecode) {
        uint8_t h, m, s, f, drop = (this->GetFlags() == bmdTimecodeIsDropFrame);
        GetComponents(&h, &m, &s, &f);

        if (!(*timecode = (const char*)calloc(16, sizeof(char)))) {
            return S_FALSE;
        }

        snprintf((char*)*timecode, 16, "%02u:%02u:%02u%c%02u", h, m, s, drop ? ';' : ':', f);
        return S_OK;
    }

    virtual ULONG STDMETHODCALLTYPE AddRef(void) {
        return uatomic_fetch_add(&refcount, 1) + 1;
    }

    virtual ULONG STDMETHODCALLTYPE Release(void) {
        uint32_t new_ref = uatomic_fetch_sub(&refcount, 1) - 1;
        if (new_ref == 0)
            delete this;
        return new_ref;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) {
        return E_NOINTERFACE;
    }

private:
    uint32_t BCD;
    uatomic_uint32_t refcount;
};

class upipe_bmd_sink_frame : public IDeckLinkVideoFrame
{
public:
    upipe_bmd_sink_frame(struct uref *_uref, void *_buffer, long _width, long _height, size_t _stride, uint64_t _pts) :
                         uref(_uref), data(_buffer), width(_width), height(_height), stride(_stride), pts(_pts) {
        uatomic_init(&refcount, 1);
    }

    ~upipe_bmd_sink_frame(void) {
        uatomic_clean(&refcount);
        uref_pic_plane_unmap(uref, "u10y10v10y10u10y10v10y10u10y10v10y10", 0, 0, -1, -1);
        uref_free(uref);
    }

    virtual long STDMETHODCALLTYPE GetWidth(void) {
        return width;
    }

    virtual long STDMETHODCALLTYPE GetHeight(void) {
        return height;
    }

    virtual long STDMETHODCALLTYPE GetRowBytes(void) {
        return stride;
    }

    virtual BMDPixelFormat STDMETHODCALLTYPE GetPixelFormat(void) {
        return bmdFormat10BitYUV;
    }

    virtual BMDFrameFlags STDMETHODCALLTYPE GetFlags(void) {
        return bmdVideoOutputFlagDefault;
    }

    virtual HRESULT STDMETHODCALLTYPE GetBytes(void **buffer) {
        *buffer = data;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE GetTimecode(BMDTimecodeFormat format,
                                                  IDeckLinkTimecode **_timecode) {
        timecode->AddRef();
        *_timecode = timecode;
        return S_FALSE;
    }

    virtual HRESULT STDMETHODCALLTYPE SetTimecode(upipe_bmd_sink_timecode &_timecode) {
        timecode = &_timecode;
        return S_FALSE;
    }

    virtual HRESULT STDMETHODCALLTYPE GetAncillaryData(IDeckLinkVideoFrameAncillary **ancillary) {
        frame_anc->AddRef();
        *ancillary = frame_anc;
        return S_OK;
    }

    virtual HRESULT STDMETHODCALLTYPE SetAncillaryData(IDeckLinkVideoFrameAncillary *ancillary) {
        frame_anc = ancillary;
        return S_OK;
    }

    virtual ULONG STDMETHODCALLTYPE AddRef(void) {
        frame_anc->AddRef();
        return uatomic_fetch_add(&refcount, 1) + 1;
    }

    virtual ULONG STDMETHODCALLTYPE Release(void) {
        frame_anc->Release();
        uint32_t new_ref = uatomic_fetch_sub(&refcount, 1) - 1;
        if (new_ref == 0)
            delete this;
        return new_ref;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) {
        if (memcmp(&iid, &IID_IDeckLinkVideoFrame, sizeof(REFIID)) == 0) {
            frame_anc->AddRef();
            uatomic_fetch_add(&refcount, 1);
            *ppv = (LPVOID *)this;
            return S_OK;
        }
        return E_NOINTERFACE;
    }

private:
    struct uref *uref;
    void *data;
    long width;
    long height;
    size_t stride;

    uatomic_uint32_t refcount;
    IDeckLinkVideoFrameAncillary *frame_anc;
    upipe_bmd_sink_timecode *timecode;

public:
    uint64_t pts;
};

static float dur_to_time(int64_t dur)
{
    return (float)dur / UCLOCK_FREQ;
}

static float pts_to_time(uint64_t pts)
{
    static uint64_t first = 0;
    if (!first)
        first = pts;

    return dur_to_time(pts - first);
}

#define BMD_SUBPIPE_TYPE_UNKNOWN 0
#define BMD_SUBPIPE_TYPE_SOUND   1
#define BMD_SUBPIPE_TYPE_TTX     2
#define BMD_SUBPIPE_TYPE_SCTE_35 3

/** @internal @This is the private context of an output of an bmd_sink sink
 * pipe. */
struct upipe_bmd_sink_sub {
    struct urefcount urefcount;

    struct upipe *upipe_bmd_sink;

    /** thread-safe urefs queue */
    struct uqueue uqueue;
    void *uqueue_extra;

    struct uref *uref;

    /** structure for double-linked lists */
    struct uchain uchain;

    /** delay applied to pts attribute when uclock is provided */
    uint64_t latency;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** watcher */
    struct upump *upump;

    /** subpipe type **/
    uint8_t type;

    bool dolby_e;

    bool s337;

    /** number of channels */
    uint8_t channels;

    /** position in the SDI stream */
    uint8_t channel_idx;

    /** public upipe structure */
    struct upipe upipe;
};

class callback;

/** upipe_bmd_sink structure */
struct upipe_bmd_sink {
    /** refcount management structure */
    struct urefcount urefcount;

    /** manager to create subs */
    struct upipe_mgr sub_mgr;
    /** pic subpipe */
    struct upipe_bmd_sink_sub pic_subpipe;

    /** list of input subpipes */
    struct uchain inputs;

    /** lock the list of subpipes, they are iterated from the
     * decklink callback */
    pthread_mutex_t lock;

    /** card index **/
    int card_idx;
    /** card topology */
    int64_t card_topo;

    /** selected output mode */
    BMDDisplayMode selectedMode;
    /** output mode **/
    BMDDisplayMode mode;

    /** video frame index (modulo 5) */
    uint8_t frame_idx;

    uint64_t start_pts;
    uatomic_uint32_t preroll;

    /** vanc/vbi temporary buffer **/

    /** closed captioning **/
    uint16_t cdp_hdr_sequence_cntr;

    /** OP47 teletext sequence counter **/
    // XXX: should counter be per-field?
    uint16_t op47_sequence_counter[2];

    /** OP47 teletext buffer. 5, packets per field */
    uint8_t op47_ttx_buf[DVBVBI_LENGTH * OP47_PACKETS_PER_FIELD * 2];

#ifdef UPIPE_HAVE_LIBZVBI_H
    /** vbi **/
    vbi_sampling_par sp;
#endif

    /** handle to decklink card */
    IDeckLink *deckLink;
    /** handle to decklink card output */
    IDeckLinkOutput *deckLinkOutput;

    IDeckLinkDisplayMode *displayMode;

    /** card name */
    const char *modelName;

    /** hardware uclock */
    struct uclock uclock;

    /** external clock */
    struct uclock *uclock_external;
    /** external clock request */
    struct urequest uclock_external_request;

    /** genlock status */
    int genlock_status;

    /** time at which we got genlock */
    uint64_t genlock_transition_time;

    /** clock offset to ensure it is increasing */
    uint64_t offset;

    /** frame duration */
    uint64_t ticks_per_frame;

    /** public upipe structure */
    struct upipe upipe;

    /** Frame completion callback */
    callback *cb;

    /** audio buffer to merge tracks */
    int32_t *audio_buf;

    /** offset between audio sample 0 and dolby e first sample*/
    uint8_t dolbye_offset;

    /** pass through closed captions */
    uatomic_uint32_t cc;

    /** pass through teletext */
    uatomic_uint32_t ttx;

    /** pass through timecode */
    uatomic_uint32_t timecode;

    /** last frame output */
    upipe_bmd_sink_frame *video_frame;

    /** current timing adjustment */
    int64_t timing_adjustment;

    /** is output acquired */
    bool acquired;

    /** is opened? */
    bool opened;
};

static int upipe_bmd_sink_check(struct upipe *upipe, struct uref *flow_format);

UPIPE_HELPER_UPIPE(upipe_bmd_sink, upipe, UPIPE_BMD_SINK_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_bmd_sink, urefcount, upipe_bmd_sink_free);
UPIPE_HELPER_UCLOCK(upipe_bmd_sink, uclock_external, uclock_external_request,
                    upipe_bmd_sink_check,
                    upipe_throw_provide_request, NULL);
UPIPE_HELPER_SYNC(upipe_bmd_sink, acquired);

UPIPE_HELPER_UPIPE(upipe_bmd_sink_sub, upipe, UPIPE_BMD_SINK_INPUT_SIGNATURE)
UPIPE_HELPER_UPUMP_MGR(upipe_bmd_sink_sub, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_bmd_sink_sub, upump, upump_mgr);
UPIPE_HELPER_FLOW(upipe_bmd_sink_sub, NULL);
UPIPE_HELPER_SUBPIPE(upipe_bmd_sink, upipe_bmd_sink_sub, input, sub_mgr, inputs, uchain)
UPIPE_HELPER_UREFCOUNT(upipe_bmd_sink_sub, urefcount, upipe_bmd_sink_sub_free);

UBASE_FROM_TO(upipe_bmd_sink, upipe_bmd_sink_sub, pic_subpipe, pic_subpipe)

UBASE_FROM_TO(upipe_bmd_sink, uclock, uclock, uclock)

static void uqueue_uref_flush(struct uqueue *uqueue)
{
    for (;;) {
        struct uref *uref = uqueue_pop(uqueue, struct uref *);
        if (!uref)
            break;
        uref_free(uref);
    }
}

static int upipe_bmd_open_vid(struct upipe *upipe);
static void output_cb(struct upipe *upipe, uint64_t pts);

class callback : public IDeckLinkVideoOutputCallback
{
public:
    virtual HRESULT ScheduledFrameCompleted (IDeckLinkVideoFrame *frame, BMDOutputFrameCompletionResult result) {
        if (uatomic_load(&upipe_bmd_sink->preroll))
            return S_OK;

        if (pts == 0) {
            /* preroll has ended, set up our counter */
            pts = ((upipe_bmd_sink_frame*)frame)->pts;
            pts += PREROLL_FRAMES * upipe_bmd_sink->ticks_per_frame;
        }

        static const char *Result_str[] = {
            "completed",
            "late",
            "dropped",
            "flushed",
            "?",
        };
        enum uprobe_log_level level;
        switch (result) {
            case 0:
                level = UPROBE_LOG_VERBOSE;
                break;
            case 1:
            case 2:
            case 3:
                level = UPROBE_LOG_WARNING;
                break;
            default:
                result = 4;
                level = UPROBE_LOG_ERROR;
                break;
        }
        upipe_log_va(&upipe_bmd_sink->upipe, level,
                     "%p Frame %s", frame, Result_str[result]);

#if 0

        BMDTimeValue val;
        if (upipe_bmd_sink->deckLinkOutput->GetFrameCompletionReferenceTimestamp(frame, UCLOCK_FREQ, &val) != S_OK)
            val = 0;

        uint64_t now = uclock_now(&upipe_bmd_sink->uclock);
        __sync_synchronize();
        uint64_t fpts = ((upipe_bmd_sink_frame*)frame)->pts;
        int64_t diff = now - fpts - upipe_bmd_sink->ticks_per_frame;

        upipe_notice_va(&upipe_bmd_sink->upipe,
                "%p Frame %s (%.2f ms) - delay %.2f ms", frame,
                Result_str[(result > 4) ? 4 : result], dur_to_time(1000 * (val - prev)), dur_to_time(1000 * diff));
        prev = val;
#endif

        /* next frame */
        output_cb(&upipe_bmd_sink->pic_subpipe.upipe, pts);
        pts += upipe_bmd_sink->ticks_per_frame;
        return S_OK;
    }

    virtual HRESULT ScheduledPlaybackHasStopped (void)
    {
        return S_OK;
    }

    callback(struct upipe_bmd_sink *upipe_bmd_sink_) {
        upipe_bmd_sink = upipe_bmd_sink_;
        uatomic_init(&refcount, 1);
        prev = 0;
    }

    ~callback(void) {
        uatomic_clean(&refcount);
    }

    virtual ULONG STDMETHODCALLTYPE AddRef(void) {
        return uatomic_fetch_add(&refcount, 1) + 1;
    }

    virtual ULONG STDMETHODCALLTYPE Release(void) {
        uint32_t new_ref = uatomic_fetch_sub(&refcount, 1) - 1;
        if (new_ref == 0)
            delete this;
        return new_ref;
    }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) {
        return E_NOINTERFACE;
    }

private:
    uatomic_uint32_t refcount;
    BMDTimeValue prev;
    struct upipe_bmd_sink *upipe_bmd_sink;

public:
    uint64_t pts;
};

#ifdef UPIPE_HAVE_LIBZVBI_H
/* VBI Teletext */
static void upipe_bmd_sink_extract_ttx(IDeckLinkVideoFrameAncillary *ancillary,
        const uint8_t *pic_data, size_t pic_data_size, int w, int sd,
        vbi_sampling_par *sp, uint16_t *ctr_array)
{
    const uint8_t *packet[2][OP47_PACKETS_PER_FIELD];
    int packets[2] = {0, 0};
    memset(packet, 0, sizeof(packet));

    if (pic_data[0] != DVBVBI_DATA_IDENTIFIER)
        return;

    pic_data++;
    pic_data_size--;

    static const unsigned dvb_unit_size = DVBVBI_UNIT_HEADER_SIZE + DVBVBI_LENGTH;
    for (; pic_data_size >= dvb_unit_size; pic_data += dvb_unit_size, pic_data_size -= dvb_unit_size) {
        uint8_t data_unit_id  = pic_data[0];
        uint8_t data_unit_len = pic_data[1];

        if (data_unit_id != DVBVBI_ID_TTX_SUB && data_unit_id != DVBVBI_ID_TTX_NONSUB)
            continue;

        if (data_unit_len != DVBVBI_LENGTH)
            continue;

        uint8_t line_offset = dvbvbittx_get_line(&pic_data[DVBVBI_UNIT_HEADER_SIZE]);

        uint8_t f2 = !dvbvbittx_get_field(&pic_data[DVBVBI_UNIT_HEADER_SIZE]);
        if (f2 == 0 && line_offset == 0) // line == 0
            continue;

        if (packets[f2] < (sd ? 1 : OP47_PACKETS_PER_FIELD))
            packet[f2][packets[f2]++] = pic_data;
    }

    for (int i = 0; i < 2; i++) {
        if (packets[i] == 0)
            continue;

        if (sd) {
            uint8_t buf[720*2];
            sdi_clear_vbi(buf, 720);

            int line = sdi_encode_ttx_sd(&buf[0], packet[i][0], sp);

            void *vanc;
            ancillary->GetBufferForVerticalBlankingLine(line, &vanc);
            sdi_encode_v210_sd((uint32_t*)vanc, buf, w);
        } else {
            uint16_t buf[VANC_WIDTH*2];

            upipe_sdi_blank_c(buf, VANC_WIDTH);

            /* +1 to destination buffer to write to luma plane */
            sdi_encode_ttx(&buf[1], packets[i], &packet[i][0], &ctr_array[i]);

            void *vanc;
            int line = OP47_LINE1 + 563*i;
            ancillary->GetBufferForVerticalBlankingLine(line, &vanc);
            sdi_encode_v210((uint32_t*)vanc, buf, w);
        }
    }
}
#endif

/** @internal @This initializes an subpipe of a bmd sink pipe.
 *
 * @param upipe pointer to subpipe
 * @param sub_mgr manager of the subpipe
 * @param uprobe structure used to raise events by the subpipe
 */
static void upipe_bmd_sink_sub_init(struct upipe *upipe,
        struct upipe_mgr *sub_mgr, struct uprobe *uprobe, bool static_pipe)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_sub_mgr(sub_mgr);

    if (static_pipe) {
        upipe_init(upipe, sub_mgr, uprobe);
        /* increment super pipe refcount only when the static pipes are retrieved */
        upipe_mgr_release(sub_mgr);
        upipe->refcount = &upipe_bmd_sink->urefcount;
    } else
        upipe_bmd_sink_sub_init_urefcount(upipe);

    struct upipe_bmd_sink_sub *upipe_bmd_sink_sub = upipe_bmd_sink_sub_from_upipe(upipe);
    upipe_bmd_sink_sub->upipe_bmd_sink = upipe_bmd_sink_to_upipe(upipe_bmd_sink);

    pthread_mutex_lock(&upipe_bmd_sink->lock);
    upipe_bmd_sink_sub_init_sub(upipe);

    static const uint8_t length = 255;
    upipe_bmd_sink_sub->uqueue_extra = malloc(uqueue_sizeof(length));
    assert(upipe_bmd_sink_sub->uqueue_extra);
    uqueue_init(&upipe_bmd_sink_sub->uqueue, length, upipe_bmd_sink_sub->uqueue_extra);

    upipe_bmd_sink_sub->uref = NULL;
    upipe_bmd_sink_sub->latency = 0;
    upipe_bmd_sink_sub_init_upump_mgr(upipe);
    upipe_bmd_sink_sub_init_upump(upipe);
    upipe_bmd_sink_sub->type = BMD_SUBPIPE_TYPE_UNKNOWN;
    upipe_bmd_sink_sub->dolby_e = false;
    upipe_bmd_sink_sub->s337 = false;
    upipe_bmd_sink_sub->channels = 0;

    upipe_throw_ready(upipe);
    pthread_mutex_unlock(&upipe_bmd_sink->lock);
}

static void upipe_bmd_sink_sub_free(struct upipe *upipe)
{
    struct upipe_bmd_sink_sub *upipe_bmd_sink_sub = upipe_bmd_sink_sub_from_upipe(upipe);
    struct upipe_bmd_sink *upipe_bmd_sink =
        upipe_bmd_sink_from_sub_mgr(upipe->mgr);

    pthread_mutex_lock(&upipe_bmd_sink->lock);
    upipe_throw_dead(upipe);

    upipe_bmd_sink_sub_clean_sub(upipe);
    pthread_mutex_unlock(&upipe_bmd_sink->lock);

    upipe_bmd_sink_sub_clean_upump(upipe);
    upipe_bmd_sink_sub_clean_upump_mgr(upipe);
    uref_free(upipe_bmd_sink_sub->uref);
    uqueue_uref_flush(&upipe_bmd_sink_sub->uqueue);
    uqueue_clean(&upipe_bmd_sink_sub->uqueue);
    free(upipe_bmd_sink_sub->uqueue_extra);

    if (upipe_bmd_sink_sub == &upipe_bmd_sink->pic_subpipe) {
        upipe_clean(upipe);
        return;
    }

    upipe_bmd_sink_sub_clean_urefcount(upipe);
    upipe_bmd_sink_sub_free_flow(upipe);
}

static void copy_samples(upipe_bmd_sink_sub *upipe_bmd_sink_sub,
        struct uref *uref, uint64_t samples)
{
    struct upipe *upipe = &upipe_bmd_sink_sub->upipe;
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_sub_mgr(upipe->mgr);
    uint8_t idx = upipe_bmd_sink_sub->channel_idx;
    int32_t *out = upipe_bmd_sink->audio_buf;

    uint64_t offset = 0;
    if (upipe_bmd_sink_sub->dolby_e) {
        if (upipe_bmd_sink->dolbye_offset >= samples) {
            upipe_err_va(upipe, "offsetting for dolbye would overflow audio: "
                "dolbye %hhu, %" PRIu64" samples",
                upipe_bmd_sink->dolbye_offset, samples);
        } else {
            offset   = upipe_bmd_sink->dolbye_offset;
            samples -= upipe_bmd_sink->dolbye_offset;
        }
    }

    const uint8_t c = upipe_bmd_sink_sub->channels;
    const int32_t *in = NULL;
    UBASE_FATAL_RETURN(upipe, uref_sound_read_int32_t(uref, 0, samples, &in, 1));
    for (int i = 0; i < samples; i++)
        memcpy(&out[DECKLINK_CHANNELS * (offset + i) + idx], &in[c*i], c * sizeof(int32_t));

    uref_sound_unmap(uref, 0, samples, 1);
}

/** @internal @This sets the bmd_sink timing adjustment.
 *
 * @param upipe description structure of the pipe
 * @param int64_t requested timing adjustment
 * @return an error code
 */
static int _upipe_bmd_sink_set_timing_adjustment(struct upipe *upipe, int64_t adj)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    IDeckLinkConfiguration *decklink_configuration;
    HRESULT result;

    result = upipe_bmd_sink->deckLink->QueryInterface(
        IID_IDeckLinkConfiguration, (void**)&decklink_configuration);
    if (result != S_OK)
        return UBASE_ERR_EXTERNAL;

    if (adj > 127)
        adj = 127;
    else if (adj < -127)
        adj = -127;

    result = decklink_configuration->SetInt(
        bmdDeckLinkConfigClockTimingAdjustment, adj);
    if (result != S_OK) {
        decklink_configuration->Release();
        return UBASE_ERR_EXTERNAL;
    }

    decklink_configuration->WriteConfigurationToPreferences();
    decklink_configuration->Release();

    return UBASE_ERR_NONE;
}

/** @internal @This adjusts the bmd_sink timing.
 *
 * @param upipe description structure of the pipe
 * @param int64_t requested timing adjustment
 * @return an error code
 */
static int _upipe_bmd_sink_adjust_timing(struct upipe *upipe, int64_t adj)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    IDeckLinkConfiguration *decklink_configuration;
    HRESULT result;

    result = upipe_bmd_sink->deckLink->QueryInterface(
        IID_IDeckLinkConfiguration, (void**)&decklink_configuration);
    if (result != S_OK)
        return UBASE_ERR_EXTERNAL;

    if (upipe_bmd_sink->timing_adjustment == INT64_MAX) {
        result = decklink_configuration->GetInt(
            bmdDeckLinkConfigClockTimingAdjustment,
            &upipe_bmd_sink->timing_adjustment);
        if (result != S_OK) {
            decklink_configuration->Release();
            return UBASE_ERR_EXTERNAL;
        }
        upipe_dbg_va(upipe, "current timing adjustment %" PRIi64,
                     upipe_bmd_sink->timing_adjustment);
    }

    adj += upipe_bmd_sink->timing_adjustment;
    if (adj > 127)
        adj = 127;
    else if (adj < -127)
        adj = -127;

    if (upipe_bmd_sink->timing_adjustment == adj)
        return UBASE_ERR_NONE;

    upipe_bmd_sink->timing_adjustment = adj;

    result = decklink_configuration->SetInt(
        bmdDeckLinkConfigClockTimingAdjustment,
        upipe_bmd_sink->timing_adjustment);
    if (result != S_OK) {
        decklink_configuration->Release();
        return UBASE_ERR_EXTERNAL;
    }

    decklink_configuration->WriteConfigurationToPreferences();
    decklink_configuration->Release();

    upipe_dbg_va(upipe, "adjust timing to %" PRIi64" ppm", adj);

    return UBASE_ERR_NONE;
}

static struct uref *upipe_bmd_sink_sub_pop(struct upipe *upipe, uint64_t date)
{
    struct upipe_bmd_sink_sub *upipe_bmd_sink_sub =
        upipe_bmd_sink_sub_from_upipe(upipe);
    struct upipe_bmd_sink *upipe_bmd_sink =
        upipe_bmd_sink_from_sub_mgr(upipe->mgr);
    struct uref *uref = NULL;

    if (upipe_bmd_sink->uclock_external) {
        if (date == UINT64_MAX)
            return NULL;

        uint64_t tolerance = upipe_bmd_sink->ticks_per_frame;

        while (true) {
                if (upipe_bmd_sink_sub->uref) {
                    uref = upipe_bmd_sink_sub->uref;
                    upipe_bmd_sink_sub->uref = NULL;
                }
                else
                    uref = uqueue_pop(
                        &upipe_bmd_sink_sub->uqueue, struct uref *);
                if (!uref)
                    break;

            uint64_t pts_sys = UINT64_MAX;
            uref_clock_get_pts_sys(uref, &pts_sys);
            if (unlikely(pts_sys == UINT64_MAX)) {
                upipe_warn(upipe, "drop undated buffer");
                uref_free(uref);
                continue;
            }
            pts_sys += upipe_bmd_sink_sub->latency;
            if (pts_sys + tolerance < date) {
                upipe_warn_va(upipe, "drop late buffer %.3f ms",
                              (date - pts_sys) * 1000. / UCLOCK_FREQ);
                uref_free(uref);
                continue;
            }
            else if (pts_sys > date + tolerance) {
                upipe_warn_va(upipe, "skip early  buffer %.3f ms",
                              (pts_sys - date) * 1000. / UCLOCK_FREQ);
                upipe_bmd_sink_sub->uref = uref;
                uref = NULL;
            }
            break;
        }
    }
    else {
        uref = uqueue_pop(&upipe_bmd_sink_sub->uqueue, struct uref *);
    }

    return uref;
}

/** @internal @This fills the audio samples for one single stereo pair
 */
static unsigned upipe_bmd_sink_sub_sound_get_samples_channel(struct upipe *upipe,
        const uint64_t video_pts, struct upipe_bmd_sink_sub *upipe_bmd_sink_sub)
{
    size_t samples;
    struct uref *uref = upipe_bmd_sink_sub_pop(
        upipe_bmd_sink_sub_to_upipe(upipe_bmd_sink_sub), video_pts);
    if (!uref) {
        upipe_warn(&upipe_bmd_sink_sub->upipe, "no audio");
        return 0;
    }

    if (!ubase_check(uref_sound_size(uref, &samples, NULL /* sample_size */))) {
        upipe_err(&upipe_bmd_sink_sub->upipe, "can't read sound size");
        uref_free(uref);
        return 0;
    }

    if (samples > max_samples) {
        upipe_err_va(&upipe_bmd_sink_sub->upipe, "too much samples (%zu)", samples);
        samples = max_samples;
    }

    /* read the samples into our final buffer */
    copy_samples(upipe_bmd_sink_sub, uref, samples);

    uref_free(uref);

    return samples;
}

/** @internal @This fills one video frame worth of audio samples
 */
static unsigned upipe_bmd_sink_sub_sound_get_samples(struct upipe *upipe,
        const uint64_t video_pts)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);

    /* Clear buffer */
    memset(upipe_bmd_sink->audio_buf, 0, audio_buf_size);

    unsigned samples = 0;

    /* iterate through input subpipes */
    pthread_mutex_lock(&upipe_bmd_sink->lock);
    struct uchain *uchain = NULL;
    ulist_foreach(&upipe_bmd_sink->inputs, uchain) {
        struct upipe_bmd_sink_sub *upipe_bmd_sink_sub =
            upipe_bmd_sink_sub_from_uchain(uchain);
        if (upipe_bmd_sink_sub->type != BMD_SUBPIPE_TYPE_SOUND)
            continue;

        unsigned s = upipe_bmd_sink_sub_sound_get_samples_channel(upipe, video_pts, upipe_bmd_sink_sub);
        if (samples < s)
            samples = s;
    }
    pthread_mutex_unlock(&upipe_bmd_sink->lock);

    return samples;
}

static upipe_bmd_sink_frame *get_video_frame(struct upipe *upipe,
    uint64_t pts, struct uref *uref)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    int w = upipe_bmd_sink->displayMode->GetWidth();
    int h = upipe_bmd_sink->displayMode->GetHeight();
    int sd = upipe_bmd_sink->mode == bmdModePAL || upipe_bmd_sink->mode == bmdModeNTSC;
#ifdef UPIPE_HAVE_LIBZVBI_H
    int ttx = upipe_bmd_sink->mode == bmdModePAL || upipe_bmd_sink->mode == bmdModeHD1080i50;
#endif

    if (!uref) {
        if (!upipe_bmd_sink->video_frame)
            return NULL;

        /* increase refcount before outputting this frame */
        ULONG ref = upipe_bmd_sink->video_frame->AddRef();
        upipe_warn_va(upipe, "reusing frame %p : %lu", upipe_bmd_sink->video_frame, ref);
        return upipe_bmd_sink->video_frame;
    }

    const char *v210 = "u10y10v10y10u10y10v10y10u10y10v10y10";
    size_t stride;
    const uint8_t *plane;
    if (unlikely(!ubase_check(uref_pic_plane_size(uref, v210, &stride,
                        NULL, NULL, NULL)) ||
                !ubase_check(uref_pic_plane_read(uref, v210, 0, 0, -1, -1,
                        &plane)))) {
        upipe_err_va(upipe, "Could not read v210 plane");
        return NULL;
    }
    upipe_bmd_sink_frame *video_frame = new upipe_bmd_sink_frame(uref,
            (void*)plane, w, h, stride, pts);
    if (!video_frame) {
        uref_free(uref);
        return NULL;
    }

    if (upipe_bmd_sink->video_frame)
        upipe_bmd_sink->video_frame->Release();
    upipe_bmd_sink->video_frame = NULL;

    IDeckLinkVideoFrameAncillary *ancillary = NULL;
    HRESULT res = upipe_bmd_sink->deckLinkOutput->CreateAncillaryData(video_frame->GetPixelFormat(), &ancillary);
    if (res != S_OK) {
        upipe_err(upipe, "Could not create ancillary data");
        delete video_frame;
        return NULL;
    }

    if (uatomic_load(&upipe_bmd_sink->cc)) {
        const uint8_t *pic_data = NULL;
        size_t pic_data_size = 0;
        uref_pic_get_cea_708(uref, &pic_data, &pic_data_size);
        int ntsc = upipe_bmd_sink->mode == bmdModeNTSC ||
            upipe_bmd_sink->mode == bmdModeHD1080i5994 ||
            upipe_bmd_sink->mode == bmdModeHD720p5994;

        if (ntsc && pic_data_size > 0) {
            /** XXX: Support crazy 25fps captions? **/
            const uint8_t fps = upipe_bmd_sink->mode == bmdModeNTSC ||
                upipe_bmd_sink->mode == bmdModeHD1080i5994 ? 0x4 : 0x7;
            void *vanc;
            ancillary->GetBufferForVerticalBlankingLine(CC_LINE, &vanc);
            uint16_t buf[VANC_WIDTH*2];
            upipe_sdi_blank_c(buf, VANC_WIDTH);
            /* +1 to write into the Y plane */
            sdi_write_cdp(pic_data, pic_data_size, &buf[1], upipe_bmd_sink->mode == bmdModeNTSC ? 1 : 2,
                    &upipe_bmd_sink->cdp_hdr_sequence_cntr, fps);
            sdi_calc_parity_checksum(&buf[1]);

            if (!sd)
                sdi_encode_v210((uint32_t*)vanc, buf, w);
        }
    }

    uint64_t vid_pts = 0;
    uref_clock_get_pts_sys(uref, &vid_pts);
    vid_pts += upipe_bmd_sink->pic_subpipe.latency;

#ifdef UPIPE_HAVE_LIBZVBI_H
    /* iterate through input subpipes */
    pthread_mutex_lock(&upipe_bmd_sink->lock);
    struct uchain *uchain = NULL;
    ulist_foreach(&upipe_bmd_sink->inputs, uchain) {
        struct upipe_bmd_sink_sub *upipe_bmd_sink_sub =
            upipe_bmd_sink_sub_from_uchain(uchain);
        if (upipe_bmd_sink_sub->type != BMD_SUBPIPE_TYPE_TTX)
            continue;

        struct upipe_bmd_sink_sub *subpic_sub = upipe_bmd_sink_sub_from_uchain(uchain);
        for (;;) {
            /* buffered uref if any */
            struct uref *subpic = subpic_sub->uref;
            if (subpic)
                subpic_sub->uref = NULL;
            else { /* thread-safe queue */
                subpic = uqueue_pop(&subpic_sub->uqueue, struct uref *);
                if (!subpic)
                    break;
            }

            if (!ttx) {
                uref_free(subpic);
                continue;
            }

            uint64_t subpic_pts = 0;
            uref_clock_get_pts_sys(subpic, &subpic_pts);
            subpic_pts += subpic_sub->latency;

            /* Delete old urefs */
            if (subpic_pts + (UCLOCK_FREQ/25) < vid_pts) {
                uref_free(subpic);
                continue;
            }

            /* Buffer if needed */
            if (subpic_pts - (UCLOCK_FREQ/25) > vid_pts) {
                subpic_sub->uref = subpic;
                break;
            }

            if (!uatomic_load(&upipe_bmd_sink->ttx)) {
                uref_free(subpic);
                break;
            }

            /* Choose the closest subpic in the past */
            uint8_t *buf = upipe_bmd_sink->op47_ttx_buf;
            size_t size = -1;
            uref_block_size(subpic, &size);
            if (size > DVBVBI_LENGTH * OP47_PACKETS_PER_FIELD * 2)
                size = DVBVBI_LENGTH * OP47_PACKETS_PER_FIELD * 2;

            if (ubase_check(uref_block_extract(subpic, 0, size, buf))) {
                upipe_bmd_sink_extract_ttx(ancillary, buf, size, w, sd,
                        &upipe_bmd_sink->sp,
                        &upipe_bmd_sink->op47_sequence_counter[0]);
            }
        }
    }
    pthread_mutex_unlock(&upipe_bmd_sink->lock);
#endif

    if (uatomic_load(&upipe_bmd_sink->timecode)) {
        const uint8_t *tc_data;
        size_t tc_data_size;
        // bmdVideoOutputRP188
        if (ubase_check(uref_pic_get_s12m(uref, &tc_data, &tc_data_size))
                && uref_attr_s12m_check(tc_data, tc_data_size)) {
            upipe_bmd_sink_timecode timecode(uref_attr_s12m_read(tc_data + sizeof(uint32_t)));
            video_frame->SetTimecode(timecode);
        }
    }

    video_frame->SetAncillaryData(ancillary);
    video_frame->AddRef(); // we're gonna buffer this frame
    upipe_bmd_sink->video_frame = video_frame;

    return video_frame;
}

static void schedule_frame(struct upipe *upipe, struct uref *uref, uint64_t pts)
{
    HRESULT result;
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_sub_mgr(upipe->mgr);

    upipe_bmd_sink_frame *video_frame = get_video_frame(&upipe_bmd_sink->upipe, pts, uref);
    if (!video_frame)
        return;

    result = upipe_bmd_sink->deckLinkOutput->ScheduleVideoFrame(video_frame, pts, upipe_bmd_sink->ticks_per_frame, UCLOCK_FREQ);
    video_frame->Release();

    if( result != S_OK )
        upipe_err_va(upipe, "DROPPED FRAME %x", result);

    /* audio */
    uint64_t pts_sys = UINT64_MAX;
    if (uref)
        uref_clock_get_pts_sys(uref, &pts_sys);
    unsigned samples = upipe_bmd_sink_sub_sound_get_samples(&upipe_bmd_sink->upipe, pts_sys);

    uint32_t written;
    result = upipe_bmd_sink->deckLinkOutput->ScheduleAudioSamples(
            upipe_bmd_sink->audio_buf, samples, pts,
            UCLOCK_FREQ, &written);
    if( result != S_OK ) {
        upipe_err_va(upipe, "DROPPED AUDIO: %x", result);
        written = 0;
    }
    if (written != samples)
        upipe_dbg_va(upipe, "written %u/%u", written, samples);

    /* debug */

    uint32_t buffered;
    upipe_bmd_sink->deckLinkOutput->GetBufferedAudioSampleFrameCount(&buffered);

#if 0
    if (buffered == 0) {
        /* TODO: get notified as soon as audio buffers empty */
        upipe_bmd_sink->deckLinkOutput->StopScheduledPlayback(0, NULL, 0);
    }
#endif

    uint32_t vbuffered;
    upipe_bmd_sink->deckLinkOutput->GetBufferedVideoFrameCount(&vbuffered);
    if (0) upipe_dbg_va(upipe, "A %.2f | V %.2f",
            (float)(1000 * buffered) / 48000, (float) 1000 * vbuffered / 25);
}

static void output_cb(struct upipe *upipe, uint64_t pts)
{
    struct upipe_bmd_sink_sub *upipe_bmd_sink_sub =
        upipe_bmd_sink_sub_from_upipe(upipe);
    struct upipe_bmd_sink *upipe_bmd_sink =
        upipe_bmd_sink_from_sub_mgr(upipe->mgr);

    uint64_t now;
    if (upipe_bmd_sink->uclock_external)
        now = uclock_now(upipe_bmd_sink->uclock_external);
    else
        now = uclock_now(&upipe_bmd_sink->uclock);
    if (0) upipe_notice_va(upipe, "PTS %.2f - %.2f - %u pics",
            pts_to_time(pts),
            dur_to_time(now - pts),
            uqueue_length(&upipe_bmd_sink_sub->uqueue));

    /* Find a picture */
    schedule_frame(upipe, upipe_bmd_sink_sub_pop(upipe, now), pts);

    /* Restart playback 4s after genlock transition */
    if (upipe_bmd_sink->genlock_transition_time) {
        if (now > upipe_bmd_sink->genlock_transition_time + 4 * UCLOCK_FREQ) {
            upipe_warn(upipe, "restarting playback after genlock synchronization");
            upipe_bmd_sink->genlock_transition_time = 0;
            upipe_bmd_sink->deckLinkOutput->StopScheduledPlayback(0, NULL, 0);
            upipe_bmd_sink->deckLinkOutput->StartScheduledPlayback(pts, UCLOCK_FREQ, 1.0);
        }
    }

    int genlock_status = upipe_bmd_sink->genlock_status;
    upipe_bmd_sink_get_genlock_status(&upipe_bmd_sink->upipe, &upipe_bmd_sink->genlock_status);
    if (genlock_status == UPIPE_BMD_SINK_GENLOCK_UNLOCKED) {
        if (upipe_bmd_sink->genlock_status == UPIPE_BMD_SINK_GENLOCK_LOCKED) {
            upipe_warn(upipe, "genlock synchronized");
            upipe_bmd_sink->genlock_transition_time = uclock_now(&upipe_bmd_sink->uclock);
        }
    }
}

/** @internal @This handles input uref.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 */
static bool upipe_bmd_sink_sub_output(struct upipe *upipe, struct uref *uref)
{
    struct upipe_bmd_sink *upipe_bmd_sink =
        upipe_bmd_sink_from_sub_mgr(upipe->mgr);
    struct upipe_bmd_sink_sub *upipe_bmd_sink_sub =
        upipe_bmd_sink_sub_from_upipe(upipe);

    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_bmd_sink_sub->latency = 0;

        uref_clock_get_latency(uref, &upipe_bmd_sink_sub->latency);
        upipe_dbg_va(upipe, "latency %" PRIu64, upipe_bmd_sink_sub->latency);

        upipe_bmd_sink_sub->s337 = !ubase_ncmp(def, "sound.s32.s337.");
        upipe_bmd_sink_sub->dolby_e = upipe_bmd_sink_sub->s337 &&
            !ubase_ncmp(def, "sound.s32.s337.dolbye.");

        upipe_bmd_sink_sub_check_upump_mgr(upipe);

        uref_free(uref);
        return true;
    }

    /* output is controlled by the pic subpipe */
    if (upipe_bmd_sink_sub != &upipe_bmd_sink->pic_subpipe)
        return false;

    /* preroll is done, buffer and let the callback do the rest */
    if (!uatomic_load(&upipe_bmd_sink->preroll))
        return false;

    uint64_t pts = upipe_bmd_sink->start_pts;
    if (unlikely(!pts)) {
        /* First PTS is set to the first picture PTS */
        if (unlikely(!ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
            upipe_err(upipe, "Could not read pts");
            uref_free(uref);
            return true;
        }
        pts += upipe_bmd_sink_sub->latency;
        upipe_bmd_sink->start_pts = pts;
    }

    uint64_t now;
    /* use external clock if set, hardware clock otherwise */
    if (upipe_bmd_sink->uclock_external)
        now = uclock_now(upipe_bmd_sink->uclock_external);
    else
        now = uclock_now(&upipe_bmd_sink->uclock);

    /* next PTS */
    pts += (PREROLL_FRAMES - uatomic_load(&upipe_bmd_sink->preroll)) *
        upipe_bmd_sink->ticks_per_frame;

    if (now < upipe_bmd_sink->start_pts) {
        upipe_notice_va(upipe, "%.2f < %.2f, buffering",
            pts_to_time(now), pts_to_time(upipe_bmd_sink->start_pts));
        return false;
    }

    /* We're done buffering and now prerolling,
     * push the uref we just got into the fifo and
     * get the first one we buffered */
    if (!uqueue_push(&upipe_bmd_sink_sub->uqueue, uref)) {
        upipe_err_va(upipe, "Buffer is full");
        uref_free(uref);
    }
    uref = uqueue_pop(&upipe_bmd_sink_sub->uqueue, struct uref *);
    if (!uref) {
        upipe_err_va(upipe, "Buffer is empty");
    }

    upipe_notice_va(upipe, "PREROLLING %.2f", pts_to_time(pts));
    schedule_frame(upipe, uref, pts);

    if (uatomic_fetch_sub(&upipe_bmd_sink->preroll, 1) == 1) {
        upipe_notice(upipe, "Starting playback");
        if (upipe_bmd_sink->deckLinkOutput->EndAudioPreroll() != S_OK)
            upipe_err_va(upipe, "End preroll failed");
        upipe_bmd_sink->deckLinkOutput->StartScheduledPlayback(upipe_bmd_sink->start_pts, UCLOCK_FREQ, 1.0);
    }

    return true;
}

/** @internal @This handles output data.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_bmd_sink_sub_input(struct upipe *upipe, struct uref *uref,
                                      struct upump **upump_p)
{
    struct upipe_bmd_sink *upipe_bmd_sink =
        upipe_bmd_sink_from_sub_mgr(upipe->mgr);
    struct upipe_bmd_sink_sub *upipe_bmd_sink_sub =
        upipe_bmd_sink_sub_from_upipe(upipe);

    if (!upipe_bmd_sink->deckLink) {
        upipe_err_va(upipe, "DeckLink card not ready");
        uref_free(uref);
        return;
    }

    if (!upipe_bmd_sink_sub_output(upipe, uref))
        if (!uqueue_push(&upipe_bmd_sink_sub->uqueue, uref)) {
            upipe_err(upipe, "Couldn't queue uref");
            uref_free(uref);
        }
}

uint32_t upipe_bmd_mode_from_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    IDeckLinkOutput *deckLinkOutput = upipe_bmd_sink->deckLinkOutput;
    char *displayModeName = NULL;
    uint32_t bmdMode = bmdModeUnknown;

    if (!deckLinkOutput) {
        upipe_err(upipe, "Card not opened yet");
        return bmdModeUnknown;
    }

    uint64_t hsize, vsize;
    struct urational fps;
    if (unlikely(!ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) ||
                !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize)) ||
                !ubase_check(uref_pic_flow_get_fps(flow_def, &fps)))) {
        upipe_err(upipe, "cannot read size and frame rate");
        uref_dump(flow_def, upipe->uprobe);
        return bmdModeUnknown;
    }

    bool interlaced = !ubase_check(uref_pic_get_progressive(flow_def));

    upipe_notice_va(upipe, "%" PRIu64"x%" PRIu64" %" PRId64"/%" PRIu64" interlaced %d",
            hsize, vsize, fps.num, fps.den, interlaced);

    IDeckLinkDisplayModeIterator *displayModeIterator = NULL;
    HRESULT result = deckLinkOutput->GetDisplayModeIterator(&displayModeIterator);
    if (result != S_OK){
        upipe_err(upipe, "decklink card has no display modes");
        return bmdModeUnknown;
    }

    IDeckLinkDisplayMode *mode = NULL;
    while ((result = displayModeIterator->Next(&mode)) == S_OK) {
        BMDFieldDominance field;
        BMDTimeValue timeValue;
        BMDTimeScale timeScale;
        struct urational bmd_fps;

        if (mode->GetWidth() != hsize)
            goto next;

        if (mode->GetHeight() != vsize)
            goto next;

        mode->GetFrameRate(&timeValue, &timeScale);
        bmd_fps.num = timeScale;
        bmd_fps.den = timeValue;

        if (urational_cmp(&fps, &bmd_fps))
            goto next;

        field = mode->GetFieldDominance();
        if (field == bmdUnknownFieldDominance) {
            upipe_err(upipe, "unknown field dominance");
        } else if (field == bmdLowerFieldFirst || field == bmdUpperFieldFirst) {
            if (!interlaced) {
                goto next;
            }
        } else {
            if (interlaced) {
                goto next;
            }
        }
        break;
next:
        mode->Release();
    }

    if (result != S_OK || !mode)
        goto end;

    if (mode->GetName((const char**)&displayModeName) == S_OK) {
        upipe_dbg_va(upipe, "Flow def is mode %s", displayModeName);
        free(displayModeName);
    }
    bmdMode = mode->GetDisplayMode();

end:
    if (mode)
        mode->Release();

    displayModeIterator->Release();

    return bmdMode;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_bmd_sink_sub_set_flow_def(struct upipe *upipe,
                                          struct uref *flow_def)
{
    struct upipe_bmd_sink *upipe_bmd_sink =
        upipe_bmd_sink_from_sub_mgr(upipe->mgr);
    struct upipe *super = upipe_bmd_sink_to_upipe(upipe_bmd_sink);
    struct upipe_bmd_sink_sub *upipe_bmd_sink_sub =
        upipe_bmd_sink_sub_from_upipe(upipe);

    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    uint64_t latency;
    if (ubase_check(uref_clock_get_latency(flow_def, &latency))) {
        if (/*upipe_bmd_sink_sub->latency && */latency != upipe_bmd_sink_sub->latency) {
            upipe_dbg_va(upipe, "latency %" PRIu64" -> %" PRIu64,
                    upipe_bmd_sink_sub->latency, latency);
            upipe_bmd_sink_sub->latency = latency;
        }
    }

    const char *def;
    if (!ubase_check(uref_flow_get_def(flow_def, &def)))
        return UBASE_ERR_INVALID;

    if (upipe_bmd_sink_sub == &upipe_bmd_sink->pic_subpipe) {
        upipe_bmd_sink_sync_lost(super);

        uint8_t macropixel;
        if (!ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel))) {
            upipe_err(upipe, "macropixel size not set");
            uref_dump(flow_def, upipe->uprobe);
            return UBASE_ERR_EXTERNAL;
        }

        if (macropixel != 6 || !ubase_check(
                             uref_pic_flow_check_chroma(flow_def, 1, 1, 16,
                                                        "u10y10v10y10u10y10v10y10u10y10v10y10"))) {
            upipe_err(upipe, "incompatible input flow def");
            uref_dump(flow_def, upipe->uprobe);
            return UBASE_ERR_EXTERNAL;
        }

        BMDDisplayMode bmdMode =
            upipe_bmd_mode_from_flow_def(&upipe_bmd_sink->upipe, flow_def);
        if (bmdMode == bmdModeUnknown) {
            upipe_err(upipe, "input flow def is not supported");
            return UBASE_ERR_INVALID;
        }
        if (upipe_bmd_sink->selectedMode != bmdModeUnknown &&
            bmdMode != upipe_bmd_sink->selectedMode) {
            upipe_warn(upipe, "incompatible input flow def for selected mode");
            return UBASE_ERR_INVALID;
        }
        if (bmdMode != upipe_bmd_sink->mode) {
            upipe_notice(upipe, "Changing output configuration");
            upipe_bmd_sink->mode = bmdMode;
            UBASE_RETURN(upipe_bmd_open_vid(super));
        }

        struct dolbye_offset {
            BMDDisplayMode mode;
            uint8_t offset;
        };

        static const struct dolbye_offset table[2][2] = {
            /* All others */
            {
                {
                    bmdModeHD1080i50, 33,
                },
                {
                    bmdModeHD1080i5994, 31,
                },
            },

            /* SDI (including Duo) */
            {
                {
                    bmdModeHD1080i50, 54,
                },
                {
                    bmdModeHD1080i5994, 48,
                },
            },

        };

        const struct dolbye_offset *t = &table[0][0];
        if (upipe_bmd_sink->modelName && !strcmp(upipe_bmd_sink->modelName, "DeckLink SDI")) {
            t = &table[1][0];
        }

        const size_t n = sizeof(table) / 2 / sizeof(**table);

        for (size_t i = 0; i < n; i++) {
            const struct dolbye_offset *e = &t[i];
            if (e->mode == bmdMode) {
                upipe_bmd_sink->dolbye_offset = e->offset;
                break;
            }
        }

        upipe_bmd_sink->frame_idx = 0;
        upipe_bmd_sink_sync_acquired(super);
    }
    else if (!ubase_ncmp(def, "sound.")) {
        if (!ubase_check(uref_sound_flow_get_channels(flow_def, &upipe_bmd_sink_sub->channels))) {
            upipe_err(upipe, "Could not read number of channels");
            return UBASE_ERR_INVALID;
        }

        if (upipe_bmd_sink_sub->channels > 2) {
            upipe_err_va(upipe, "Too many audio channels %u", upipe_bmd_sink_sub->channels);
            return UBASE_ERR_INVALID;
        }
    }

    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an output subpipe of an
 * bmd_sink pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_bmd_sink_sub_control(struct upipe *upipe,
                                     int command, va_list args)
{
    UBASE_HANDLED_RETURN(
        upipe_bmd_sink_sub_control_super(upipe, command, args));
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            upipe_bmd_sink_sub_set_upump(upipe, NULL);
            UBASE_RETURN(upipe_bmd_sink_sub_attach_upump_mgr(upipe))
            return UBASE_ERR_NONE;
        }
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_throw_provide_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST:
            return UBASE_ERR_NONE;
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref *);
            return upipe_bmd_sink_sub_set_flow_def(upipe, flow_def);
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}

static struct upipe *upipe_bmd_sink_sub_alloc(struct upipe_mgr *mgr,
                                                 struct uprobe *uprobe,
                                                 uint32_t signature, va_list args)
{
    struct uref *flow_def = NULL;
    struct upipe *upipe = upipe_bmd_sink_sub_alloc_flow(mgr,
            uprobe, signature, args, &flow_def);
    struct upipe_bmd_sink_sub *upipe_bmd_sink_sub = upipe_bmd_sink_sub_from_upipe(upipe);

    if (unlikely(upipe == NULL || flow_def == NULL))
        goto error;

    const char *def;
    if (!ubase_check(uref_flow_get_def(flow_def, &def)))
        goto error;

    if (!ubase_ncmp(def, "sound.")) {
        uint8_t channel_idx;
        if (!ubase_check(uref_bmd_sink_get_channel(flow_def, &channel_idx))) {
            upipe_err(upipe, "Could not read channel_idx");
            uref_dump(flow_def, uprobe);
            goto error;
        }
        if (channel_idx >= DECKLINK_CHANNELS) {
            upipe_err_va(upipe, "channel_idx %hhu not in range", channel_idx);
            goto error;
        }

        upipe_bmd_sink_sub_init(upipe, mgr, uprobe, false);
        upipe_bmd_sink_sub->type = BMD_SUBPIPE_TYPE_SOUND;
        upipe_bmd_sink_sub->channel_idx = channel_idx;
    }
    else if (!ubase_ncmp(def, "block.dvb_teletext.")) {
        upipe_bmd_sink_sub_init(upipe, mgr, uprobe, false);
        upipe_bmd_sink_sub->type = BMD_SUBPIPE_TYPE_TTX;
    }
    else {
        goto error;
    }

    /* different subpipe type */
    uref_dump(flow_def, uprobe);
    uref_free(flow_def);

    return upipe;

error:
    uref_free(flow_def);
    if (upipe) {
        upipe_clean(upipe);
        free(upipe_bmd_sink_sub);
    }
    return NULL;
}

/** @internal @This initializes the output manager for an bmd_sink pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_sink_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_bmd_sink->sub_mgr;
    sub_mgr->refcount = upipe_bmd_sink_to_urefcount(upipe_bmd_sink);
    sub_mgr->signature = UPIPE_BMD_SINK_INPUT_SIGNATURE;
    sub_mgr->upipe_alloc = upipe_bmd_sink_sub_alloc;
    sub_mgr->upipe_input = upipe_bmd_sink_sub_input;
    sub_mgr->upipe_control = upipe_bmd_sink_sub_control;
}

/** @This returns the Blackmagic hardware output time.
 *
 * @param uclock utility structure passed to the module
 * @return current hardware output time in 27 MHz ticks
 */
static uint64_t uclock_bmd_sink_now(struct uclock *uclock)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_uclock(uclock);
    struct upipe *upipe = &upipe_bmd_sink->upipe;

    BMDTimeValue hardware_time = UINT64_MAX, time_in_frame, ticks_per_frame;

    if (!upipe_bmd_sink->deckLinkOutput) {
        upipe_err_va(upipe, "No output configured");
        return UINT64_MAX;
    }


    HRESULT res = upipe_bmd_sink->deckLinkOutput->GetHardwareReferenceClock(
            UCLOCK_FREQ, &hardware_time, &time_in_frame, &ticks_per_frame);
    if (res != S_OK) {
        upipe_err_va(upipe, "\t\tCouldn't read hardware clock: 0x%08x", res);
        hardware_time = 0;
    }

    hardware_time += upipe_bmd_sink->offset;

    if (0) upipe_notice_va(upipe, "CLOCK THR 0x%llx VAL %" PRIu64,
        (unsigned long long)pthread_self(), (uint64_t)hardware_time);

    return (uint64_t)hardware_time;
}

/** @internal @This allocates a bmd_sink pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_bmd_sink_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature, va_list args)
{
    if (signature != UPIPE_BMD_SINK_SIGNATURE)
        return NULL;
    struct uprobe *uprobe_pic = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_subpic = va_arg(args, struct uprobe *);

    struct upipe_bmd_sink *upipe_bmd_sink =
        (struct upipe_bmd_sink *)calloc(1, sizeof(struct upipe_bmd_sink));
    if (unlikely(upipe_bmd_sink == NULL)) {
        uprobe_release(uprobe_pic);
        uprobe_release(uprobe_subpic);
        return NULL;
    }

    struct upipe *upipe = upipe_bmd_sink_to_upipe(upipe_bmd_sink);
    upipe_init(upipe, mgr, uprobe);

    upipe_bmd_sink_init_sub_inputs(upipe);
    upipe_bmd_sink_init_sub_mgr(upipe);
    upipe_bmd_sink_init_urefcount(upipe);
    upipe_bmd_sink_init_uclock(upipe);
    upipe_bmd_sink_init_sync(upipe);

    pthread_mutex_init(&upipe_bmd_sink->lock, NULL);

    /* Initialise subpipes */
    upipe_bmd_sink_sub_init(upipe_bmd_sink_sub_to_upipe(upipe_bmd_sink_to_pic_subpipe(upipe_bmd_sink)),
                            &upipe_bmd_sink->sub_mgr, uprobe_pic, true);

    upipe_bmd_sink->audio_buf = (int32_t*)malloc(audio_buf_size);

    upipe_bmd_sink->uclock.refcount = upipe->refcount;
    upipe_bmd_sink->uclock.uclock_now = uclock_bmd_sink_now;
    upipe_bmd_sink->card_idx = -1;
    upipe_bmd_sink->card_topo = -1;
    upipe_bmd_sink->opened = false;
    upipe_bmd_sink->mode = bmdModeUnknown;
    upipe_bmd_sink->selectedMode = bmdModeUnknown;
    upipe_bmd_sink->timing_adjustment = INT64_MAX;
    uatomic_init(&upipe_bmd_sink->preroll, PREROLL_FRAMES);
    uatomic_init(&upipe_bmd_sink->cc, 0);
    uatomic_init(&upipe_bmd_sink->ttx, 0);

    upipe_throw_ready(upipe);
    return upipe;
}

static void upipe_bmd_stop(struct upipe *upipe)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    IDeckLinkOutput *deckLinkOutput = upipe_bmd_sink->deckLinkOutput;

    upipe_bmd_sink->start_pts = 0;

    uatomic_store(&upipe_bmd_sink->preroll, PREROLL_FRAMES);
    upipe_bmd_sink->cb->pts = 0; /* callback is not running anymore */
    __sync_synchronize();

    deckLinkOutput->StopScheduledPlayback(0, NULL, 0);
    deckLinkOutput->DisableAudioOutput();
    /* bump clock upwards before it's made unavailable by DisableVideoOutput */
    if (upipe_bmd_sink->opened)
        upipe_bmd_sink->offset = uclock_now(&upipe_bmd_sink->uclock);
    deckLinkOutput->DisableVideoOutput();

    struct uchain *uchain = NULL;
    ulist_foreach(&upipe_bmd_sink->inputs, uchain) {
        struct upipe_bmd_sink_sub *upipe_bmd_sink_sub =
            upipe_bmd_sink_sub_from_uchain(uchain);
        uqueue_uref_flush(&upipe_bmd_sink_sub->uqueue);
    }

    if (upipe_bmd_sink->displayMode) {
        upipe_bmd_sink->displayMode->Release();
        upipe_bmd_sink->displayMode = NULL;
    }

    if (upipe_bmd_sink->video_frame) {
        upipe_bmd_sink->video_frame->Release();
        upipe_bmd_sink->video_frame = NULL;
    }

    upipe_bmd_sink->opened = false;
    upipe_bmd_sink_sync_lost(upipe);
}

static int upipe_bmd_open_vid(struct upipe *upipe)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    IDeckLinkOutput *deckLinkOutput = upipe_bmd_sink->deckLinkOutput;
    IDeckLinkDisplayModeIterator *displayModeIterator = NULL;
    char* displayModeName = NULL;
    IDeckLinkDisplayMode* displayMode = NULL;
    int err = UBASE_ERR_NONE;
    HRESULT result = E_NOINTERFACE;

    upipe_bmd_stop(upipe);

    result = deckLinkOutput->GetDisplayModeIterator(&displayModeIterator);
    if (result != S_OK){
        upipe_err_va(upipe, "decklink card has no display modes");
        err = UBASE_ERR_EXTERNAL;
        goto end;
    }

    while ((result = displayModeIterator->Next(&displayMode)) == S_OK)
    {
        if (displayMode->GetDisplayMode() == upipe_bmd_sink->mode)
            break;

        displayMode->Release();
    }

    if (result != S_OK || displayMode == NULL)
    {
        uint32_t mode = htonl(upipe_bmd_sink->mode);
        upipe_err_va(upipe, "Unable to get display mode %4.4s\n", (char*)&mode);
        err = UBASE_ERR_EXTERNAL;
        goto end;
    }

    result = displayMode->GetName((const char**)&displayModeName);
    if (result == S_OK)
    {
        upipe_dbg_va(upipe, "Using mode %s", displayModeName);
        free(displayModeName);
    }

    upipe_bmd_sink->displayMode = displayMode;

    BMDTimeValue timeValue;
    BMDTimeScale timeScale;
    displayMode->GetFrameRate(&timeValue, &timeScale);
    upipe_bmd_sink->ticks_per_frame = UCLOCK_FREQ * timeValue / timeScale;

    /* TODO: use timecode option to set bit */
    result = deckLinkOutput->EnableVideoOutput(displayMode->GetDisplayMode(),
            bmdVideoOutputVANC|bmdVideoOutputVITC|bmdVideoOutputRP188);
    if (result != S_OK)
    {
        upipe_err(upipe, "Failed to enable video output. Is another application using the card?\n");
        err = UBASE_ERR_EXTERNAL;
        goto end;
    }

    result = deckLinkOutput->EnableAudioOutput(48000, bmdAudioSampleType32bitInteger,
            DECKLINK_CHANNELS, bmdAudioOutputStreamTimestamped);
    if (result != S_OK)
    {
        upipe_err(upipe, "Failed to enable audio output. Is another application using the card?\n");
        err = UBASE_ERR_EXTERNAL;
        goto end;
    }

    if (deckLinkOutput->BeginAudioPreroll() != S_OK)
        upipe_err(upipe, "Could not begin audio preroll");

    upipe_bmd_sink->genlock_status = -1;
    upipe_bmd_sink->genlock_transition_time = 0;

#ifdef UPIPE_HAVE_LIBZVBI_H
    if (upipe_bmd_sink->mode == bmdModePAL) {
        upipe_bmd_sink->sp.scanning         = 625; /* PAL */
        upipe_bmd_sink->sp.sampling_format  = VBI_PIXFMT_YUV420;
        upipe_bmd_sink->sp.sampling_rate    = 13.5e6;
        upipe_bmd_sink->sp.bytes_per_line   = 720;
        upipe_bmd_sink->sp.start[0]     = 6;
        upipe_bmd_sink->sp.count[0]     = 17;
        upipe_bmd_sink->sp.start[1]     = 319;
        upipe_bmd_sink->sp.count[1]     = 17;
        upipe_bmd_sink->sp.interlaced   = FALSE;
        upipe_bmd_sink->sp.synchronous  = FALSE;
        upipe_bmd_sink->sp.offset       = 128;
    } else if (upipe_bmd_sink->mode == bmdModeNTSC) {
        upipe_bmd_sink->sp.scanning         = 525; /* NTSC */
        upipe_bmd_sink->sp.sampling_format  = VBI_PIXFMT_YUV420;
        upipe_bmd_sink->sp.sampling_rate    = 13.5e6;
        upipe_bmd_sink->sp.bytes_per_line   = 720;
        upipe_bmd_sink->sp.interlaced   = FALSE;
        upipe_bmd_sink->sp.synchronous  = TRUE;
    }
#endif

    upipe_bmd_sink->opened = true;

end:
    if (displayModeIterator != NULL)
        displayModeIterator->Release();

    return err;
}

/** @internal @This asks to open the given device.
 *
 * @param upipe description structure of the pipe
 * @param uri URI
 * @return an error code
 */
static int upipe_bmd_sink_open_card(struct upipe *upipe)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);

    int err = UBASE_ERR_NONE;
    HRESULT result = E_NOINTERFACE;

    assert(!upipe_bmd_sink->deckLink);

    /* decklink interface iterator */
    IDeckLinkIterator *deckLinkIterator = CreateDeckLinkIteratorInstance();
    if (!deckLinkIterator) {
        upipe_err_va(upipe, "decklink drivers not found");
        return UBASE_ERR_EXTERNAL;
    }

    /* get decklink interface handler */
    IDeckLink *deckLink = NULL;

    if (upipe_bmd_sink->card_topo >= 0) {
        for ( ; ; ) {
            if (deckLink)
                deckLink->Release();
            result = deckLinkIterator->Next(&deckLink);
            if (result != S_OK)
                break;

            IDeckLinkAttributes *deckLinkAttributes = NULL;
            if (deckLink->QueryInterface(IID_IDeckLinkAttributes,
                                         (void**)&deckLinkAttributes) == S_OK) {
                int64_t deckLinkTopologicalId = 0;
                HRESULT result =
                    deckLinkAttributes->GetInt(BMDDeckLinkTopologicalID,
                            &deckLinkTopologicalId);
                deckLinkAttributes->Release();
                if (result == S_OK &&
                    (uint64_t)deckLinkTopologicalId == upipe_bmd_sink->card_topo)
                    break;
            }
        }
    }
    else if (upipe_bmd_sink->card_idx >= 0) {
        for (int i = 0; i <= upipe_bmd_sink->card_idx; i++) {
            if (deckLink)
                deckLink->Release();
            result = deckLinkIterator->Next(&deckLink);
            if (result != S_OK)
                break;
        }
    }

    if (result != S_OK) {
        upipe_err_va(upipe, "decklink card %d not found", upipe_bmd_sink->card_idx);
        err = UBASE_ERR_EXTERNAL;
        if (deckLink)
            deckLink->Release();
        goto end;
    }

    if (deckLink->GetModelName(&upipe_bmd_sink->modelName) != S_OK) {
        upipe_err(upipe, "Could not read card model name");
    }

    if (deckLink->QueryInterface(IID_IDeckLinkOutput,
                                 (void**)&upipe_bmd_sink->deckLinkOutput) != S_OK) {
        upipe_err_va(upipe, "decklink card has no output");
        err = UBASE_ERR_EXTERNAL;
        deckLink->Release();
        goto end;
    }

    upipe_bmd_sink->cb = new callback(upipe_bmd_sink);
    if (upipe_bmd_sink->deckLinkOutput->SetScheduledFrameCompletionCallback(
                upipe_bmd_sink->cb) != S_OK)
        upipe_err(upipe, "Could not set callback");

    upipe_bmd_sink->deckLink = deckLink;

end:

    deckLinkIterator->Release();

    return err;
}

/** @internal @This sets the content of a bmd_sink option.
 *
 * @param upipe description structure of the pipe
 * @param option name of the option
 * @param content content of the option, or NULL to delete it
 * @return an error code
 */
static int upipe_bmd_sink_set_option(struct upipe *upipe,
                                   const char *k, const char *v)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    assert(k != NULL);

    if (!strcmp(k, "card-index"))
        upipe_bmd_sink->card_idx = atoi(v);
    else if (!strcmp(k, "card-topology"))
        upipe_bmd_sink->card_topo = strtoll(v, NULL, 10);
    else if (!strcmp(k, "mode")) {
        if (!v || strlen(v) != 4)
            return UBASE_ERR_INVALID;
        union {
            BMDDisplayMode mode_id;
            char mode_s[4];
        } u;
        memcpy(u.mode_s, v, sizeof(u.mode_s));
        upipe_bmd_sink->selectedMode = htonl(u.mode_id);
    } else if (!strcmp(k, "cc")) {
        uatomic_store(&upipe_bmd_sink->cc, strcmp(v, "0"));
    } else if (!strcmp(k, "teletext")) {
        uatomic_store(&upipe_bmd_sink->ttx, strcmp(v, "0"));
    } else if (!strcmp(k, "timecode")) {
        uatomic_store(&upipe_bmd_sink->timecode, strcmp(v, "0"));
    } else
        return UBASE_ERR_INVALID;

    return UBASE_ERR_NONE;
}

/** @internal @This returns the bmd_sink genlock status.
 *
 * @param upipe description structure of the pipe
 * @param pointer to integer for genlock status
 * @return an error code
 */
static int _upipe_bmd_sink_get_genlock_status(struct upipe *upipe, int *status)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    BMDReferenceStatus reference_status;

    if (!upipe_bmd_sink->deckLinkOutput) {
        upipe_err_va(upipe, "No output configured");
        return UBASE_ERR_INVALID;
    }

    HRESULT result = upipe_bmd_sink->deckLinkOutput->GetReferenceStatus(&reference_status);
    if (result != S_OK)
        return UBASE_ERR_EXTERNAL;

    if (reference_status & bmdReferenceNotSupportedByHardware) {
        *status = UPIPE_BMD_SINK_GENLOCK_UNSUPPORTED;
        return UBASE_ERR_NONE;
    }

    if (reference_status & bmdReferenceLocked) {
        *status = UPIPE_BMD_SINK_GENLOCK_LOCKED;
        return UBASE_ERR_NONE;
    }

    *status = UPIPE_BMD_SINK_GENLOCK_UNLOCKED;
    return UBASE_ERR_NONE;
}

/** @internal @This returns the bmd_sink genlock offset.
 *
 * @param upipe description structure of the pipe
 * @param pointer to int64_t for genlock offset
 * @return an error code
 */
static int _upipe_bmd_sink_get_genlock_offset(struct upipe *upipe, int64_t *offset)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    BMDReferenceStatus reference_status;
    IDeckLinkConfiguration *decklink_configuration;
    HRESULT result;

    if (!upipe_bmd_sink->deckLinkOutput) {
        upipe_err_va(upipe, "No output configured");
        return UBASE_ERR_INVALID;
    }

    result = upipe_bmd_sink->deckLinkOutput->GetReferenceStatus(&reference_status);
    if (result != S_OK)
        return UBASE_ERR_EXTERNAL;
    if ((reference_status & bmdReferenceNotSupportedByHardware) ||
        !(reference_status & bmdReferenceLocked)) {
        *offset = 0;
        return UBASE_ERR_EXTERNAL;
    }

    result = upipe_bmd_sink->deckLink->QueryInterface(IID_IDeckLinkConfiguration, (void**)&decklink_configuration);
    if (result != S_OK) {
        *offset = 0;
        return UBASE_ERR_EXTERNAL;
    }

    result = decklink_configuration->GetInt(bmdDeckLinkConfigReferenceInputTimingOffset, offset);
    if (result != S_OK) {
        *offset = 0;
        decklink_configuration->Release();
        return UBASE_ERR_EXTERNAL;
    }
    decklink_configuration->Release();

    return UBASE_ERR_NONE;
}

/** @internal @This sets the bmd_sink genlock offset.
 *
 * @param upipe description structure of the pipe
 * @param int64_t requested genlock offset
 * @return an error code
 */
static int _upipe_bmd_sink_set_genlock_offset(struct upipe *upipe, int64_t offset)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);
    BMDReferenceStatus reference_status;
    IDeckLinkConfiguration *decklink_configuration;
    HRESULT result;

    if (!upipe_bmd_sink->deckLinkOutput) {
        upipe_err_va(upipe, "No output configured");
        return UBASE_ERR_INVALID;
    }

    result = upipe_bmd_sink->deckLinkOutput->GetReferenceStatus(&reference_status);
    if (result != S_OK)
        return UBASE_ERR_EXTERNAL;

    if ((reference_status & bmdReferenceNotSupportedByHardware)) {
        return UBASE_ERR_EXTERNAL;
    }

    result = upipe_bmd_sink->deckLink->QueryInterface(IID_IDeckLinkConfiguration, (void**)&decklink_configuration);
    if (result != S_OK) {
        return UBASE_ERR_EXTERNAL;
    }

    result = decklink_configuration->SetInt(bmdDeckLinkConfigReferenceInputTimingOffset, offset);
    if (result != S_OK) {
        decklink_configuration->Release();
        return UBASE_ERR_EXTERNAL;
    }

    decklink_configuration->WriteConfigurationToPreferences();
    decklink_configuration->Release();

    return UBASE_ERR_NONE;
}

/** @internal @This checks the internal pipe state.
 *
 * @param upipe description structure of the pipe
 * @param flow_format amended flow format
 * @return an error code
 */
static int upipe_bmd_sink_check(struct upipe *upipe, struct uref *flow_format)
{
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on an bmd_sink source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_bmd_sink_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_bmd_sink *bmd_sink = upipe_bmd_sink_from_upipe(upipe);

    UBASE_HANDLED_RETURN(upipe_bmd_sink_control_inputs(upipe, command, args));
    switch (command) {
        case UPIPE_ATTACH_UCLOCK:
            upipe_bmd_sink_require_uclock(upipe);
            return UBASE_ERR_NONE;

        case UPIPE_SET_URI:
            if (!bmd_sink->deckLink) {
                UBASE_RETURN(upipe_bmd_sink_open_card(upipe));
            }
            return UBASE_ERR_NONE;

        case UPIPE_BMD_SINK_GET_PIC_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SINK_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p =  upipe_bmd_sink_sub_to_upipe(
                            upipe_bmd_sink_to_pic_subpipe(
                                upipe_bmd_sink_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_BMD_SINK_GET_UCLOCK: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SINK_SIGNATURE)
            struct uclock **pp_uclock = va_arg(args, struct uclock **);
            *pp_uclock = &bmd_sink->uclock;
            return UBASE_ERR_NONE;
        }
        case UPIPE_BMD_SINK_GET_GENLOCK_STATUS: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SINK_SIGNATURE)
            int *status = va_arg(args, int *);
            return _upipe_bmd_sink_get_genlock_status(upipe, status);
        }
        case UPIPE_BMD_SINK_GET_GENLOCK_OFFSET: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SINK_SIGNATURE)
            int64_t *offset = va_arg(args, int64_t *);
            return _upipe_bmd_sink_get_genlock_offset(upipe, offset);
        }
        case UPIPE_BMD_SINK_SET_GENLOCK_OFFSET: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SINK_SIGNATURE)
            int64_t offset = va_arg(args, int64_t);
            return _upipe_bmd_sink_set_genlock_offset(upipe, offset);
        }
        case UPIPE_BMD_SINK_SET_TIMING_ADJUSTMENT: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SINK_SIGNATURE)
            int64_t timing_adj = va_arg(args, int64_t);
            return _upipe_bmd_sink_set_timing_adjustment(upipe, timing_adj);
        }
        case UPIPE_BMD_SINK_ADJUST_TIMING: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SINK_SIGNATURE)
            int64_t adj = va_arg(args, int64_t);
            return _upipe_bmd_sink_adjust_timing(upipe, adj);
        }
        case UPIPE_SET_OPTION: {
            const char *k = va_arg(args, const char *);
            const char *v = va_arg(args, const char *);
            return upipe_bmd_sink_set_option(upipe, k, v);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees all resources allocated.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_sink_free(struct upipe *upipe)
{
    struct upipe_bmd_sink *upipe_bmd_sink = upipe_bmd_sink_from_upipe(upipe);

    if (upipe_bmd_sink->deckLink)
        upipe_bmd_stop(upipe);

    upipe_bmd_sink_sub_free(upipe_bmd_sink_sub_to_upipe(&upipe_bmd_sink->pic_subpipe));
    upipe_dbg_va(upipe, "releasing blackmagic sink pipe %p", upipe);

    upipe_throw_dead(upipe);

    free(upipe_bmd_sink->audio_buf);

    if (upipe_bmd_sink->deckLink) {
        free((void*)upipe_bmd_sink->modelName);
        upipe_bmd_sink->deckLinkOutput->Release();
        upipe_bmd_sink->deckLink->Release();
    }

    pthread_mutex_destroy(&upipe_bmd_sink->lock);

    if (upipe_bmd_sink->cb)
        upipe_bmd_sink->cb->Release();

    upipe_bmd_sink_clean_sub_inputs(upipe);
    upipe_bmd_sink_clean_sync(upipe);
    upipe_bmd_sink_clean_uclock(upipe);
    upipe_bmd_sink_clean_urefcount(upipe);
    upipe_clean(upipe);
    free(upipe_bmd_sink);
}

/** upipe_bmd_sink (/dev/bmd_sink) */
static struct upipe_mgr upipe_bmd_sink_mgr = {
    /* .refcount = */ NULL,
    /* .signature = */ UPIPE_BMD_SINK_SIGNATURE,

    /* .upipe_err_str = */ NULL,
    /* .upipe_command_str = */ NULL,
    /* .upipe_event_str = */ NULL,

    /* .upipe_alloc = */ upipe_bmd_sink_alloc,
    /* .upipe_input = */ NULL,
    /* .upipe_control = */ upipe_bmd_sink_control,

    /* .upipe_mgr_control = */ NULL
};

/** @This returns the management structure for bmd_sink pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_bmd_sink_mgr_alloc(void)
{
    return &upipe_bmd_sink_mgr;
}
