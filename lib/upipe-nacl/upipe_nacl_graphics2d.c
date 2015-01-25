/*
 * Copyright (C) 2014-2015 OpenHeadend S.A.R.L.
 *
 * Authors: Xavier Boulet
 *          Christophe Massiot
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

#undef NDEBUG

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_clock.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_dump.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upipe_helper_input.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe-nacl/upipe_nacl_graphics2d.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include <ppapi/c/pp_errors.h>
#include <ppapi/c/pp_resource.h>
#include <ppapi/c/pp_size.h>
#include <ppapi/c/ppb_core.h>
#include <ppapi/c/ppb_graphics_2d.h>
#include <ppapi/c/ppb_image_data.h>
#include <ppapi/c/ppb_instance.h>
#include <ppapi_simple/ps.h>

/** @hidden */
static bool upipe_nacl_g2d_output(struct upipe *upipe, struct uref *uref,
                                  struct upump **upump_p);
/** @hidden */
static void upipe_nacl_g2d_write_watcher(struct upump *upump);

/** @internal element of a list of urequests */
struct upipe_nacl_g2d_request {
    /** structure for double-linked lists */
    struct uchain uchain;
    /** pointer to upstream request */
    struct urequest *upstream;
};

UBASE_FROM_TO(upipe_nacl_g2d_request, uchain, uchain, uchain)

/** @internal upipe_nacl_g2d private structure */
struct upipe_nacl_g2d {
    /** refcount management structure */
    struct urefcount urefcount;

    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** uclock request */
    struct urequest uclock_request;

    /** temporary uref storage */
    struct uchain urefs;
    /** nb urefs in storage */
    unsigned int nb_urefs;
    /** max urefs in storage */
    unsigned int max_urefs;
    /** list of blockers */
    struct uchain blockers;

    /** delay applied to pts attribute when uclock is provided */
    uint64_t latency;
    /** current width of the viewport */
    unsigned int width;
    /** current height of the viewport */
    unsigned int height;
    /** pointer to NaCl core interface */
    PPB_Core* ppb_core_interface;
    /** pointer to NaCl instance interface */
    PPB_Instance *ppb_instance_interface;
    /** pointer to NaCl g2d interface */
    PPB_Graphics2D *ppb_g2d_interface;
    /** pointer to NaCl imagedata interface */
    PPB_ImageData* ppb_imagedata_interface;
    /** handle to g2d context */
    PP_Resource g2d;
    /** native image data format */
    PP_ImageDataFormat native_imagedata_format;
    /** native chroma format */
    const char *native_chroma;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** event watcher */
    struct upump *upump_watcher;
    /** write watcher */
    struct upump *upump;

    /** list of flow_format urequests */
    struct uchain urequests;

    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_nacl_g2d, upipe, UPIPE_NACL_G2D_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_nacl_g2d, urefcount, upipe_nacl_g2d_free);
UPIPE_HELPER_VOID(upipe_nacl_g2d);
UPIPE_HELPER_UCLOCK(upipe_nacl_g2d, uclock, uclock_request, NULL, upipe_throw_provide_request, NULL)

UPIPE_HELPER_UPUMP_MGR(upipe_nacl_g2d, upump_mgr);
UPIPE_HELPER_UPUMP(upipe_nacl_g2d, upump, upump_mgr);
UPIPE_HELPER_INPUT(upipe_nacl_g2d, urefs, nb_urefs, max_urefs, blockers, upipe_nacl_g2d_output);

