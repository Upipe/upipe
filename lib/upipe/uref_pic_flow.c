/*
 * Copyright (C) 2023 Clément Vasseur <clement.vasseur@gmail.com>
 *
 * Authors: Clément Vasseur
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
