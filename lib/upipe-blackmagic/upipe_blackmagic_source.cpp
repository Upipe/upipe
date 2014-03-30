/*
 * Copyright (C) 2013-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
 *          Christophe Massiot
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
 * @short Upipe source module for BlackMagic Design SDI cards
 */

#define __STDC_LIMIT_MACROS    1
#define __STDC_FORMAT_MACROS   1
#define __STDC_CONSTANT_MACROS 1

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/ufifo.h>
#include <upipe/uqueue.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-blackmagic/upipe_blackmagic_source.h>
#include <upipe-blackmagic/ubuf_pic_blackmagic.h>
#include <upipe-blackmagic/ubuf_sound_blackmagic.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include "include/DeckLinkAPI.h"
#include "include/DeckLinkAPIDispatch.cpp"

/** uqueue length */
#define MAX_QUEUE_LENGTH 255
/** ubuf pool depth */
#define UBUF_POOL_DEPTH 25
/** lowest possible prog PTS (just an arbitrarily high time) */
#define BMD_CLOCK_MIN UINT32_MAX
/** fixed sample rate FIXME */
#define BMD_SAMPLERATE 48000

/** @internal @This is the class that retrieves frames in a private thread. */
class DeckLinkCaptureDelegate : public IDeckLinkInputCallback
{
public:
    DeckLinkCaptureDelegate(struct upipe *_upipe) : upipe(_upipe) {
        uatomic_store(&refcount, 1);
    }

    ~DeckLinkCaptureDelegate(void) {
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

    virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
                BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*,
                BMDDetectedVideoInputFormatFlags);

    virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
                IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*);

private:
    uatomic_uint32_t refcount;

    struct upipe *upipe;
};

/** @internal packet type */
enum upipe_bmd_src_type {
    /** packet for pic subpipe */
    UPIPE_BMD_SRC_PIC,
    /** packet for sound subpipe */
    UPIPE_BMD_SRC_SOUND,
    /** packet for subpic subpipe */
    UPIPE_BMD_SRC_SUBPIC
};

/** @internal @This is the private context of an output of a bmdsrc pipe */
struct upipe_bmd_src_output {
    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_bmd_src_output, upipe, UPIPE_BMD_SRC_OUTPUT_SIGNATURE)
UPIPE_HELPER_OUTPUT(upipe_bmd_src_output, output, flow_def, flow_def_sent)
UPIPE_HELPER_UBUF_MGR(upipe_bmd_src_output, ubuf_mgr, flow_def)

/** @internal @This is the private context of a http source pipe. */
struct upipe_bmd_src {
    /** refcount management structure */
    struct urefcount urefcount;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** pump */
    struct upump *upump;

    /** uref manager */
    struct uref_mgr *uref_mgr;
    /** uclock structure */
    struct uclock *uclock;

    /** subpipe manager */
    struct upipe_mgr sub_mgr;
    /** pic subpipe */
    struct upipe_bmd_src_output pic_subpipe;
    /** sound subpipe */
    struct upipe_bmd_src_output sound_subpipe;
    /** subpic subpipe */
    struct upipe_bmd_src_output subpic_subpipe;

    /** URI */
    char *uri;
    /** card index */
    int card_idx;
    /** queue between blackmagic thread and pipe thread */
    struct uqueue uqueue;
    /** handle to decklink card */
    IDeckLink *deckLink;
    /** handle to decklink card input */
    IDeckLinkInput *deckLinkInput;
    /** handle to decklink delegate */
    DeckLinkCaptureDelegate *deckLinkCaptureDelegate;
    /** true for progressive frames - for use by the private thread */
    bool progressive;
    /** true for top field first - for use by the private thread */
    bool tff;

    /** public upipe structure */
    struct upipe upipe;

    /** extra data for the queue structures */
    uint8_t uqueue_extra[];
};

