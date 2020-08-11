/*
 * Copyright (C) 2012-2016 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
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

/** @file
 * @short Upipe ubuf manager for picture formats with blackmagic storage
 */

#define __STDC_LIMIT_MACROS    1
#define __STDC_FORMAT_MACROS   1
#define __STDC_CONSTANT_MACROS 1

#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/urefcount.h>
#include <upipe/upool.h>
#include <upipe/ubuf.h>
#include <upipe/ubuf_pic.h>
#include <upipe/ubuf_pic_common.h>
#include <upipe-blackmagic/ubuf_pic_blackmagic.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "include/DeckLinkAPI.h"

/** @This is a super-set of the @ref ubuf (and @ref ubuf_pic_common)
 * structure with private fields pointing to shared data. */
struct ubuf_pic_bmd {
    /** pointer to shared structure */
    IDeckLinkVideoFrame *shared;

    /** common picture structure */
    struct ubuf_pic_common ubuf_pic_common;
};

UBASE_FROM_TO(ubuf_pic_bmd, ubuf, ubuf, ubuf_pic_common.ubuf)

/** @This is a super-set of the ubuf_mgr structure with additional local
 * members. */
struct ubuf_pic_bmd_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** ubuf pool */
    struct upool ubuf_pool;

    /** blackmagic pixel format */
    BMDPixelFormat PixelFormat;

    /** common picture management structure */
    struct ubuf_pic_common_mgr common_mgr;

    /** extra space for upool */
    uint8_t upool_extra[];
};

UBASE_FROM_TO(ubuf_pic_bmd_mgr, ubuf_mgr, ubuf_mgr, common_mgr.mgr)
UBASE_FROM_TO(ubuf_pic_bmd_mgr, urefcount, urefcount, urefcount)
UBASE_FROM_TO(ubuf_pic_bmd_mgr, upool, ubuf_pool, ubuf_pool)

/** @This allocates a ubuf, a shared structure and a blackmagic buffer.
 *
 * @param mgr common management structure
 * @param alloc_type must be UBUF_ALLOC_PICTURE (sentinel)
 * @param args optional arguments (1st = hsize, 2nd = vsize)
 * @return pointer to ubuf or NULL in case of allocation error
 */
static struct ubuf *ubuf_pic_bmd_alloc(struct ubuf_mgr *mgr,
                                       uint32_t signature, va_list args)
{
    if (unlikely(signature != UBUF_BMD_ALLOC_PICTURE))
        return NULL;

    struct ubuf_pic_bmd_mgr *pic_mgr =
        ubuf_pic_bmd_mgr_from_ubuf_mgr(mgr);
    void *_VideoFrame = va_arg(args, void *);
    IDeckLinkVideoFrame *VideoFrame = (IDeckLinkVideoFrame *)_VideoFrame;
    BMDPixelFormat PixelFormat = VideoFrame->GetPixelFormat();
    if (unlikely(PixelFormat != pic_mgr->PixelFormat))
        return NULL;

    struct ubuf_pic_bmd *pic_bmd = upool_alloc(&pic_mgr->ubuf_pool,
                                               struct ubuf_pic_bmd *);
    if (unlikely(pic_bmd == NULL))
        return NULL;

    struct ubuf *ubuf = ubuf_pic_bmd_to_ubuf(pic_bmd);
    ubuf->mgr = ubuf_mgr_use(mgr);
    pic_bmd->shared = VideoFrame;
    VideoFrame->AddRef();
    ubuf_pic_common_init(ubuf,
            0, 0, (VideoFrame->GetWidth() + pic_mgr->common_mgr.macropixel - 1)/ pic_mgr->common_mgr.macropixel,
            0, 0, VideoFrame->GetHeight());

    void *buffer;
    VideoFrame->GetBytes(&buffer);
    ubuf_pic_common_plane_init(ubuf, 0, (uint8_t *)buffer,
                               VideoFrame->GetRowBytes());

    return ubuf;
}

/** @This asks for the creation of a new reference to the same buffer space.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf_p reference written with a pointer to the newly allocated
 * ubuf
 * @return an error code
 */
