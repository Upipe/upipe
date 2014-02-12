/*
 * Copyright (C) 2013 OpenHeadend S.A.R.L.
 *
 * Authors: Benjamin Cohen
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

#include <upipe/ubase.h>
#include <stdio.h>
#include <stdlib.h>
#include <DeckLinkAPI.h>
#include <DeckLinkAPIDispatch.cpp>
#include "blackmagic_wrap.h"

#ifndef NULL
#define NULL 0
#endif

/** @internal @This is the private context of a blackmagic wrapper */
struct bmd_wrap {
    /** audio callback */
    bmd_wrap_cb audio_cb;
    /** video callback */
    bmd_wrap_cb video_cb;
    /** user-defined opaque */
    void *opaque;

    /** blackmagic input */
    IDeckLinkInput *input;
};

class DeckLinkCaptureDelegate : public IDeckLinkInputCallback
{
public:
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) {
        return E_NOINTERFACE;
    }
	virtual ULONG STDMETHODCALLTYPE AddRef(void);
	virtual ULONG STDMETHODCALLTYPE  Release(void);
	virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
                BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*,
                BMDDetectedVideoInputFormatFlags);
	virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
                IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*);

    struct bmd_wrap wrap;

private:
};

HRESULT DeckLinkCaptureDelegate::VideoInputFormatChanged(
                BMDVideoInputFormatChangedEvents events,
                IDeckLinkDisplayMode *mode, BMDDetectedVideoInputFormatFlags)
{
    printf("Caught input format changed\n");
    return S_OK;
}

ULONG DeckLinkCaptureDelegate::AddRef(void)
{
	return 1;
}

ULONG DeckLinkCaptureDelegate::Release(void)
{
	return 1;
}

/** @internal @This is called when receiving an audio or video frame
 * @param videoFrame video frame
 * @param audioFrame audio frame
 * @return S_OK
 */
HRESULT DeckLinkCaptureDelegate::VideoInputFrameArrived(
                        IDeckLinkVideoInputFrame* videoFrame,
                        IDeckLinkAudioInputPacket* audioFrame)
{
    struct bmd_frame frame;

	/* handle video frame */
	if(videoFrame && (! (videoFrame->GetFlags() & bmdFrameHasNoInputSource))
                  && wrap.video_cb)
    {
        videoFrame->GetBytes((void **)&frame.data);
        frame.stride = videoFrame->GetRowBytes();
        frame.width = videoFrame->GetWidth();
        frame.height = videoFrame->GetHeight();

        /* TODO duration and timecode */
        frame.duration = frame.timecode = 0;

        /* user callback */
        wrap.video_cb(wrap.opaque, &frame);
    }

	/* handle audio frame */
	if (audioFrame && wrap.audio_cb)
	{
        audioFrame->GetBytes((void **)&frame.data);
        frame.samples = audioFrame->GetSampleFrameCount();
        wrap.audio_cb(wrap.opaque, &frame);
        /* TODO handle audio */
	}
    return S_OK;
}

/** @This sets the video callback to a blackmagic wrapper
 * @param wrap pointer to wrap structure
 * @param cb callback
 * @return previous callback
 */
bmd_wrap_cb bmd_wrap_set_video_cb(struct bmd_wrap *wrap, bmd_wrap_cb cb)
{
    bmd_wrap_cb old = wrap->video_cb;
    wrap->video_cb = cb;
    return old;
}

/** @This sets the audio callback to a blackmagic wrapper
 * @param wrap pointer to wrap structure
 * @param cb callback
 * @return previous callback
 */
bmd_wrap_cb bmd_wrap_set_audio_cb(struct bmd_wrap *wrap, bmd_wrap_cb cb)
{
    bmd_wrap_cb old = wrap->video_cb;
    wrap->video_cb = cb;
    return old;
}

/** @This starts blackmagic streams
 * @param wrap pointer to wrap structure
 * @return false in case of error
 */
bool bmd_wrap_start(struct bmd_wrap *wrap)
{
    /* start blackmagic streams (spawns a thread) */
	if (wrap->input->StartStreams() < 0) {
        return false;
    }
    return true;
}

/** @This stops blackmagic streams
 * @param wrap pointer to wrap structure
 * @return false in case of error
 */
bool bmd_wrap_stop(struct bmd_wrap *wrap)
{
    /* flush internal queue */
    wrap->input->FlushStreams();

    /* stop blackmagic streams */
	if (wrap->input->StopStreams() < 0) {
        return false;
    }
    return true;
}

/** @This stops releases a blackmagic wrapper
 * @param wrap pointer to wrap structure
 * @return false in case of error
 */
bool bmd_wrap_free(struct bmd_wrap *wrap)
{
    bmd_wrap_stop(wrap);
    /* TODO */
    return true;
}

/** @This allocates a blackmagic wrapper
 * @param opaque user-defined opaque
 * @return pointer to wrap structure
 */
struct bmd_wrap *bmd_wrap_alloc(void *opaque)
{
    struct bmd_wrap *wrap;
    IDeckLinkIterator *deckLinkIterator;
    IDeckLink *deckLink;
    IDeckLinkInput *deckLinkInput;
	DeckLinkCaptureDelegate *delegate;

    deckLinkIterator = CreateDeckLinkIteratorInstance();
    if (!deckLinkIterator) {
        return NULL;
    }

    /* get decklink input handler */
    deckLinkIterator->Next(&deckLink);
	deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&deckLinkInput); 

    /* callback helper */
    delegate = new DeckLinkCaptureDelegate();
	deckLinkInput->SetCallback(delegate);


    /* configure input */
    /* FIXME hardcoded parameters */
    deckLinkInput->EnableVideoInput(bmdModeHD1080i50, bmdFormat8BitYUV, 0);
    deckLinkInput->EnableAudioInput(bmdAudioSampleRate48kHz, 16, 2);

    wrap = &delegate->wrap;
    memset(wrap, 0, sizeof(struct bmd_wrap));
    wrap->input = deckLinkInput;
    wrap->opaque = opaque;

    return wrap;
}