UPIPE_HELPER_UPIPE(upipe_bmd_src, upipe, UPIPE_BMD_SRC_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_bmd_src, urefcount, upipe_bmd_src_free)
UPIPE_HELPER_UREF_MGR(upipe_bmd_src, uref_mgr)
UPIPE_HELPER_UCLOCK(upipe_bmd_src, uclock)
UPIPE_HELPER_UPUMP_MGR(upipe_bmd_src, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_bmd_src, upump, upump_mgr)

UBASE_FROM_TO(upipe_bmd_src, upipe_mgr, sub_mgr, sub_mgr)
UBASE_FROM_TO(upipe_bmd_src, upipe_bmd_src_output, pic_subpipe, pic_subpipe)
UBASE_FROM_TO(upipe_bmd_src, upipe_bmd_src_output, sound_subpipe, sound_subpipe)
UBASE_FROM_TO(upipe_bmd_src, upipe_bmd_src_output, subpic_subpipe, subpic_subpipe)

HRESULT DeckLinkCaptureDelegate::VideoInputFormatChanged(
                BMDVideoInputFormatChangedEvents events,
                IDeckLinkDisplayMode *mode, BMDDetectedVideoInputFormatFlags)
{
    if (!(events & bmdVideoInputDisplayModeChanged))
        return S_OK;

    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    upipe_bmd_src->deckLinkInput->PauseStreams();
    upipe_bmd_src->deckLinkInput->EnableVideoInput(mode->GetDisplayMode(),
            bmdFormat8BitYUV, bmdVideoInputEnableFormatDetection);
    upipe_bmd_src->deckLinkInput->FlushStreams();
    upipe_bmd_src->deckLinkInput->StartStreams();

    struct uref *flow_def_video =
        uref_dup(upipe_bmd_src->pic_subpipe.flow_def);

    uref_pic_flow_set_hsize(flow_def_video, mode->GetWidth());
    uref_pic_flow_set_vsize(flow_def_video, mode->GetHeight());

    struct urational fps;
    BMDTimeValue frameDuration;
    BMDTimeScale timeScale;
    mode->GetFrameRate(&frameDuration, &timeScale);
    fps.num = timeScale;
    fps.den = frameDuration;
    urational_simplify(&fps);
    uref_pic_flow_set_fps(flow_def_video, fps);

    BMDFieldDominance field = mode->GetFieldDominance();
    switch (field) {
        case bmdLowerFieldFirst:
            uref_pic_delete_tff(flow_def_video);
            uref_pic_delete_progressive(flow_def_video);
            upipe_bmd_src->tff = false;
            upipe_bmd_src->progressive = false;
            break;
        default:
        case bmdUnknownFieldDominance:
            /* sensible defaults */
        case bmdUpperFieldFirst:
            uref_pic_set_tff(flow_def_video);
            uref_pic_delete_progressive(flow_def_video);
            upipe_bmd_src->tff = true;
            upipe_bmd_src->progressive = false;
            break;
        case bmdProgressiveFrame:
        case bmdProgressiveSegmentedFrame:
            uref_pic_delete_tff(flow_def_video);
            uref_pic_set_progressive(flow_def_video);
            upipe_bmd_src->tff = false;
            upipe_bmd_src->progressive = true;
            break;
    }

    if (unlikely(!uqueue_push(&upipe_bmd_src->uqueue, flow_def_video)))
        uref_free(flow_def_video);
    return S_OK;
}

/** @internal @This is called when receiving a video or audio frame.
 *
 * @param VideoFrame video frame
 * @param AudioPacket audio packet
 * @return S_OK
 */
