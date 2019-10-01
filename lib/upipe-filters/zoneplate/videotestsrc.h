#ifndef _VIDEOTESTSRC_H_
/** @hidden */
#define _VIDEOTESTSRC_H_

void
gst_video_test_src_zoneplate_8bit (uint8_t *data,
        int w, int h, size_t stride, int t);

void
gst_video_test_src_zoneplate_10bit (uint16_t *data,
        int w, int h, size_t stride, int t);

#endif