/** @internal @This allocates a nacl_g2d pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_nacl_g2d_alloc(struct upipe_mgr *mgr,
                                          struct uprobe *uprobe,
                                          uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_nacl_g2d_alloc_void(mgr, uprobe,
                                                    signature, args);
    if (unlikely(upipe == NULL))
        return NULL;

    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);
    upipe_nacl_g2d_init_urefcount(upipe);
    upipe_nacl_g2d_init_upump_mgr(upipe);
    upipe_nacl_g2d_init_upump(upipe);
    upipe_nacl_g2d_init_input(upipe);
    upipe_nacl_g2d_init_uclock(upipe);
    upipe_nacl_g2d->latency = 0;
    ulist_init(&upipe_nacl_g2d->urequests);

    upipe_nacl_g2d->ppb_core_interface =
        (PPB_Core *)PSGetInterface(PPB_CORE_INTERFACE);
    upipe_nacl_g2d->ppb_g2d_interface =
        (PPB_Graphics2D *)PSGetInterface(PPB_GRAPHICS_2D_INTERFACE);
    upipe_nacl_g2d->ppb_instance_interface =
        (PPB_Instance *)PSGetInterface(PPB_INSTANCE_INTERFACE);
    upipe_nacl_g2d->ppb_imagedata_interface =
        (PPB_ImageData *)PSGetInterface(PPB_IMAGEDATA_INTERFACE);
    upipe_nacl_g2d->native_imagedata_format =
        upipe_nacl_g2d->ppb_imagedata_interface->GetNativeImageDataFormat();
    switch (upipe_nacl_g2d->native_imagedata_format) {
        case PP_IMAGEDATAFORMAT_BGRA_PREMUL:
            upipe_nacl_g2d->native_chroma = "b8g8r8a8";
            break;
        case PP_IMAGEDATAFORMAT_RGBA_PREMUL:
            upipe_nacl_g2d->native_chroma = "r8g8b8a8";
            break;
        default:
            assert(0);
            break;
    }
    upipe_nacl_g2d->g2d = 0;

    upipe_throw_ready(upipe);
    upipe_nacl_g2d_check_upump_mgr(upipe);

    return upipe;
}

/** @internal @This handles input pics.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static bool upipe_nacl_g2d_output(struct upipe *upipe, struct uref *uref, 
                                  struct upump **upump_p)
{
    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);

    const char *def;
    if (unlikely(ubase_check(uref_flow_get_def(uref, &def)))) {
        upipe_nacl_g2d->latency = 0;
        uref_clock_get_latency(uref, &upipe_nacl_g2d->latency);

        uref_free(uref);
        return true;
    }

    if (likely(upipe_nacl_g2d->uclock != NULL)) {
        uint64_t pts = 0;
        if (likely(ubase_check(uref_clock_get_pts_sys(uref, &pts)))) {
            pts += upipe_nacl_g2d->latency;
            uint64_t now = uclock_now(upipe_nacl_g2d->uclock);
            if (now < pts) {
                upipe_verbose_va(upipe, "sleeping %"PRIu64" (%"PRIu64")",
                                 pts - now, pts);
                upipe_nacl_g2d_wait_upump(upipe, pts - now,
                                          upipe_nacl_g2d_write_watcher);
                return false;
            } else if (now > pts + UCLOCK_FREQ / 10) {
                upipe_warn_va(upipe, "late picture dropped (%"PRId64")",
                              (now - pts) * 1000 / UCLOCK_FREQ);
                uref_free(uref);
                return true;
            }
        } else
            upipe_warn(upipe, "received non-dated buffer");
    }

    size_t hsize, vsize, stride;
    const uint8_t *src = NULL;
    if (unlikely(!ubase_check(uref_pic_size(uref, &hsize, &vsize, NULL)) ||
                 !ubase_check(uref_pic_plane_size(uref,
                        upipe_nacl_g2d->native_chroma, &stride,
                        NULL, NULL, NULL)) ||
                 !ubase_check(uref_pic_plane_read(uref,
                        upipe_nacl_g2d->native_chroma, 0, 0, -1, -1,
                        &src)))) {
        upipe_warn(upipe, "unable to map picture plane");
        uref_free(uref);
        return true;
    }

    struct PP_Size size;
    size.width = upipe_nacl_g2d->width;
    size.height = upipe_nacl_g2d->height;
    PP_Resource image =
        upipe_nacl_g2d->ppb_imagedata_interface->Create(PSGetInstanceId(),
            upipe_nacl_g2d->native_imagedata_format, &size, PP_FALSE);
    assert(image);

    struct PP_ImageDataDesc desc;
    upipe_nacl_g2d->ppb_imagedata_interface->Describe(image, &desc);
    uint8_t *dst = upipe_nacl_g2d->ppb_imagedata_interface->Map(image);

    /* TODO: it is possible to avoid this memcpy by creating a ubuf manager
     * allowing swscale to write directly into the image buffer. However
     * the best would be to use graphics3d-accelerated YUV transform. */
    for (int y = 0; y < vsize; y++)
        memcpy(dst + y * desc.stride, src + y * stride, 4 * hsize);

    upipe_nacl_g2d->ppb_imagedata_interface->Unmap(image);  
    uref_pic_plane_unmap(uref, upipe_nacl_g2d->native_chroma,
                         0, 0, -1, -1);

    upipe_nacl_g2d->ppb_g2d_interface->ReplaceContents(
            upipe_nacl_g2d->g2d, image);

    struct PP_CompletionCallback cb_null =
        PP_MakeCompletionCallback(NULL, NULL);
    int32_t err = upipe_nacl_g2d->ppb_g2d_interface->Flush(upipe_nacl_g2d->g2d,
                                                           cb_null);
    if (unlikely(err != PP_OK))
        upipe_warn_va(upipe, "g2d flush returned error %"PRId32, err);

    upipe_nacl_g2d->ppb_core_interface->ReleaseResource(image);
    uref_free(uref);
    return true;
}