HRESULT DeckLinkCaptureDelegate::VideoInputFrameArrived(
                        IDeckLinkVideoInputFrame* VideoFrame,
                        IDeckLinkAudioInputPacket* AudioPacket)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    uint64_t cr_sys = UINT64_MAX;
    if (upipe_bmd_src->uclock)
        cr_sys = uclock_now(upipe_bmd_src->uclock);

    if (VideoFrame && !(VideoFrame->GetFlags() & bmdFrameHasNoInputSource)) {
        struct ubuf *ubuf =
            ubuf_pic_bmd_alloc(upipe_bmd_src->pic_subpipe.ubuf_mgr, VideoFrame);
        if (likely(ubuf != NULL)) {
            /* TODO subpic */
            struct uref *uref = uref_alloc(upipe_bmd_src->uref_mgr);
            uref_attach_ubuf(uref, ubuf);
            uref_attr_set_priv(uref, UPIPE_BMD_SRC_PIC);

            if (cr_sys != UINT64_MAX)
                uref_clock_set_cr_sys(uref, cr_sys);
            BMDTimeValue FrameTime, FrameDuration;
            VideoFrame->GetStreamTime(&FrameTime, &FrameDuration, UCLOCK_FREQ);
            uref_clock_set_pts_orig(uref, FrameTime);
            uref_clock_set_pts_prog(uref, FrameTime + BMD_CLOCK_MIN);
            uref_clock_set_dts_pts_delay(uref, 0);
            uref_clock_set_duration(uref, FrameDuration);

            if (upipe_bmd_src->progressive)
                uref_pic_set_progressive(uref);
            else if (upipe_bmd_src->tff)
                uref_pic_set_tff(uref);

            if (!uqueue_push(&upipe_bmd_src->uqueue, uref))
                uref_free(uref);
        }
    }

    if (AudioPacket) {
        struct ubuf *ubuf =
            ubuf_sound_bmd_alloc(upipe_bmd_src->sound_subpipe.ubuf_mgr,
                                 AudioPacket);
        if (likely(ubuf != NULL)) {
            struct uref *uref = uref_alloc(upipe_bmd_src->uref_mgr);
            uref_attach_ubuf(uref, ubuf);
            uref_attr_set_priv(uref, UPIPE_BMD_SRC_SOUND);

            if (cr_sys != UINT64_MAX)
                uref_clock_set_cr_sys(uref, cr_sys);
            BMDTimeValue PacketTime;
            AudioPacket->GetPacketTime(&PacketTime, UCLOCK_FREQ);
            uref_clock_set_pts_orig(uref, PacketTime);
            uref_clock_set_pts_prog(uref, PacketTime + BMD_CLOCK_MIN);
            uref_clock_set_dts_pts_delay(uref, 0);
            uref_clock_set_duration(uref, AudioPacket->GetSampleFrameCount() *
                                          UCLOCK_FREQ / BMD_SAMPLERATE);

            if (!uqueue_push(&upipe_bmd_src->uqueue, uref))
                uref_free(uref);
        }
    }
    return S_OK;
}

/** @internal @This initializes an output subpipe of a bmd source pipe.
 *
 * @param upipe pointer to subpipe
 * @param sub_mgr manager of the subpipe
 * @param uprobe structure used to raise events by the subpipe
 */
static void upipe_bmd_src_output_init(struct upipe *upipe,
        struct upipe_mgr *sub_mgr, struct uprobe *uprobe)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_sub_mgr(sub_mgr);
    upipe_init(upipe, sub_mgr, uprobe);
    upipe->refcount = &upipe_bmd_src->urefcount;

    upipe_bmd_src_output_init_ubuf_mgr(upipe);
    upipe_bmd_src_output_init_output(upipe);

    upipe_throw_ready(upipe);
}

