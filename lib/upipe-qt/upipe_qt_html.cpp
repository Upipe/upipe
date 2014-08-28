/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Sebastien Gougelet
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
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
#define __STDC_LIMIT_MACROS    1
#define __STDC_FORMAT_MACROS   1
#define __STDC_CONSTANT_MACROS 1

#include <upipe/upipe.h>
#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uqueue.h>
#include <upipe/uprobe.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/upump.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_clock.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref_flow.h>
#include <upipe/uref_attr.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_flow_def.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_upump.h>
#include <upipe/upump_blocker.h>
#include <upipe/upump_common.h>

#include <upipe/upipe_helper_output.h>

#include <QImage>
#include <QPaintDevice>
#include <QWebFrame>
#include <QTimer>
#include <QtCore>
#include <QCoreApplication>
#include <QApplication>
#include <QObject>
#include <QWebPage>
#include <QWebView>
#include <QtWebKit>
#include <QUrl>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>

#include <upipe-qt/upipe_qt_html.h>
#include "thumbnail.h"

/** only accept pics */
#define EXPECTED_FLOW_DEF "pic."
#define MAX_QUEUE_LENGTH 255
/** @file
 * @short Upipe HTML renderer
 */
static void upipe_qt_html_freefin(struct upipe *upipe);
/** upipe_qt_html structure to do html */ 
struct upipe_qt_html {
    /** refcount management structure */
    struct urefcount urefcount;
    struct urefcount urefcountfin;

    /** output flow */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;
    /** output pipe */
    struct upipe *output;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;

    /** ubuf manager */
    struct uref_mgr *uref_mgr;

    /** public upipe structure */
    struct upipe upipe;
    
    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** upump to alloc and start the qt thread */
    struct upump *upumpstart;
    /** url */
    const char * url;

    /** Free QT thread */
    bool qtthreadfree;

    /** uqueue structure */
    struct uqueue uqueue;
    /** extra data for the queue structures */
    uint8_t *uqueue_extra;

    /** uqueue structure */
    struct uqueue uqueue2;
    /** extra data for the queue structures */
    uint8_t *uqueue_extra2;

    /** thread to process the qt application */
    pthread_t thread;

    /** size of the source */
    int H;
    int V;
    /** framerate source */
    int fr;
};

UPIPE_HELPER_UPIPE(upipe_qt_html, upipe, UPIPE_QT_HTML_SIGNATURE);
UPIPE_HELPER_UREFCOUNT(upipe_qt_html, urefcount, upipe_qt_html_free)
UPIPE_HELPER_VOID(upipe_qt_html)
UPIPE_HELPER_OUTPUT(upipe_qt_html, output, flow_def, flow_def_sent)
UPIPE_HELPER_UBUF_MGR(upipe_qt_html, ubuf_mgr, flow_def)
UPIPE_HELPER_UREF_MGR(upipe_qt_html, uref_mgr)
UPIPE_HELPER_UPUMP_MGR(upipe_qt_html, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_qt_html, upump, upump_mgr)
UPIPE_HELPER_UPUMP(upipe_qt_html, upumpstart, upump_mgr)

/** @internal @This is the QT thread main function
 *
 * @param arg argument containing the upipe html structure
 */
void *thread_start(void *arg){
    struct upipe_qt_html *html = (struct upipe_qt_html *)arg;
    urefcount_use(&html->urefcountfin);
    char * argv[1];
    char name[] = "QtApp";
    int argc = 1;
    argv[0] = name;
    QApplication app(argc, argv);
    Thumbnail *thumbnail = new Thumbnail(html->url);
    thumbnail->seturefmgr(html->uref_mgr);
    thumbnail->setubufmgr(html->ubuf_mgr);
    thumbnail->setuqueue(&html->uqueue);
    thumbnail->setuqueue2(&html->uqueue2);
    thumbnail->setH(html->H);
    thumbnail->setV(html->V);
    QTimer *timer = new QTimer(thumbnail);
    QObject::connect(timer, SIGNAL(timeout()), thumbnail, SLOT(render()));
    timer->start(html->fr);
    QObject::connect(thumbnail, SIGNAL(finished()), &app, SLOT(quit()));
    QObject::connect(&app, SIGNAL(aboutToQuit()), timer, SLOT(stop()));
    app.exec();
    delete timer;
    delete thumbnail;
    printf("End of the QT App\n");
    struct uref *uref = uref_alloc(html->uref_mgr);
    uqueue_push(&html->uqueue, uref);
    return NULL;
}

/** @internal @This is the worker of the pipe
 *
 * @param upump pump of the worker
 */
