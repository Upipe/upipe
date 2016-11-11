/*
 * Copyright (C) 2014 OpenHeadend S.A.R.L.
 *
 * Authors: Sebastien Gougelet
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, includin
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

#define __STDC_LIMIT_MACROS    1
#define __STDC_FORMAT_MACROS   1
#define __STDC_CONSTANT_MACROS 1

#include <upipe/ubase.h>
#include <upipe/uqueue.h>
#include <upipe/uref.h>
#include <upipe/ubuf.h>
#include <upipe/uref_pic.h>
#include <upipe/ubuf_pic.h>
#include <upipe/uref_flow.h>

#include <QtCore>
#include <QImage>
#include <QPaintDevice>
#include <QWebFrame>
#include <QTimer>
#include <QApplication>
#include <QObject>
#include <QWebView>
#include <QWebPage>
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

/** @This is the main constructor of the Thumbnail
 *
 * @param url chosen url
 */
Thumbnail::Thumbnail(const char *url)
{
    QUrl qurl;
    this->url = url;
    if (!strncmp(url,"http",4)) {
        qurl = QUrl(this->url);
    } else {
        qurl = QUrl::fromLocalFile(this->url);
    }

    view.load(qurl);
}

/** @This set the uref_mgr of the Thumbnail
 *
 * @param uref_mgr chosen uref_mgr
 */
void Thumbnail::seturefmgr(struct uref_mgr *uref_mgr)
{
    this->uref_mgr = uref_mgr;
}

/** @This set the ubuf_mgr of the Thumbnail
 *
 * @param ubuf_mgr chosen ubuf_mgr
 */
void Thumbnail::setubufmgr(struct ubuf_mgr *ubuf_mgr)
{
    this->ubuf_mgr = ubuf_mgr;
}

/** @This set the uqueue of the Thumbnail
 *
 * @param uqueue chosen uqueue
 */
void Thumbnail::setuqueue(struct uqueue *uqueue)
{
    this->uqueue = uqueue;
}

/** @This set the second uqueue (uqueue2) of the Thumbnail
 *
 * @param uqueue2 chosen uqueue2
 */
void Thumbnail::setuqueue2(struct uqueue *uqueue2)
{
    this->uqueue2 = uqueue2;
}

/** @This set the url of the Thumbnail
 *
 * @param url chosen url
 */
void Thumbnail::seturl(const char *url)
{
    this->url = url;
}

/** @This set the H size of the Thumbnail output
 *
 * @param H chosen H size
 */
void Thumbnail::setH(int H)
{
    this->H = H;
}

/** @This set the V size of the Thumbnail output
 *
 * @param V chosen V size
 */
void Thumbnail::setV(int V)
{
    this->V = V;
}

/** @This is the rendering function of the Thumbnail
 *
 */
void Thumbnail::render()
{
    if ((uqueue_pop(uqueue2, void *)) != (void *)0){
        printf("send QT signal to terminate the QT thread\n");
        finished();
        return;
    }

    size_t h, v, stride;
    uint8_t hsub, vsub, macropixel_size, macropixel;
    uint8_t *data;
    struct uref *uref = uref_pic_alloc(this->uref_mgr, this->ubuf_mgr, H, V);
    uref_pic_size(uref, &h, &v, &macropixel);
    uref_pic_plane_size(uref, "b8g8r8a8", &stride, &hsub, &vsub, &macropixel_size);
    uref_pic_plane_write(uref, "b8g8r8a8", 0, 0, -1, -1, &data);
    QSize S  = QSize(H, V);
    view.resize(S);

    QPalette palette = view.palette();
    palette.setBrush(QPalette::Background, Qt::transparent);
    view.page()->setPalette(palette);
    view.setAttribute(Qt::WA_OpaquePaintEvent, false);
    view.setAttribute(Qt::WA_NoSystemBackground, false);
    view.setAttribute(Qt::WA_TranslucentBackground, true);
    view.setAutoFillBackground(false);

    QImage image = QImage (data, h, v, stride, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    view.render(&painter);
    painter.end();

#if 0
    printf("%d %d %d %d \n", data[0], data[1], data[2], data[3]);
#endif

    uref_pic_plane_unmap(uref, "b8g8r8a8", 0, 0, -1, -1);
    uqueue_push(uqueue, uref);
}
