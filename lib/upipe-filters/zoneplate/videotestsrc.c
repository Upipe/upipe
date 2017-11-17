/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdint.h>
#include <stdlib.h>

#include "videotestsrc.h"

#define V_POINTER_K0      0
#define V_POINTER_KX      0
#define V_POINTER_KY      0
#define V_POINTER_KT      1
#define V_POINTER_KXT     0
#define V_POINTER_KYT     0
#define V_POINTER_KXY     0
#define V_POINTER_KX2     20
#define V_POINTER_KY2     20
#define V_POINTER_KT2     0
#define V_POINTER_XOFFSET 0
#define V_POINTER_YOFFSET 0

static const uint8_t sine_table[256] = {
  128, 131, 134, 137, 140, 143, 146, 149,
  152, 156, 159, 162, 165, 168, 171, 174,
  176, 179, 182, 185, 188, 191, 193, 196,
  199, 201, 204, 206, 209, 211, 213, 216,
  218, 220, 222, 224, 226, 228, 230, 232,
  234, 236, 237, 239, 240, 242, 243, 245,
  246, 247, 248, 249, 250, 251, 252, 252,
  253, 254, 254, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 254, 254,
  253, 252, 252, 251, 250, 249, 248, 247,
  246, 245, 243, 242, 240, 239, 237, 236,
  234, 232, 230, 228, 226, 224, 222, 220,
  218, 216, 213, 211, 209, 206, 204, 201,
  199, 196, 193, 191, 188, 185, 182, 179,
  176, 174, 171, 168, 165, 162, 159, 156,
  152, 149, 146, 143, 140, 137, 134, 131,
  128, 124, 121, 118, 115, 112, 109, 106,
  103, 99, 96, 93, 90, 87, 84, 81,
  79, 76, 73, 70, 67, 64, 62, 59,
  56, 54, 51, 49, 46, 44, 42, 39,
  37, 35, 33, 31, 29, 27, 25, 23,
  21, 19, 18, 16, 15, 13, 12, 10,
  9, 8, 7, 6, 5, 4, 3, 3,
  2, 1, 1, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 1, 1,
  2, 3, 3, 4, 5, 6, 7, 8,
  9, 10, 12, 13, 15, 16, 18, 19,
  21, 23, 25, 27, 29, 31, 33, 35,
  37, 39, 42, 44, 46, 49, 51, 54,
  56, 59, 62, 64, 67, 70, 73, 76,
  79, 81, 84, 87, 90, 93, 96, 99,
  103, 106, 109, 112, 115, 118, 121, 124
};

