/*
 * Copyright (C) 2023 Clément Vasseur <clement.vasseur@gmail.com>
 *
 * Authors: Clément Vasseur
 *
 * SPDX-License-Identifier: MIT
 */

#include "upipe/ubase.h"
#include "upipe/uref_pic_flow.h"

/** @This associates a numeric value to the corresponding string attribute. */
struct value_desc {
    int value;
    const char *name;
};

/** @This lists colour primaries values */
static const struct value_desc colour_primaries[] = {
    {  1, "bt709" },
    {  2, NULL },
    {  4, "bt470m" },
    {  5, "bt470bg" },
    {  6, "smpte170m" },
    {  7, "smpte240m" },
    {  8, "film" },
    {  9, "bt2020" },
    { 10, "smpte428" },
    { 11, "smpte431" },
    { 12, "smpte432" },
    { 22, "ebu3213" },
};

/** @This lists transfer characteristics values */
static const struct value_desc transfer_characteristics[] = {
    {  1, "bt709" },
    {  2, NULL },
    {  4, "bt470m" },
    {  5, "bt470bg" },
    {  6, "smpte170m" },
    {  7, "smpte240m" },
    {  8, "linear" },
    {  9, "log100" },
    { 10, "log316" },
    { 11, "iec61966-2-4" },
    { 12, "bt1361e" },
    { 13, "iec61966-2-1" },
    { 14, "bt2020-10" },
    { 15, "bt2020-12" },
    { 16, "smpte2084" },
    { 17, "smpte428" },
    { 18, "arib-std-b67" },
};

/** @This lists matrix coefficients values */
static const struct value_desc matrix_coefficients[] = {
    {  0, "GBR" },
    {  1, "bt709" },
    {  2, NULL },
    {  4, "fcc" },
    {  5, "bt470bg" },
    {  6, "smpte170m" },
    {  7, "smpte240m" },
    {  8, "YCgCo" },
    {  9, "bt2020nc" },
    { 10, "bt2020c" },
    { 11, "smpte2085" },
    { 12, "chroma-nc" },
    { 13, "chroma-c" },
    { 14, "ictcp" },
};

#define UREF_PIC_FLOW_VALUE_CONVERT(Name)                                   \
int uref_pic_flow_set_##Name##_val(struct uref *flow_def, int value)        \
{                                                                           \
    for (int i = 0; i < UBASE_ARRAY_SIZE(Name); i++)                        \
        if (Name[i].value == value)                                         \
            return Name[i].name != NULL ?                                   \
                   uref_pic_flow_set_##Name(flow_def, Name[i].name) :       \
                   (uref_pic_flow_delete_##Name(flow_def), UBASE_ERR_NONE); \
                                                                            \
    return UBASE_ERR_UNHANDLED;                                             \
}                                                                           \
                                                                            \
int uref_pic_flow_get_##Name##_val(struct uref *flow_def, int *value)       \
{                                                                           \
    const char *name;                                                       \
    if (!ubase_check(uref_pic_flow_get_##Name(flow_def, &name))) {          \
        *value = 2;                                                         \
        return UBASE_ERR_NONE;                                              \
    }                                                                       \
                                                                            \
    for (int i = 0; i < UBASE_ARRAY_SIZE(Name); i++)                        \
        if (Name[i].name != NULL && !strcmp(Name[i].name, name)) {          \
            *value = Name[i].value;                                         \
            return UBASE_ERR_NONE;                                          \
        }                                                                   \
                                                                            \
    return UBASE_ERR_INVALID;                                               \
}

UREF_PIC_FLOW_VALUE_CONVERT(colour_primaries)
UREF_PIC_FLOW_VALUE_CONVERT(transfer_characteristics)
UREF_PIC_FLOW_VALUE_CONVERT(matrix_coefficients)

/** @This gets the bit depth from flow def attributes.
 *
 * @param flow_def flow definition packet
 * @param p_bit_depth pointer to returned bit depth
 * @return an error code
 */
int uref_pic_flow_get_bit_depth(struct uref *flow_def, int *p_bit_depth)
{
    const char *chroma;
    UBASE_RETURN(uref_pic_flow_get_chroma(flow_def, &chroma, 0))

    char c;
    unsigned bit_depth;
    if (sscanf(chroma, "%c%u", &c, &bit_depth) != 2)
        return UBASE_ERR_INVALID;

    if (p_bit_depth)
        *p_bit_depth = bit_depth;

    return UBASE_ERR_NONE;
}

/** @This checks whether the flow definition conforms
 * to the SDR format.
 *
 * @param flow_def flow definition packet
 * @return an error code
 */
int uref_pic_flow_check_sdr(struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF))

    int trc;
    UBASE_RETURN(uref_pic_flow_get_transfer_characteristics_val(flow_def, &trc))

    if (trc != 1 && trc != 6 && trc != 13 && trc != 14 && trc != 15)
        return UBASE_ERR_INVALID;

    return UBASE_ERR_NONE;
}