static void upipe_qt_html_worker(struct upump *upump){
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_qt_html *upipe_qt_html = upipe_qt_html_from_upipe(upipe);
    struct uref *uref;
    upipe_use(upipe);
    while (uref = uqueue_pop(&upipe_qt_html->uqueue, struct uref *)){
        if (uref->ubuf !=  NULL){      
            upipe_qt_html_output(upipe, uref, &upipe_qt_html->upump);
        } else {
            pthread_join(upipe_qt_html->thread, NULL);
            urefcount_release(&upipe_qt_html->urefcountfin);
            upipe_qt_html_freefin(upipe);
            return;
        }
    }
    upipe_release(upipe);
}

/** @internal @This is the callback of upumpstart, start the QT Thread
 *
 * @param upumpstart pump that start this
 */
void start(struct upump *upumpstart){
    struct upipe *upipe = upump_get_opaque(upumpstart, struct upipe *);
    struct upipe_qt_html *upipe_qt_html = upipe_qt_html_from_upipe(upipe);
    
    struct uref *flow_def = uref_pic_flow_alloc_def(upipe_qt_html->uref_mgr, 1);
    uref_pic_flow_set_macropixel(flow_def, 1);
    uref_pic_flow_add_plane(flow_def, 1, 1, 4, "b8g8r8a8");
    uref_pic_flow_set_hsize(flow_def, upipe_qt_html->H);
    uref_pic_flow_set_vsize(flow_def, upipe_qt_html->V);
    upipe_qt_html_store_flow_def(upipe, flow_def);
    upipe_qt_html_check_ubuf_mgr(upipe);

    upipe_qt_html->qtthreadfree = false;
    upipe_qt_html->uqueue_extra = (uint8_t*)malloc(uqueue_sizeof(MAX_QUEUE_LENGTH));
    uqueue_init(&upipe_qt_html->uqueue, MAX_QUEUE_LENGTH,
                upipe_qt_html->uqueue_extra);
    upipe_qt_html->uqueue_extra2 = (uint8_t*)malloc(uqueue_sizeof(MAX_QUEUE_LENGTH));
    uqueue_init(&upipe_qt_html->uqueue2, MAX_QUEUE_LENGTH,
                upipe_qt_html->uqueue_extra2);

    struct upump *upump =
            uqueue_upump_alloc_pop(&upipe_qt_html->uqueue,
                                   upipe_qt_html->upump_mgr,
                                   upipe_qt_html_worker, upipe);
    upipe_qt_html_set_upump(upipe, upump);
    upump_start(upump);

    upipe_qt_html->url = "http://upipe.org/blog/";
    pthread_create(&upipe_qt_html->thread, NULL, thread_start, (void *)upipe_qt_html);
    upipe_qt_html_clean_upumpstart(upipe);
    upipe_qt_html_set_upumpstart(upipe,NULL);
}

/** @internal @This amends a proposed flow format.
 * 
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_qt_html_amend_flow_format(struct upipe *upipe,
                                                  struct uref *flow_format)
{
    if (flow_format == NULL)
        return UBASE_ERR_INVALID;

    uint64_t align;
    if (!ubase_check(uref_pic_flow_get_align(flow_format, &align)) || !align)
        return uref_pic_flow_set_align(flow_format, 16);

    if (align % 16) {
        align = align * 16 / ubase_gcd(align, 16);
        return uref_pic_flow_set_align(flow_format, align);
    }

    return UBASE_ERR_NONE;
}

/** @internal @This sets the input flow definition.
 *
 * @param upipe description structure of the pipe
 * @param flow_def flow definition packet
 * @return an error code
 */
static int upipe_qt_html_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    if (flow_def == NULL)
        return UBASE_ERR_INVALID;

    UBASE_RETURN(uref_flow_match_def(flow_def, "pic."))
    flow_def = uref_dup(flow_def);

    if (unlikely(flow_def == NULL)) {
        upipe_throw_fatal(upipe, UBASE_ERR_ALLOC);
        return UBASE_ERR_ALLOC;
    }

    upipe_qt_html_store_flow_def(upipe, flow_def);
    return UBASE_ERR_NONE;
}

/** @internal @This start the upumpstart if there is a upump_mgr
 *
 * @param upipe description structure of the pipe
 */
static int find_upump_mgr(struct upipe *upipe){
    upipe_qt_html_attach_upump_mgr(upipe);
    upipe_qt_html_wait_upumpstart(upipe, 0, start);
    return UBASE_ERR_NONE;
}

/** @internal @This processes control commands on a file source pipe, and
 * checks the status of the pipe afterwards.
 * 
 * @param upipe description structure of the pipe
 * @param command type of command
 * @param args arguments of the command
 * @return an error code
 */