/** @internal @This processes control commands on a blackmagic output pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_bmd_src_output_control(struct upipe *upipe,
                                                   int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UBUF_MGR: {
            return upipe_bmd_src_output_attach_ubuf_mgr(upipe);
        }

        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_bmd_src_output_get_flow_def(upipe, p);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_bmd_src_output_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_bmd_src_output_set_output(upipe, output);
        }
        case UPIPE_SUB_GET_SUPER: {
            struct upipe **p = va_arg(args, struct upipe **);
            *p = upipe_bmd_src_to_upipe(upipe_bmd_src_from_sub_mgr(upipe->mgr));
            return UBASE_ERR_NONE;
        }

        default:
            return UBASE_ERR_UNHANDLED;
    }
}


/** @This clean up an output subpipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_src_output_clean(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_bmd_src_output_clean_output(upipe);
    upipe_bmd_src_output_clean_ubuf_mgr(upipe);

    upipe_clean(upipe);
}

/** @internal @This initializes the output manager for a blackmagic pipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_src_init_sub_mgr(struct upipe *upipe)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    struct upipe_mgr *sub_mgr = &upipe_bmd_src->sub_mgr;
    sub_mgr->refcount = NULL;
    sub_mgr->signature = UPIPE_BMD_SRC_OUTPUT_SIGNATURE;
    sub_mgr->upipe_alloc = NULL;
    sub_mgr->upipe_input = NULL;
    sub_mgr->upipe_control = upipe_bmd_src_output_control;
    sub_mgr->upipe_mgr_control = NULL;
}

/** @internal @This allocates a bmd source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_bmd_src_alloc(struct upipe_mgr *mgr,
                                         struct uprobe *uprobe,
                                         uint32_t signature, va_list args)
{
    if (signature != UPIPE_BMD_SRC_SIGNATURE)
        return NULL;
    struct uprobe *uprobe_pic = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_sound = va_arg(args, struct uprobe *);
    struct uprobe *uprobe_subpic = va_arg(args, struct uprobe *);

    struct upipe_bmd_src *upipe_bmd_src =
        (struct upipe_bmd_src *)malloc(sizeof(struct upipe_bmd_src) +
                                       uqueue_sizeof(MAX_QUEUE_LENGTH));
    if (unlikely(upipe_bmd_src == NULL)) {
        uprobe_release(uprobe_pic);
        uprobe_release(uprobe_sound);
        uprobe_release(uprobe_subpic);
        return NULL;
    }

    struct upipe *upipe = upipe_bmd_src_to_upipe(upipe_bmd_src);
    upipe_init(upipe, mgr, uprobe);
    upipe_bmd_src_output_init(upipe_bmd_src_output_to_upipe(
                                upipe_bmd_src_to_pic_subpipe(upipe_bmd_src)),
                              &upipe_bmd_src->sub_mgr, uprobe_pic);
    upipe_bmd_src_output_init(upipe_bmd_src_output_to_upipe(
                                upipe_bmd_src_to_sound_subpipe(upipe_bmd_src)),
                              &upipe_bmd_src->sub_mgr, uprobe_sound);
    upipe_bmd_src_output_init(upipe_bmd_src_output_to_upipe(
                                upipe_bmd_src_to_subpic_subpipe(upipe_bmd_src)),
                              &upipe_bmd_src->sub_mgr, uprobe_subpic);

    upipe_bmd_src_init_urefcount(upipe);
    upipe_bmd_src_init_uref_mgr(upipe);
    upipe_bmd_src_init_uclock(upipe);
    upipe_bmd_src_init_upump_mgr(upipe);
    upipe_bmd_src_init_upump(upipe);
    upipe_bmd_src_init_sub_mgr(upipe);

    uqueue_init(&upipe_bmd_src->uqueue, MAX_QUEUE_LENGTH,
                upipe_bmd_src->uqueue_extra);
    upipe_bmd_src->deckLink = NULL;
    upipe_bmd_src->deckLinkInput = NULL;
    upipe_bmd_src->deckLinkCaptureDelegate = NULL;
    upipe_bmd_src->progressive = false;
    upipe_bmd_src->tff = true;

    upipe_throw_ready(upipe);
    return upipe;
}

/** @internal @This reads data from the queue and outputs it.
 *
 * @param upipe description structure of the pipe
 * @param upump description structure of the pump
 */