/** @This checks whether the flow definition conforms
 * to the HLG format.
 *
 * @param flow_def flow definition packet
 * @return an error code
 */
int uref_pic_flow_check_hlg(struct uref *flow_def)
{
    const char *val;
    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF))

    /* ARIB STD-B67 (HLG) transfer function */
    UBASE_RETURN(uref_pic_flow_get_transfer_characteristics(flow_def, &val))
    if (strcmp(val, "arib-std-b67"))
        return UBASE_ERR_INVALID;

    /* BT.2020 color primaries */
    UBASE_RETURN(uref_pic_flow_get_colour_primaries(flow_def, &val))
    if (strcmp(val, "bt2020"))
        return UBASE_ERR_INVALID;

    /* bit depth of 10-bit */
    int bit_depth;
    UBASE_RETURN(uref_pic_flow_get_bit_depth(flow_def, &bit_depth))
    if (bit_depth != 10)
        return UBASE_ERR_INVALID;

    return UBASE_ERR_NONE;
}

/** @This checks whether the flow definition conforms
 * to the PQ10 format.
 *
 * @param flow_def flow definition packet
 * @return an error code
 */
int uref_pic_flow_check_pq10(struct uref *flow_def)
{
    const char *val;
    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF))

    /* SMPTE ST 2084 (PQ) transfer function */
    UBASE_RETURN(uref_pic_flow_get_transfer_characteristics(flow_def, &val))
    if (strcmp(val, "smpte2084"))
        return UBASE_ERR_INVALID;

    /* BT.2020 color primaries */
    UBASE_RETURN(uref_pic_flow_get_colour_primaries(flow_def, &val))
    if (strcmp(val, "bt2020"))
        return UBASE_ERR_INVALID;

    /* bit depth of 10-bit */
    int bit_depth;
    UBASE_RETURN(uref_pic_flow_get_bit_depth(flow_def, &bit_depth))
    if (bit_depth != 10)
        return UBASE_ERR_INVALID;

    return UBASE_ERR_NONE;
}

/** @This checks whether the flow definition conforms
 * to the HDR10 Media Profile.
 *
 * @param flow_def flow definition packet
 * @return an error code
 */
int uref_pic_flow_check_hdr10(struct uref *flow_def)
{
    UBASE_RETURN(uref_flow_match_def(flow_def, UREF_PIC_FLOW_DEF))

    /* SMPTE ST 2086 (mastering display color volume) */
    UBASE_RETURN(uref_pic_flow_get_mastering_display(flow_def, NULL))

    /* MaxFALL (maximum frame-average light level) */
    UBASE_RETURN(uref_pic_flow_get_max_fall(flow_def, NULL))

    /* MaxCLL (maximum content light level) */
    UBASE_RETURN(uref_pic_flow_get_max_cll(flow_def, NULL))

    return uref_pic_flow_check_pq10(flow_def);
}