/** @internal @This is called when the picture should be displayed.
 *
 * @param upump description structure of the watcher
 */
static void upipe_nacl_g2d_write_watcher(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    upipe_nacl_g2d_set_upump(upipe, NULL);
    upipe_nacl_g2d_output_input(upipe);
    upipe_nacl_g2d_unblock_input(upipe);
}

/** @internal @This handles input.
 *
 * @param upipe description structure of the pipe
 * @param uref uref structure
 * @param upump_p reference to upump structure
 */
static void upipe_nacl_g2d_input(struct upipe *upipe, struct uref *uref,
                                 struct upump **upump_p)
{
    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);

    if (!upipe_nacl_g2d->g2d) {
        upipe_warn(upipe, "g2d context not ready");
        uref_free(uref);
        return;
    }

    if (!upipe_nacl_g2d_check_input(upipe) ||
        !upipe_nacl_g2d_output(upipe, uref, upump_p)) {
        upipe_nacl_g2d_hold_input(upipe, uref);
        upipe_nacl_g2d_block_input(upipe, upump_p);
    }
}

/** @internal @This provides a flow format suggestion.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_nacl_g2d_provide_flow_format(struct upipe *upipe,
                                              struct urequest *request)
{
    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);
    struct uref *flow_format = uref_dup(request->uref);
    UBASE_ALLOC_RETURN(flow_format);
    uref_pic_flow_clear_format(flow_format);
    uref_pic_flow_set_macropixel(flow_format, 1);
    uref_pic_flow_set_planes(flow_format, 0);
    uref_pic_flow_add_plane(flow_format, 1, 1, 4,
                            upipe_nacl_g2d->native_chroma);
    uref_pic_flow_set_hsize(flow_format, upipe_nacl_g2d->width);
    uref_pic_flow_set_vsize(flow_format, upipe_nacl_g2d->height);
    uref_pic_set_progressive(flow_format);
    return urequest_provide_flow_format(request, flow_format);
}

/** @internal @This updates the size of the viewport.
 *
 * @param upipe description structure of the pipe
 * @param width width of the viewport
 * @param height height of the viewport
 * @return an error code
 */
static int upipe_nacl_g2d_update_size(struct upipe *upipe,
                                      unsigned int width, unsigned int height)
{
    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);
    if (upipe_nacl_g2d->width == width && upipe_nacl_g2d->height == height)
        return UBASE_ERR_NONE;

    upipe_nacl_g2d->ppb_core_interface->ReleaseResource(upipe_nacl_g2d->g2d);

    upipe_notice_va(upipe, "configuring for %ux%u", width, height);
    struct PP_Size size;
    size.width = upipe_nacl_g2d->width = width;
    size.height = upipe_nacl_g2d->height = height;
    upipe_nacl_g2d->g2d =
        upipe_nacl_g2d->ppb_g2d_interface->Create(PSGetInstanceId(),
                                                              &size, PP_TRUE);
    assert(upipe_nacl_g2d->g2d);
    if (unlikely(!upipe_nacl_g2d->ppb_instance_interface->BindGraphics(
                    PSGetInstanceId(), upipe_nacl_g2d->g2d))) {
        upipe_err(upipe, "unable to bind g2d");
        upipe_nacl_g2d->ppb_core_interface->ReleaseResource(
                upipe_nacl_g2d->g2d);
        return UBASE_ERR_EXTERNAL;
    }

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_nacl_g2d->urequests, uchain, uchain_tmp) {
        struct upipe_nacl_g2d_request *proxy =
            upipe_nacl_g2d_request_from_uchain(uchain);
        upipe_nacl_g2d_provide_flow_format(upipe, proxy->upstream);
    }
    return UBASE_ERR_NONE;
}

/** @internal @This sets an option (size).
 *
 * @param upipe description structure of the pipe
 * @param option option name
 * @param content option content
 * @return an error code
 */
static int upipe_nacl_g2d_set_option(struct upipe *upipe,
                                     const char *option, const char *content)
{
    if (strcmp(option, "size"))
        return UBASE_ERR_UNHANDLED;
    unsigned int width, height;
    if (sscanf(content, "%ux%u", &width, &height) != 2)
        return UBASE_ERR_INVALID;
    return upipe_nacl_g2d_update_size(upipe, width, height);
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_nacl_g2d_set_flow_def(struct upipe *upipe,
                                       struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;
    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))

    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);
    uint8_t macropixel;
    if (!ubase_check(uref_pic_flow_get_macropixel(flow_def, &macropixel)) ||
        macropixel != 1 ||
        !ubase_check(uref_pic_flow_check_chroma(flow_def, 1, 1, 4,
                upipe_nacl_g2d->native_chroma))) {
        upipe_err(upipe, "incompatible flow definition");
        uref_dump(flow_def, upipe->uprobe);
        return UBASE_ERR_INVALID;
    }

    flow_def = uref_dup(flow_def);
    UBASE_ALLOC_RETURN(flow_def)
    upipe_input(upipe, flow_def, NULL);
    return UBASE_ERR_NONE;
}