void upipe_bmd_src_work(struct upipe *upipe, struct upump *upump)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    struct uref *uref;

    /* unqueue urefs */
    while ((uref = uqueue_pop(&upipe_bmd_src->uqueue, struct uref *))) {
        uint64_t type;
        struct upipe *subpipe;
        if (unlikely(!ubase_check(uref_attr_get_priv(uref, &type)))) {
            upipe_throw_error(upipe, UBASE_ERR_UNKNOWN);
            uref_free(uref);
            continue;
        }
        uref_attr_delete_priv(uref);

        switch (type) {
            case UPIPE_BMD_SRC_PIC:
                subpipe = upipe_bmd_src_output_to_upipe(
                        upipe_bmd_src_to_pic_subpipe(upipe_bmd_src));
                break;
            case UPIPE_BMD_SRC_SOUND:
                subpipe = upipe_bmd_src_output_to_upipe(
                        upipe_bmd_src_to_sound_subpipe(upipe_bmd_src));
                break;
            case UPIPE_BMD_SRC_SUBPIC:
                subpipe = upipe_bmd_src_output_to_upipe(
                        upipe_bmd_src_to_subpic_subpipe(upipe_bmd_src));
                break;
            default:
                upipe_throw_error(upipe, UBASE_ERR_UNKNOWN);
                uref_free(uref);
                continue;
        }

        const char *def;
        if (unlikely(uref_flow_get_def(uref, &def))) {
            upipe_bmd_src_output_store_flow_def(subpipe, uref);
            continue;
        }

        if (type == UPIPE_BMD_SRC_PIC) {
            uint64_t cr_sys;
            if (likely(ubase_check(uref_clock_get_cr_sys(uref, &cr_sys))))
                upipe_throw_clock_ref(subpipe, uref, cr_sys, 0);
        }
        upipe_throw_clock_ts(subpipe, uref);
        upipe_bmd_src_output_output(subpipe, uref, &upipe_bmd_src->upump);
    }
}

/** @internal @This reads data from the source and outputs it.
 * It is called upon receiving new data from the card.
 *
 * @param upump description structure of the read watcher
 */
static void upipe_bmd_src_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_bmd_src_work(upipe, upump);
}

/** @internal @This returns the currently opened device.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the currently opened device
 * @return an error code
 */
static int upipe_bmd_src_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_bmd_src->uri;
    return UBASE_ERR_NONE;
}

/** @internal @This asks to open the given device.
 *
 * @param upipe description structure of the pipe
 * @param uri URI
 * @return an error code
 */
static int upipe_bmd_src_set_uri(struct upipe *upipe, const char *uri)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);

    if (unlikely(upipe_bmd_src->uri != NULL)) {
        upipe_err(upipe, "unable to reopen device");
        return UBASE_ERR_INVALID;
    }

    if (unlikely(uri == NULL))
        return UBASE_ERR_NONE;

    UBASE_RETURN(upipe_bmd_src_check_uref_mgr(upipe))
    upipe_bmd_src_check_upump_mgr(upipe);

    upipe_bmd_src->uri = strdup(uri);
    upipe_bmd_src->card_idx = atoi(uri);
    upipe_notice_va(upipe, "opening device %s", upipe_bmd_src->uri);

    IDeckLinkIterator *deckLinkIterator;
    IDeckLink *deckLink;

    deckLinkIterator = CreateDeckLinkIteratorInstance();
    if (!deckLinkIterator) {
        upipe_err(upipe, "decklink drivers not found");
        return UBASE_ERR_EXTERNAL;
    }

    /* get decklink input handler */
    HRESULT result = E_NOINTERFACE;
    for (int i = 0; i <= upipe_bmd_src->card_idx; i++) {
        if (deckLink)
            deckLink->Release();
        result = deckLinkIterator->Next(&deckLink);
        if (result != S_OK)
            break;
    }
    deckLinkIterator->Release();

    if (result != S_OK) {
        upipe_err_va(upipe, "decklink card %d not found",
                     upipe_bmd_src->card_idx);
        if (deckLink)
            deckLink->Release();
        return UBASE_ERR_EXTERNAL;
    }

    const char *model_name;
    if (deckLink->GetModelName(&model_name) == S_OK)
        upipe_notice_va(upipe, "detected card type %s", model_name);

    IDeckLinkInput *deckLinkInput;
    if (deckLink->QueryInterface(IID_IDeckLinkInput,
                                 (void**)&deckLinkInput) != S_OK) {
        upipe_err(upipe, "decklink card has no input");
        deckLink->Release();
        return UBASE_ERR_EXTERNAL;
    }