static int ubuf_pic_bmd_dup(struct ubuf *ubuf, struct ubuf **new_ubuf_p)
{
    assert(new_ubuf_p != NULL);
    struct ubuf_pic_bmd_mgr *pic_mgr =
        ubuf_pic_bmd_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic_bmd *new_pic = upool_alloc(&pic_mgr->ubuf_pool,
                                               struct ubuf_pic_bmd *);
    if (unlikely(new_pic == NULL))
        return UBASE_ERR_ALLOC;

    struct ubuf *new_ubuf = ubuf_pic_bmd_to_ubuf(new_pic);
    new_ubuf->mgr = ubuf_mgr_use(ubuf->mgr);
    if (unlikely(!ubase_check(ubuf_pic_common_dup(ubuf, new_ubuf)))) {
        ubuf_free(new_ubuf);
        return UBASE_ERR_INVALID;
    }
    for (uint8_t plane = 0; plane < pic_mgr->common_mgr.nb_planes; plane++) {
        if (unlikely(!ubase_check(ubuf_pic_common_plane_dup(ubuf, new_ubuf, plane)))) {
            ubuf_free(new_ubuf);
            return UBASE_ERR_INVALID;
        }
    }
    *new_ubuf_p = new_ubuf;

    struct ubuf_pic_bmd *pic_bmd = ubuf_pic_bmd_from_ubuf(ubuf);
    new_pic->shared = pic_bmd->shared;
    pic_bmd->shared->AddRef();
    return UBASE_ERR_NONE;
}

/** @This returns the blackmagic video frame. The reference counter is not
 * incremented.
 *
 * @param ubuf pointer to ubuf
 * @param VideoFrame_p filled in with a pointer to IDeckLinkVideoFrame
 * @return an error code
 */
static int _ubuf_pic_bmd_get_video_frame(struct ubuf *ubuf, void **VideoFrame_p)
{
    struct ubuf_pic_bmd *pic_bmd = ubuf_pic_bmd_from_ubuf(ubuf);
    *VideoFrame_p = pic_bmd->shared;
    return UBASE_ERR_NONE;
}

