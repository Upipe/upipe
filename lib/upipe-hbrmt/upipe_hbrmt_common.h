#ifndef _UPIPE_MODULES_UPIPE_HBRMT_COMMON_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_HBRMT_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

struct sdi_line_range {
    uint16_t start;
    uint16_t end;
};

struct sdi_picture_fmt {
    bool sd;
    
    /* Active picture dimensions */
    uint16_t active_width;
    uint16_t active_height;
    
    /* Offset between fields */
    uint16_t field_offset;

    /* SMPTE RP168 Switch Line */
    uint16_t switching_line;

    /* SMPTE 352 Payload ID line */
    uint16_t payload_id_line;

    /* Field 1 (interlaced) or Frame (progressive) line ranges */
    struct sdi_line_range vbi_f1_part1;
    struct sdi_line_range active_f1;
    struct sdi_line_range vbi_f1_part2;

    /* Field 2 (interlaced)  */
    struct sdi_line_range vbi_f2_part1;
    struct sdi_line_range active_f2;
    struct sdi_line_range vbi_f2_part2;
};

struct sdi_offsets_fmt {
    /* Full SDI width and height */
    uint16_t width;
    uint16_t height;
 
    /* Number of samples (pairs) between SAV and start of active data */
    uint16_t active_offset;

    const struct sdi_picture_fmt *pict_fmt;

    /* 0x0 (Interlace), 0x1 (Segmented frame), 0x3 (Progressive) */
    uint8_t psf_ident;

    /* frame_rate
     * 0x0 Undefined
     * 0x1 Reserved
     * 0x2 24/1.001Hz
     * 0x3 24Hz
     * 0x4 Reserved
     * 0x5 25Hz
     * 0x6 30/1.001 Hz
     * 0x7 30Hz
     * 0x8 Reserved
     * 0x9 50Hz
     * 0xA 60/1.001 Hz
     */
    uint8_t frame_rate;

    struct urational fps;
};

static inline const struct sdi_offsets_fmt *sdi_get_offsets(struct uref *flow_def)
{
    struct urational fps;

    if (!ubase_check(uref_pic_flow_get_fps(flow_def, &fps)))
        return NULL;

    uint64_t hsize, vsize;
    if (!ubase_check(uref_pic_flow_get_hsize(flow_def, &hsize)) ||
        !ubase_check(uref_pic_flow_get_vsize(flow_def, &vsize)))
        return NULL;

    static const struct sdi_picture_fmt pict_fmts[3] = {
        /* 1125 Interlaced (1080 active) lines */
        {0, 1920, 1080, 562, 7, 10, {1, 20}, {21, 560}, {561, 563}, {564, 583}, {584, 1123}, {1124, 1125}},
        /* 1125 Progressive (1080 active) lines */
        {0, 1920, 1080, 0, 7, 10, {1, 41}, {42, 1121}, {1122, 1125}, {0, 0}, {0, 0}, {0, 0}},

        /* PAL */
        {1, 720, 576, 313, 6, 9, {1, 22}, {23, 310}, {311, 312}, {313, 335}, {336, 623}, {624, 625}},
    };

    static const struct sdi_offsets_fmt fmts_data[7] = {
        { 2640, 1125, 720, &pict_fmts[0], 0x0, 0x5, { 25, 1} },        /* 25 Hz I */
        { 2640, 1125, 720, &pict_fmts[1], 0x3, 0x9, { 50, 1} },        /* 50 Hz P */

        { 2200, 1125, 280, &pict_fmts[0], 0x0, 0x6, { 30000, 1001 } }, /* 30/1.001 Hz I */
        { 2200, 1125, 280, &pict_fmts[1], 0x3, 0xA, { 60000, 1001 } }, /* 60/1.001 Hz P */

        { 2750, 1125, 830, &pict_fmts[0], 0x3, 0x2, { 24000, 1001 } }, /* 24/1.001 Hz */
        { 2750, 1125, 830, &pict_fmts[0], 0x3, 0x3, { 24, 1 } },       /* 24 Hz */

        {  864,  625, 144, &pict_fmts[2], 0x0, 0x5, { 25, 1} },        /* 625-line 25 Hz I */
    };

    for (size_t i = 0; i < sizeof(fmts_data) / sizeof(struct sdi_offsets_fmt); i++)
        if (!urational_cmp(&fps, &fmts_data[i].fps))
            if (fmts_data[i].pict_fmt->active_width == hsize)
                if (fmts_data[i].pict_fmt->active_height == vsize)
                    return &fmts_data[i];

    return NULL;
}

#ifdef __cplusplus
}
#endif
#endif