#if 0
    IDeckLinkConfiguration *deckLinkConfiguration;
    if (deckLink->QueryInterface(IID_IDeckLinkConfiguration,
                                 (void**)&deckLinkConfiguration) != S_OK) {
        upipe_err(upipe, "decklink card has no configuration");
        deckLinkInput->Release();
        deckLink->Release();
        return UBASE_ERR_EXTERNAL;
    }

    /* TODO set pic and sound connection */
#endif

    /* configure input */
    /* FIXME hardcoded format and default mode */
    deckLinkInput->EnableVideoInput(bmdModePAL, bmdFormat8BitYUV,
                                    bmdVideoInputEnableFormatDetection);
    deckLinkInput->EnableAudioInput(bmdAudioSampleRate48kHz,
                                    bmdAudioSampleType16bitInteger, 2);

    /* managers */
    upipe_bmd_src->pic_subpipe.ubuf_mgr =
        ubuf_pic_bmd_mgr_alloc(UBUF_POOL_DEPTH, bmdFormat8BitYUV);
    upipe_bmd_src->sound_subpipe.ubuf_mgr =
        ubuf_sound_bmd_mgr_alloc(UBUF_POOL_DEPTH,
                                 bmdAudioSampleType16bitInteger, 2, "lr");
    /* TODO subpic */
    if (unlikely(upipe_bmd_src->pic_subpipe.ubuf_mgr == NULL ||
                 upipe_bmd_src->sound_subpipe.ubuf_mgr == NULL)) {
        if (upipe_bmd_src->pic_subpipe.ubuf_mgr)
            ubuf_mgr_release(upipe_bmd_src->pic_subpipe.ubuf_mgr);
        upipe_bmd_src->pic_subpipe.ubuf_mgr = NULL;
        deckLinkInput->Release();
        deckLink->Release();
        return UBASE_ERR_ALLOC;
    }

    /* flow definitions */
    struct uref *flow_def =
        uref_pic_flow_alloc_def(upipe_bmd_src->uref_mgr, 1);
    uref_pic_flow_add_plane(flow_def, 1, 1, 4, "u8y8v8y8");
    upipe_bmd_src_output_store_flow_def(
            upipe_bmd_src_output_to_upipe(
                upipe_bmd_src_to_pic_subpipe(upipe_bmd_src)),
            flow_def);
    flow_def = uref_sound_flow_alloc_def(upipe_bmd_src->uref_mgr, "s16.", 2, 2);
    uref_sound_flow_add_plane(flow_def, "lr");
    upipe_bmd_src_output_store_flow_def(
            upipe_bmd_src_output_to_upipe(
                upipe_bmd_src_to_sound_subpipe(upipe_bmd_src)),
            flow_def);
    /* TODO subpic */

    upipe_bmd_src->deckLink = deckLink;
    upipe_bmd_src->deckLinkInput = deckLinkInput;
    /* callback helper */
    upipe_bmd_src->deckLinkCaptureDelegate = new DeckLinkCaptureDelegate(upipe);
    deckLinkInput->SetCallback(upipe_bmd_src->deckLinkCaptureDelegate);

    if (deckLinkInput->StartStreams() != S_OK) {
        upipe_err(upipe, "decklink card doesn't start");
        return UBASE_ERR_EXTERNAL;
    }

    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a blackmagic source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int _upipe_bmd_src_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UREF_MGR: {
            return upipe_bmd_src_attach_uref_mgr(upipe);
        }
        case UPIPE_ATTACH_UCLOCK: {
            return upipe_bmd_src_attach_uclock(upipe);
        }
        case UPIPE_ATTACH_UPUMP_MGR: {
            upipe_bmd_src_set_upump(upipe, NULL);
            return upipe_bmd_src_attach_upump_mgr(upipe);
        }
        case UPIPE_GET_URI: {
            const char **uri_p = va_arg(args, const char **);
            return upipe_bmd_src_get_uri(upipe, uri_p);
        }
        case UPIPE_SET_URI: {
            const char *uri = va_arg(args, const char *);
            return upipe_bmd_src_set_uri(upipe, uri);
        }

        case UPIPE_BMD_SRC_GET_PIC_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SRC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_bmd_src_output_to_upipe(
                    upipe_bmd_src_to_pic_subpipe(
                        upipe_bmd_src_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_BMD_SRC_GET_SOUND_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SRC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_bmd_src_output_to_upipe(
                    upipe_bmd_src_to_sound_subpipe(
                        upipe_bmd_src_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        case UPIPE_BMD_SRC_GET_SUBPIC_SUB: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_BMD_SRC_SIGNATURE)
            struct upipe **upipe_p = va_arg(args, struct upipe **);
            *upipe_p = upipe_bmd_src_output_to_upipe(
                    upipe_bmd_src_to_subpic_subpipe(
                        upipe_bmd_src_from_upipe(upipe)));
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This processes control commands on a bmd source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_bmd_src_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_RETURN(_upipe_bmd_src_control(upipe, command, args));
    upipe_bmd_src_check_upump_mgr(upipe);

    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);
    if (upipe_bmd_src->upump_mgr != NULL && upipe_bmd_src->uri != NULL &&
        upipe_bmd_src->upump == NULL) {
        struct upump *upump =
            uqueue_upump_alloc_pop(&upipe_bmd_src->uqueue,
                                   upipe_bmd_src->upump_mgr,
                                   upipe_bmd_src_worker, upipe);
        if (unlikely(upump == NULL)) {
            upipe_throw_fatal(upipe, UBASE_ERR_UPUMP);
            return UBASE_ERR_UPUMP;
        }
        upipe_bmd_src_set_upump(upipe, upump);
        upump_start(upump);
    }

    return UBASE_ERR_NONE;
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_bmd_src_free(struct upipe *upipe)
{
    struct upipe_bmd_src *upipe_bmd_src = upipe_bmd_src_from_upipe(upipe);

    upipe_bmd_src_work(upipe, NULL);
    if (upipe_bmd_src->deckLinkInput)
        upipe_bmd_src->deckLinkInput->Release();
    if (upipe_bmd_src->deckLink)
        upipe_bmd_src->deckLink->Release();
    if (upipe_bmd_src->deckLinkCaptureDelegate)
        upipe_bmd_src->deckLinkCaptureDelegate->Release();
    uqueue_clean(&upipe_bmd_src->uqueue);

    upipe_bmd_src_output_clean(upipe_bmd_src_output_to_upipe(
                upipe_bmd_src_to_pic_subpipe(upipe_bmd_src)));
    upipe_bmd_src_output_clean(upipe_bmd_src_output_to_upipe(
                upipe_bmd_src_to_sound_subpipe(upipe_bmd_src)));
    upipe_bmd_src_output_clean(upipe_bmd_src_output_to_upipe(
                upipe_bmd_src_to_subpic_subpipe(upipe_bmd_src)));

    upipe_throw_dead(upipe);

    upipe_bmd_src_clean_uref_mgr(upipe);
    upipe_bmd_src_clean_upump(upipe);
    upipe_bmd_src_clean_upump_mgr(upipe);
    upipe_bmd_src_clean_urefcount(upipe);

    free(upipe_bmd_src);
}

extern "C" {
/** module manager static descriptor */
static struct upipe_mgr upipe_bmd_src_mgr = {
    /* .refcount = */ NULL,
    /* .signature = */ UPIPE_BMD_SRC_SIGNATURE,

    /* .upipe_alloc = */ upipe_bmd_src_alloc,
    /* .upipe_input = */ NULL,
    /* .upipe_control = */ upipe_bmd_src_control,

    /* .upipe_mgr_control = */ NULL
};
}

/** @This returns the management structure for all bmd source pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_bmd_src_mgr_alloc(void)
{
    return &upipe_bmd_src_mgr;
}
