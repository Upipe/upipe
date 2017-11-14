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

#include <QImage>
#include <QPaintDevice>
#include <QWebFrame>
#include <QTimer>
#include <QCoreApplication>
#include <QObject>
#include <QWebView>
#include <QWebPage>
#include <QtWebKit>
#include <QUrl>
#include <upipe/ubase.h>
#include <upipe/uatomic.h>
#include <upipe/uprobe.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_pic.h>
#include <upipe/uref_pic_flow.h>
#include <upipe/uref_sound.h>
#include <upipe/uref_sound_flow.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/ufifo.h>
#include <upipe/uqueue.h>

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

/** @This @internal is the Thumbnail class description
 *
 */
class Thumbnail : public QObject
{
    Q_OBJECT

public:
    Thumbnail(const char *url);
    void seturefmgr(struct uref_mgr *uref_mgr);
    void setubufmgr(struct ubuf_mgr *ubuf_mgr);
    void setuqueue(struct uqueue *uqueue);
    void setuqueue2(struct uqueue *uqueue2);
    void seturl(const char* url);
    void setH(int H);
    void setV(int V);

signals:
    void finished();

private slots:
    void render();

private:
    QWebView view;
    const char *url;
    struct uref_mgr *uref_mgr;
    struct ubuf_mgr *ubuf_mgr;
    struct uqueue *uqueue;
    struct uqueue *uqueue2;
    int H;
    int V;
};