/** @This handles control commands.
 *
 * @param ubuf pointer to ubuf
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int ubuf_pic_bmd_control(struct ubuf *ubuf, int command, va_list args)
{
    switch (command) {
        case UBUF_DUP: {
            struct ubuf **new_ubuf_p = va_arg(args, struct ubuf **);
            return ubuf_pic_bmd_dup(ubuf, new_ubuf_p);
        }
        case UBUF_SIZE_PICTURE: {
            size_t *hsize_p = va_arg(args, size_t *);
            size_t *vsize_p = va_arg(args, size_t *);
            uint8_t *macropixel_p = va_arg(args, uint8_t *);
            return ubuf_pic_common_size(ubuf, hsize_p, vsize_p, macropixel_p);
        }
        case UBUF_ITERATE_PICTURE_PLANE: {
            const char **chroma_p = va_arg(args, const char **);
            return ubuf_pic_common_iterate_plane(ubuf, chroma_p);
        }
        case UBUF_SIZE_PICTURE_PLANE: {
            const char *chroma = va_arg(args, const char *);
            size_t *stride_p = va_arg(args, size_t *);
            uint8_t *hsub_p = va_arg(args, uint8_t *);
            uint8_t *vsub_p = va_arg(args, uint8_t *);
            uint8_t *macropixel_size_p = va_arg(args, uint8_t *);
            return ubuf_pic_common_plane_size(ubuf, chroma, stride_p,
                                              hsub_p, vsub_p,
                                              macropixel_size_p);
        }
        case UBUF_READ_PICTURE_PLANE: {
            const char *chroma = va_arg(args, const char *);
            int hoffset = va_arg(args, int);
            int voffset = va_arg(args, int);
            int hsize = va_arg(args, int);
            int vsize = va_arg(args, int);
            uint8_t **buffer_p = va_arg(args, uint8_t **);
            return ubuf_pic_common_plane_map(ubuf, chroma, hoffset, voffset,
                                             hsize, vsize, buffer_p);
        }
        case UBUF_WRITE_PICTURE_PLANE: {
            /* There is no way to know reference count */
            return UBASE_ERR_BUSY;
        }
        case UBUF_UNMAP_PICTURE_PLANE: {
            /* we don't actually care about the parameters */
            return UBASE_ERR_NONE;
        }
        case UBUF_RESIZE_PICTURE: {
            /* Implementation note: here we agree to extend the ubuf, even
             * if the ubuf buffer is shared. Anyway a subsequent call to
             * @ref ubuf_pic_plane_write would fail and imply a buffer copy,
             * so it doesn't matter. */
            int hskip = va_arg(args, int);
            int vskip = va_arg(args, int);
            int new_hsize = va_arg(args, int);
            int new_vsize = va_arg(args, int);
            return ubuf_pic_common_resize(ubuf, hskip, vskip,
                                          new_hsize, new_vsize);
        }

        case UBUF_PIC_BMD_GET_VIDEO_FRAME: {
            UBASE_SIGNATURE_CHECK(args, UBUF_BMD_ALLOC_PICTURE)
            void **VideoFrame_p = va_arg(args, void **);
            return _ubuf_pic_bmd_get_video_frame(ubuf, VideoFrame_p);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This recycles or frees a ubuf.
 *
 * @param ubuf pointer to a ubuf structure
 */
static void ubuf_pic_bmd_free(struct ubuf *ubuf)
{
    struct ubuf_pic_bmd_mgr *pic_mgr =
        ubuf_pic_bmd_mgr_from_ubuf_mgr(ubuf->mgr);
    struct ubuf_pic_bmd *pic_bmd = ubuf_pic_bmd_from_ubuf(ubuf);

    ubuf_pic_common_clean(ubuf);
    for (uint8_t plane = 0; plane < pic_mgr->common_mgr.nb_planes; plane++)
        ubuf_pic_common_plane_clean(ubuf, plane);

    pic_bmd->shared->Release();
    ubuf_mgr_release(ubuf->mgr);
    upool_free(&pic_mgr->ubuf_pool, pic_bmd);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to ubuf_pic_bmd or NULL in case of allocation error
 */
static void *ubuf_pic_bmd_alloc_inner(struct upool *upool)
{
    struct ubuf_pic_bmd_mgr *pic_bmd_mgr =
        ubuf_pic_bmd_mgr_from_ubuf_pool(upool);
    struct ubuf_mgr *mgr = ubuf_pic_bmd_mgr_to_ubuf_mgr(pic_bmd_mgr);
    struct ubuf_pic_bmd *pic_bmd =
        (struct ubuf_pic_bmd *)malloc(sizeof(struct ubuf_pic_bmd) +
                                      ubuf_pic_common_sizeof(mgr));
    return pic_bmd;
}

/** @internal @This frees a ubuf_pic_bmd.
 *
 * @param upool pointer to upool
 * @param _pic_bmd pointer to a ubuf_pic_bmd structure to free
 */
static void ubuf_pic_bmd_free_inner(struct upool *upool, void *_pic_bmd)
{
    struct ubuf_pic_bmd *pic_bmd = (struct ubuf_pic_bmd *)_pic_bmd;
    free(pic_bmd);
}

/** @This handles manager control commands.
 *
 * @param mgr pointer to ubuf manager
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int ubuf_pic_bmd_mgr_control(struct ubuf_mgr *mgr,
                                    int command, va_list args)
{
    switch (command) {
        case UBUF_MGR_VACUUM: {
            struct ubuf_pic_bmd_mgr *pic_mgr =
                ubuf_pic_bmd_mgr_from_ubuf_mgr(mgr);
            upool_clean(&pic_mgr->ubuf_pool);
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a ubuf manager.
 *
 * @param urefcount pointer to urefcount
 */
static void ubuf_pic_bmd_mgr_free(struct urefcount *urefcount)
{
    struct ubuf_pic_bmd_mgr *pic_mgr =
        ubuf_pic_bmd_mgr_from_urefcount(urefcount);
    struct ubuf_mgr *mgr = ubuf_pic_bmd_mgr_to_ubuf_mgr(pic_mgr);
    upool_clean(&pic_mgr->ubuf_pool);

    ubuf_pic_common_mgr_clean(mgr);

    urefcount_clean(urefcount);
    free(pic_mgr);
}

/** @This allocates a new instance of the ubuf manager for picture formats
 * using blackmagic.
 *
 * @param ubuf_pool_depth maximum number of ubuf structures in the pool
 * @param PixelFormat blackmagic pixel format
 * @return pointer to manager, or NULL in case of error
 */
struct ubuf_mgr *ubuf_pic_bmd_mgr_alloc(uint16_t ubuf_pool_depth,
                                        uint32_t PixelFormat)
{
    uint8_t macropixel;
    switch (PixelFormat) {
        case bmdFormat8BitYUV:
            macropixel = 2;
            break;
        case bmdFormat10BitYUV:
            macropixel = 6;
            break;
        case bmdFormat8BitARGB:
        case bmdFormat8BitBGRA:
        case bmdFormat10BitRGB:
        case bmdFormat10BitRGBXLE:
        case bmdFormat10BitRGBX:
            macropixel = 1;
            break;
        default:
            return NULL;
    }

    struct ubuf_pic_bmd_mgr *pic_mgr =
        (struct ubuf_pic_bmd_mgr *)malloc(sizeof(struct ubuf_pic_bmd_mgr) +
                                          upool_sizeof(ubuf_pool_depth));
    if (unlikely(pic_mgr == NULL))
        return NULL;

    struct ubuf_mgr *mgr = ubuf_pic_bmd_mgr_to_ubuf_mgr(pic_mgr);
    ubuf_pic_common_mgr_init(mgr, macropixel);

    urefcount_init(ubuf_pic_bmd_mgr_to_urefcount(pic_mgr),
                   ubuf_pic_bmd_mgr_free);
    pic_mgr->common_mgr.mgr.refcount = ubuf_pic_bmd_mgr_to_urefcount(pic_mgr);

    mgr->signature = UBUF_BMD_ALLOC_PICTURE;
    mgr->ubuf_alloc = ubuf_pic_bmd_alloc;
    mgr->ubuf_control = ubuf_pic_bmd_control;
    mgr->ubuf_free = ubuf_pic_bmd_free;
    mgr->ubuf_mgr_control = ubuf_pic_bmd_mgr_control;

    pic_mgr->PixelFormat = PixelFormat;
    upool_init(&pic_mgr->ubuf_pool, mgr->refcount, ubuf_pool_depth,
               pic_mgr->upool_extra,
               ubuf_pic_bmd_alloc_inner, ubuf_pic_bmd_free_inner);

    int err;
    switch (PixelFormat) {
        case bmdFormat8BitYUV:
            err = ubuf_pic_common_mgr_add_plane(mgr, "u8y8v8y8", 1, 1, 4);
            break;
        case bmdFormat8BitARGB:
            err = ubuf_pic_common_mgr_add_plane(mgr, "a8r8g8b8", 1, 1, 4);
            break;
        case bmdFormat8BitBGRA:
            err = ubuf_pic_common_mgr_add_plane(mgr, "b8g8r8a8", 1, 1, 4);
            break;
        case bmdFormat10BitRGB:
            err = ubuf_pic_common_mgr_add_plane(mgr, "x2r10g10b10", 1, 1, 4);
            break;
        case bmdFormat10BitRGBXLE:
            err = ubuf_pic_common_mgr_add_plane(mgr, "x2b10g10r10", 1, 1, 4);
            break;
        case bmdFormat10BitRGBX:
            err = ubuf_pic_common_mgr_add_plane(mgr, "r10g10b10x2", 1, 1, 4);
            break;
        case bmdFormat10BitYUV:
            err = ubuf_pic_common_mgr_add_plane(mgr, "u10y10v10y10u10y10v10y10u10y10v10y10", 1, 1, 16);
            break;
        default:
            err = UBASE_ERR_INVALID;
            break;
    }

    if (unlikely(!ubase_check(err))) {
        ubuf_mgr_release(mgr);
        return NULL;
    }

    return mgr;
}