void
gst_video_test_src_zoneplate_8bit (uint8_t *data,
        int w, int h, size_t stride, int t)
{
  int i;
  int j;
  int xreset = -(w / 2) - V_POINTER_XOFFSET;   /* starting values for x^2 and y^2, centering the ellipse */
  int yreset = -(h / 2) - V_POINTER_YOFFSET;

  int x, y;
  int accum_kx;
  int accum_kxt;
  int accum_ky;
  int accum_kyt;
  int accum_kxy;
  int kt;
  int kt2;
  int ky2;
  int delta_kxt = V_POINTER_KXT * t;
  int delta_kxy;
  int scale_kxy = 0xffff / (w / 2);
  int scale_kx2 = 0xffff / w;

  /* Zoneplate equation:
   *
   * phase = k0 + kx*x + ky*y + kt*t
   *       + kxt*x*t + kyt*y*t + kxy*x*y
   *       + kx2*x*x + ky2*y*y + Kt2*t*t
   */

#if 0
  for (j = 0, y = yreset; j < h; j++, y++) {
    for (i = 0, x = xreset; i < w; i++, x++) {

      /* zero order */
      int phase = v->k0;

      /* first order */
      phase = phase + (v->kx * i) + (v->ky * j) + (v->kt * t);

      /* cross term */
      /* phase = phase + (v->kxt * i * t) + (v->kyt * j * t); */
      /* phase = phase + (v->kxy * x * y) / (w/2); */

      /*second order */
      /*normalise x/y terms to rate of change of phase at the picture edge */
      phase =
          phase + ((v->kx2 * x * x) / w) + ((v->ky2 * y * y) / h) +
          ((v->kt2 * t * t) >> 1);

      color.Y = sine_table[phase & 0xff];

      color.R = color.Y;
      color.G = color.Y;
      color.B = color.Y;
      p->paint_tmpline (p, i, 1);
    }
  }
#endif

  /* optimised version, with original code shown in comments */
  accum_ky = 0;
  accum_kyt = 0;
  kt = V_POINTER_KT * t;
  kt2 = V_POINTER_KT2 * t * t;
  for (j = 0, y = yreset; j < h; j++, y++) {
    accum_kx = 0;
    accum_kxt = 0;
    accum_ky += V_POINTER_KY;
    accum_kyt += V_POINTER_KYT * t;
    delta_kxy = V_POINTER_KXY * y * scale_kxy;
    accum_kxy = delta_kxy * xreset;
    ky2 = (V_POINTER_KY2 * y * y) / h;
    for (i = 0, x = xreset; i < w; i++, x++) {

      /* zero order */
      int phase = V_POINTER_K0;

      /* first order */
      accum_kx += V_POINTER_KX;
      /* phase = phase + (v->kx * i) + (v->ky * j) + (v->kt * t); */
      phase = phase + accum_kx + accum_ky + kt;

      /* cross term */
      accum_kxt += delta_kxt;
      accum_kxy += delta_kxy;
      /* phase = phase + (v->kxt * i * t) + (v->kyt * j * t); */
      phase = phase + accum_kxt + accum_kyt;

      /* phase = phase + (v->kxy * x * y) / (w/2); */
      /* phase = phase + accum_kxy / (w/2); */
      phase = phase + (accum_kxy >> 16);

      /*second order */
      /*normalise x/y terms to rate of change of phase at the picture edge */
      /*phase = phase + ((v->kx2 * x * x)/w) + ((v->ky2 * y * y)/h) + ((v->kt2 * t * t)>>1); */
      phase = phase + ((V_POINTER_KX2 * x * x * scale_kx2) >> 16) + ky2 + (kt2 >> 1);

      data[j*stride + i] = sine_table[phase & 0xff];
    }
  }
}


void
gst_video_test_src_zoneplate_10bit (uint16_t *data,
        int w, int h, size_t stride, int t)
{
  int i;
  int j;
  int xreset = -(w / 2) - V_POINTER_XOFFSET;   /* starting values for x^2 and y^2, centering the ellipse */
  int yreset = -(h / 2) - V_POINTER_YOFFSET;

  int x, y;
  int accum_kx;
  int accum_kxt;
  int accum_ky;
  int accum_kyt;
  int accum_kxy;
  int kt;
  int kt2;
  int ky2;
  int delta_kxt = V_POINTER_KXT * t;
  int delta_kxy;
  int scale_kxy = 0xffff / (w / 2);
  int scale_kx2 = 0xffff / w;

  stride /= 2;

  accum_ky = 0;
  accum_kyt = 0;
  kt = V_POINTER_KT * t;
  kt2 = V_POINTER_KT2 * t * t;
  for (j = 0, y = yreset; j < h; j++, y++) {
    accum_kx = 0;
    accum_kxt = 0;
    accum_ky += V_POINTER_KY;
    accum_kyt += V_POINTER_KYT * t;
    delta_kxy = V_POINTER_KXY * y * scale_kxy;
    accum_kxy = delta_kxy * xreset;
    ky2 = (V_POINTER_KY2 * y * y) / h;
    for (i = 0, x = xreset; i < w; i++, x++) {

      /* zero order */
      int phase = V_POINTER_K0;

      /* first order */
      accum_kx += V_POINTER_KX;
      phase = phase + accum_kx + accum_ky + kt;

      /* cross term */
      accum_kxt += delta_kxt;
      accum_kxy += delta_kxy;
      phase = phase + accum_kxt + accum_kyt;

      phase = phase + (accum_kxy >> 16);

      /*second order */
      /*normalise x/y terms to rate of change of phase at the picture edge */
      phase = phase + ((V_POINTER_KX2 * x * x * scale_kx2) >> 16) + ky2 + (kt2 >> 1);

      data[j*stride + i] = sine_table[phase & 0xff] << 2;
    }
  }
}