static int upipe_qt_html_control(struct upipe *upipe, int command, va_list args)
{
    struct upipe_qt_html *html = upipe_qt_html_from_upipe(upipe);
    switch (command) {
        /* generic commands */
        case UPIPE_ATTACH_UBUF_MGR:
            return upipe_qt_html_attach_ubuf_mgr(upipe);
        case UPIPE_ATTACH_UREF_MGR:
            return upipe_qt_html_attach_uref_mgr(upipe);
        case UPIPE_ATTACH_UPUMP_MGR:
            return find_upump_mgr(upipe);
        case UPIPE_AMEND_FLOW_FORMAT: {
            struct uref *flow_format = va_arg(args, struct uref *);
            return upipe_qt_html_amend_flow_format(upipe, flow_format);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_qt_html_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_qt_html_set_output(upipe, output);
        }
        case UPIPE_GET_FLOW_DEF: {
            struct uref **p = va_arg(args, struct uref **);
            return upipe_qt_html_get_flow_def(upipe, p);
        }
        case UPIPE_SET_FLOW_DEF: {
            struct uref *flow = va_arg(args, struct uref *);
            return upipe_qt_html_set_flow_def(upipe, flow);
        }
        case UPIPE_QT_HTML_SET_URL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QT_HTML_SIGNATURE);
            html->url = va_arg(args, const char*);
            return UBASE_ERR_NONE;
        }
        case UPIPE_QT_HTML_GET_URL: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QT_HTML_SIGNATURE);
            *va_arg(args, const char **) = html->url;
            return UBASE_ERR_NONE;
        }
        case UPIPE_QT_HTML_SET_SIZE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QT_HTML_SIGNATURE);
            html->H = va_arg(args, int);
            html->V = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_QT_HTML_GET_SIZE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QT_HTML_SIGNATURE);
            *va_arg(args, int *) = html->H;
            *va_arg(args, int *) = html->V;
            return UBASE_ERR_NONE;
        }
        case UPIPE_QT_HTML_SET_FRAMERATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QT_HTML_SIGNATURE);
            html->fr = va_arg(args, int);
            return UBASE_ERR_NONE;
        }
        case UPIPE_QT_HTML_GET_FRAMERATE: {
            UBASE_SIGNATURE_CHECK(args, UPIPE_QT_HTML_SIGNATURE);
            *va_arg(args, int *) = html->fr;
            return UBASE_ERR_NONE;
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @internal @This frees a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qt_html_freefin(struct upipe *upipe){
    upipe_throw_dead(upipe);
    struct upipe_qt_html *upipe_qt_html = upipe_qt_html_from_upipe(upipe); 
    free(upipe_qt_html->uqueue_extra);
    free(upipe_qt_html->uqueue_extra2);
    upipe_qt_html->qtthreadfree = true;
    upipe_qt_html_clean_upump(upipe);
    upipe_qt_html_clean_output(upipe);
    upipe_qt_html_clean_ubuf_mgr(upipe);
    upipe_qt_html_clean_uref_mgr(upipe);
    upipe_qt_html_clean_urefcount(upipe);
    upipe_qt_html_clean_upump_mgr(upipe);
    upipe_qt_html_clean_upumpstart(upipe);
    upipe_qt_html_free_void(upipe);
}
/** @internal @This start to free a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_qt_html_free(struct upipe *upipe)
{
    struct upipe_qt_html *upipe_qt_html = upipe_qt_html_from_upipe(upipe);
    uqueue_push(&upipe_qt_html->uqueue2, (void *)42);
}

/** @internal @This allocates a html pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param signature signature of the pipe allocator
 * @param args optional arguments
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_qt_html_alloc(struct upipe_mgr *mgr,
                                     struct uprobe *uprobe,
                                     uint32_t signature, va_list args)
{
    struct upipe *upipe = upipe_qt_html_alloc_void(mgr, uprobe, signature,
                                                args);
    struct upipe_qt_html *upipe_qt_html = upipe_qt_html_from_upipe(upipe);
    if (unlikely(upipe == NULL))
        return NULL;
    upipe_qt_html->H = 720;
    upipe_qt_html->V = 576;
    upipe_qt_html->fr = 40;
    upipe_qt_html_init_urefcount(upipe);
    urefcount_init(&upipe_qt_html->urefcountfin, NULL);
    upipe_qt_html_init_ubuf_mgr(upipe);
    upipe_qt_html_init_uref_mgr(upipe); 
    upipe_qt_html_init_output(upipe);
    upipe_qt_html_init_upump_mgr(upipe);
    upipe_qt_html_init_upump(upipe);
    upipe_qt_html_init_upumpstart(upipe);
    upipe_qt_html_check_uref_mgr(upipe);
    upipe_qt_html_check_upump_mgr(upipe);
    if (upipe_qt_html->upump_mgr != NULL)
        upipe_qt_html_wait_upumpstart(upipe, 0, start);
    upipe_throw_ready(upipe);
    return upipe;
}

/** module manager static descriptor */
static struct upipe_mgr upipe_qt_html_mgr = {
    .refcount = NULL,
    .signature = UPIPE_QT_HTML_SIGNATURE,

    .upipe_alloc = upipe_qt_html_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_qt_html_control,

    .upipe_mgr_control = NULL
};

/** @This returns the management structure for html pipes
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_qt_html_mgr_alloc(void)
{
    return &upipe_qt_html_mgr;
}