/** @internal @This registers a urequest.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_nacl_g2d_register_request(struct upipe *upipe,
                                           struct urequest *request)
{
    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);
    if (request->type != UREQUEST_FLOW_FORMAT)
        return upipe_throw_provide_request(upipe, request);

    struct upipe_nacl_g2d_request *proxy =
        malloc(sizeof(struct upipe_nacl_g2d_request));
    UBASE_ALLOC_RETURN(proxy)
    uchain_init(upipe_nacl_g2d_request_to_uchain(proxy));
    proxy->upstream = request;
    ulist_add(&upipe_nacl_g2d->urequests,
              upipe_nacl_g2d_request_to_uchain(proxy));

    return upipe_nacl_g2d_provide_flow_format(upipe, request);
}

/** @internal @This unregisters a urequest.
 *
 * @param upipe description structure of the pipe
 * @param request description structure of the request
 * @return an error code
 */
static int upipe_nacl_g2d_unregister_request(struct upipe *upipe,
                                             struct urequest *request)
{
    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);
    if (request->type != UREQUEST_FLOW_FORMAT)
        return UBASE_ERR_NONE;

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_nacl_g2d->urequests, uchain, uchain_tmp) {
        struct upipe_nacl_g2d_request *proxy =
            upipe_nacl_g2d_request_from_uchain(uchain);
        if (proxy->upstream == request) {
            ulist_delete(uchain);
            free(proxy);
            return UBASE_ERR_NONE;
        }
    }
    return UBASE_ERR_INVALID;
}

/** @internal @This processes control commands on the pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_nacl_g2d_control(struct upipe *upipe,
                                  int command, va_list args)
{
    switch (command) {
        case UPIPE_ATTACH_UPUMP_MGR: {
            upipe_nacl_g2d_set_upump(upipe, NULL);
            UBASE_RETURN(upipe_nacl_g2d_attach_upump_mgr(upipe))
            return UBASE_ERR_NONE;
        }
        case UPIPE_ATTACH_UCLOCK:
            upipe_nacl_g2d_set_upump(upipe, NULL);
            upipe_nacl_g2d_require_uclock(upipe);
            return UBASE_ERR_NONE;
        case UPIPE_REGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_nacl_g2d_register_request(upipe, request);
        }
        case UPIPE_UNREGISTER_REQUEST: {
            struct urequest *request = va_arg(args, struct urequest *);
            return upipe_nacl_g2d_unregister_request(upipe, request);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow_def = va_arg(args, struct uref*);
            return upipe_nacl_g2d_set_flow_def(upipe, flow_def);
        }
        case UPIPE_SET_OPTION: {
            const char *option = va_arg(args, const char *);
            const char *content = va_arg(args, const char *);
            return upipe_nacl_g2d_set_option(upipe, option, content);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_nacl_g2d_free(struct upipe *upipe)
{
    struct upipe_nacl_g2d *upipe_nacl_g2d = upipe_nacl_g2d_from_upipe(upipe);
    upipe_throw_dead(upipe);

    upipe_nacl_g2d->ppb_core_interface->ReleaseResource(upipe_nacl_g2d->g2d);

    struct uchain *uchain, *uchain_tmp;
    ulist_delete_foreach (&upipe_nacl_g2d->urequests, uchain, uchain_tmp) {
        struct upipe_nacl_g2d_request *proxy =
            upipe_nacl_g2d_request_from_uchain(uchain);
        ulist_delete(uchain);
        free(proxy);
    }

    upipe_nacl_g2d_clean_upump(upipe);
    upipe_nacl_g2d_clean_upump_mgr(upipe);
    upipe_nacl_g2d_clean_input(upipe);
    upipe_nacl_g2d_clean_uclock(upipe);
    upipe_nacl_g2d_clean_urefcount(upipe);

    upipe_nacl_g2d_free_void(upipe);
}

/** module manager static descriptor */
static struct upipe_mgr upipe_nacl_g2d_mgr = {
    .refcount = NULL,
    .signature = UPIPE_NACL_G2D_SIGNATURE,

    .upipe_alloc = upipe_nacl_g2d_alloc,
    .upipe_input = upipe_nacl_g2d_input,
    .upipe_control = upipe_nacl_g2d_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for nacl_g2d pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_nacl_g2d_mgr_alloc(void)
{
    return &upipe_nacl_g2d_mgr;
}
