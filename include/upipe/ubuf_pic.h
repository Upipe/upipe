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
 * @short Upipe buffer handling for picture managers
 * This file defines the picture-specific API to access buffers.
 */

#ifndef _UPIPE_UBUF_PIC_H_
/** @hidden */
#define _UPIPE_UBUF_PIC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubuf.h>

#include <stdint.h>
#include <stdbool.h>

/** @This is a simple signature to make sure the ubuf_alloc internal API
 * is used properly. */
#define UBUF_ALLOC_PICTURE UBASE_FOURCC('p','i','c','t')

/** @This returns a new ubuf from a picture allocator.
 *
 * @param mgr management structure for this ubuf type
 * @param hsize horizontal size in pixels
 * @param vsize vertical size in lines
 * @return pointer to ubuf or NULL in case of failure
 */
static inline struct ubuf *ubuf_pic_alloc(struct ubuf_mgr *mgr,
                                          int hsize, int vsize)
{
    return ubuf_alloc(mgr, UBUF_ALLOC_PICTURE, hsize, vsize);
}

/** @This returns the sizes of the picture ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param hsize_p reference written with the horizontal size of the picture
 * if not NULL
 * @param vsize_p reference written with the vertical size of the picture
 * if not NULL
 * @param mpixel_p reference written with the number of pixels in a
 * macropixel if not NULL
 * @return an error code
 */
static inline int ubuf_pic_size(struct ubuf *ubuf,
                                size_t *hsize_p, size_t *vsize_p,
                                uint8_t *mpixel_p)
{
    return ubuf_control(ubuf, UBUF_SIZE_PICTURE, hsize_p, vsize_p, mpixel_p);
}

/** @This iterates on picture planes chroma types. Start by initializing
 * *chroma_p to NULL. If *chroma_p is NULL after running this function, there
 * are no more planes in this picture. Otherwise the string pointed to by
 * *chroma_p remains valid until the ubuf picture manager is deallocated.
 *
 * @param ubuf pointer to ubuf
 * @param chroma_p reference written with chroma type of the next plane
 * @return an error code
 */
static inline int ubuf_pic_iterate_plane(struct ubuf *ubuf,
                                         const char **chroma_p)
{
    return ubuf_control(ubuf, UBUF_ITERATE_PICTURE_PLANE, chroma_p);
}

/** DO NOT USE: deprecated, use ubuf_pic_iterate_plane instead  */
static inline UBASE_DEPRECATED
int ubuf_pic_plane_iterate(struct ubuf *ubuf,
                           const char **chroma_p)
{
    return ubuf_pic_iterate_plane(ubuf, chroma_p);
}

/** helper for ubuf_pic_iterate_plane */
#define ubuf_pic_foreach_plane(UBUF, CHROMA)                                \
    for (CHROMA = NULL;                                                     \
         ubase_check(ubuf_pic_iterate_plane(UBUF, &CHROMA)) &&              \
         CHROMA != NULL;)

/** @This returns the sizes of a plane of the picture ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param stride_p reference written with the offset between lines, in octets,
 * if not NULL
 * @param hsub_p reference written with the horizontal subsamping for this plane
 * if not NULL
 * @param vsub_p reference written with the vertical subsamping for this plane
 * if not NULL
 * @param macropixel_size_p reference written with the size of a macropixel in
 * octets for this plane if not NULL
 * @return an error code
 */
static inline int ubuf_pic_plane_size(struct ubuf *ubuf,
        const char *chroma, size_t *stride_p, uint8_t *hsub_p, uint8_t *vsub_p,
        uint8_t *macropixel_size_p)

{
    return ubuf_control(ubuf, UBUF_SIZE_PICTURE_PLANE, chroma,
                        stride_p, hsub_p, vsub_p, macropixel_size_p);
}

/** @internal @This checks the offset and size parameters of a lot of functions,
 * and transforms them into absolute offset and size.
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param hoffset_p reference to horizontal offset of the picture area wanted
 * in the whole picture, negative values start from the end of lines, in pixels
 * (before dividing by macropixel and hsub)
 * @param voffset_p reference to vertical offset of the picture area wanted
 * in the whole picture, negative values start from the last line, in lines
 * (before dividing by vsub)
 * @param hsize_p reference to number of pixels wanted per line, or -1 for
 * until the end of the line
 * @param vsize_p reference to number of lines wanted in the picture area,
 * or -1 for until the last line (may be NULL)
 * @return UBASE_ERR_INVALID when the parameters are invalid
 */
static inline int ubuf_pic_plane_check_offset(struct ubuf *ubuf,
       const char *chroma, int *hoffset_p, int *voffset_p,
       int *hsize_p, int *vsize_p)
{
    size_t ubuf_hsize, ubuf_vsize;
    uint8_t macropixel;
    UBASE_RETURN(ubuf_pic_size(ubuf, &ubuf_hsize, &ubuf_vsize, &macropixel))
    if (unlikely(*hoffset_p > (int)ubuf_hsize || *voffset_p > (int)ubuf_vsize ||
                 *hoffset_p + *hsize_p > (int)ubuf_hsize ||
                 *voffset_p + *vsize_p > (int)ubuf_vsize))
        return UBASE_ERR_INVALID;
    if (*hoffset_p < 0)
        *hoffset_p += ubuf_hsize;
    if (*voffset_p < 0)
        *voffset_p += ubuf_vsize;
    if (*hsize_p == -1)
        *hsize_p = ubuf_hsize - *hoffset_p;
    if (*vsize_p == -1)
        *vsize_p = ubuf_vsize - *voffset_p;
    if (unlikely(*hoffset_p % macropixel || *hsize_p % macropixel))
        return UBASE_ERR_INVALID;

    uint8_t hsub, vsub;
    UBASE_RETURN(ubuf_pic_plane_size(ubuf, chroma, NULL, &hsub, &vsub, NULL))
    if (unlikely(*hoffset_p % hsub || *hsize_p % hsub ||
                 *voffset_p % vsub || *vsize_p % vsub))
        return UBASE_ERR_INVALID;
    return UBASE_ERR_NONE;
}

/** @This returns a read-only pointer to the buffer space. You must call
 * @ref ubuf_pic_plane_unmap when you're done with the pointer.
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param hoffset horizontal offset of the picture area wanted in the whole
 * picture, negative values start from the end of lines, in pixels (before
 * dividing by macropixel and hsub)
 * @param voffset vertical offset of the picture area wanted in the whole
 * picture, negative values start from the last line, in lines (before dividing
 * by vsub)
 * @param hsize number of pixels wanted per line, or -1 for until the end of
 * the line (before dividing by macropixel and hsub)
 * @param vsize number of lines wanted in the picture area, or -1 for until the
 * last line (before deviding by vsub)
 * @param buffer_p reference written with a pointer to buffer space if not NULL
 * @return an error code
 */
static inline int ubuf_pic_plane_read(struct ubuf *ubuf,
        const char *chroma, int hoffset, int voffset, int hsize, int vsize,
        const uint8_t **buffer_p)
{
    UBASE_RETURN(ubuf_pic_plane_check_offset(ubuf, chroma, &hoffset, &voffset,
                                             &hsize, &vsize))
    return ubuf_control(ubuf, UBUF_READ_PICTURE_PLANE, chroma,
                        hoffset, voffset, hsize, vsize, buffer_p);
}

/** @This returns a writable pointer to the buffer space, if the ubuf is not
 * shared. You must call @ref ubuf_pic_plane_unmap when you're done with the
 * pointer.
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param hoffset horizontal offset of the picture area wanted in the whole
 * picture, negative values start from the end of lines, in pixels (before
 * dividing by macropixel and hsub)
 * @param voffset vertical offset of the picture area wanted in the whole
 * picture, negative values start from the last line, in lines (before dividing
 * by vsub)
 * @param hsize number of pixels wanted per line, or -1 for until the end of
 * the line
 * @param vsize number of lines wanted in the picture area, or -1 for until the
 * last line
 * @param buffer_p reference written with a pointer to buffer space if not NULL
 * @return an error code
 */
static inline int ubuf_pic_plane_write(struct ubuf *ubuf,
        const char *chroma, int hoffset, int voffset, int hsize, int vsize,
        uint8_t **buffer_p)
{
    UBASE_RETURN(ubuf_pic_plane_check_offset(ubuf, chroma, &hoffset, &voffset,
                                             &hsize, &vsize))
    return ubuf_control(ubuf, UBUF_WRITE_PICTURE_PLANE, chroma,
                        hoffset, voffset, hsize, vsize,
                        buffer_p);
}

/** @This marks the buffer space as being currently unused, and the pointer
 * will be invalid until the next time the ubuf is mapped.
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param hoffset horizontal offset of the picture area wanted in the whole
 * picture, negative values start from the end of lines, in pixels (before
 * dividing by macropixel and hsub)
 * @param voffset vertical offset of the picture area wanted in the whole
 * picture, negative values start from the last line, in lines (before dividing
 * by vsub)
 * @param hsize number of pixels wanted per line, or -1 for until the end of
 * the line
 * @param vsize number of lines wanted in the picture area, or -1 for until the
 * last line
 * @return an error code
 */
static inline int ubuf_pic_plane_unmap(struct ubuf *ubuf,
        const char *chroma, int hoffset, int voffset, int hsize, int vsize)
{
    UBASE_RETURN(ubuf_pic_plane_check_offset(ubuf, chroma, &hoffset, &voffset,
                                             &hsize, &vsize))
    return ubuf_control(ubuf, UBUF_UNMAP_PICTURE_PLANE, chroma,
                        hoffset, voffset, hsize, vsize);
}

/** @internal @This checks the skip and new_size parameters of a lot of
 * resizing functions, and transforms them.
 *
 * @param ubuf pointer to ubuf
 * @param hskip_p reference to number of pixels to skip at the beginning of
 * each line (if < 0, extend the picture leftwards)
 * @param vskip_p reference to number of lines to skip at the beginning of
 * the picture (if < 0, extend the picture upwards)
 * @param new_hsize_p reference to final horizontal size of the buffer,
 * in pixels (if set to -1, keep same line ends)
 * @param new_vsize_p reference to final vertical size of the buffer,
 * in lines (if set to -1, keep same last line)
 * @param ubuf_hsize_p filled in with the total horizontal size of the ubuf
 * (may be NULL)
 * @param ubuf_vsize_p filled in with the total vertical size of the ubuf
 * (may be NULL)
 * @param macropixel_p filled in with the number of pixels in a macropixel
 * (may be NULL)
 * @return an error code
 */
static inline int ubuf_pic_check_resize(struct ubuf *ubuf,
        int *hskip_p, int *vskip_p, int *new_hsize_p, int *new_vsize_p,
        size_t *ubuf_hsize_p, size_t *ubuf_vsize_p, uint8_t *macropixel_p)
{
    size_t ubuf_hsize, ubuf_vsize;
    uint8_t macropixel;
    UBASE_RETURN(ubuf_pic_size(ubuf, &ubuf_hsize, &ubuf_vsize, &macropixel))
    if (unlikely(*hskip_p > (int)ubuf_hsize || *vskip_p > (int)ubuf_vsize))
        return UBASE_ERR_INVALID;
    if (*new_hsize_p == -1)
        *new_hsize_p = ubuf_hsize - *hskip_p;
    if (*new_vsize_p == -1)
        *new_vsize_p = ubuf_vsize - *vskip_p;
    if (unlikely(*new_hsize_p < -*hskip_p || *new_vsize_p < -*vskip_p))
        return UBASE_ERR_INVALID;
    if (unlikely((*hskip_p < 0 && -*hskip_p % macropixel) ||
                 (*hskip_p > 0 && *hskip_p % macropixel) ||
                 *new_hsize_p % macropixel))
        return UBASE_ERR_INVALID;
    if (ubuf_hsize_p != NULL)
        *ubuf_hsize_p = ubuf_hsize;
    if (ubuf_vsize_p != NULL)
        *ubuf_vsize_p = ubuf_vsize;
    if (macropixel_p != NULL)
        *macropixel_p = macropixel;
    return UBASE_ERR_NONE;
}

/** @This splits an interlaced picture ubuf in its two fields.
 *
 * Two extra ubufs are allocated, one per field.
 *
 * @param ubuf pointer to ubuf
 * @param odd pointer to pointer to odd field ubuf
 * @param even pointer to pointer to even field ubuf
 * @return an error code
 */
static inline int ubuf_split_fields(struct ubuf *ubuf, struct ubuf **odd,
        struct ubuf **even)
{
    return ubuf_control(ubuf, UBUF_PICTURE_SPLIT_FIELDS, ubuf, odd, even);
}

/** @This resizes a picture ubuf, if possible. This will only work if:
 * @list
 * @item the ubuf is only shrunk in one or both directions, or
 * @item the relevant low-level buffer is not shared with another ubuf and the
 * picture manager allows to grow the buffer (ie. prepend/append have been
 * correctly specified at allocation, or reallocation is allowed)
 * @end list
 *
 * Should this fail, @ref ubuf_pic_replace may be used to achieve the same goal
 * with an extra buffer copy.
 *
 * @param ubuf pointer to ubuf
 * @param hskip number of pixels to skip at the beginning of each line (if < 0,
 * extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @param new_hsize final horizontal size of the buffer, in pixels (if set
 * to -1, keep same line ends)
 * @param new_vsize final vertical size of the buffer, in lines (if set
 * to -1, keep same last line)
 * @return an error code
 */
static inline int ubuf_pic_resize(struct ubuf *ubuf, int hskip, int vskip,
                                  int new_hsize, int new_vsize)
{
    UBASE_RETURN(ubuf_pic_check_resize(ubuf, &hskip, &vskip,
                                       &new_hsize, &new_vsize,
                                       NULL, NULL, NULL))
    return ubuf_control(ubuf, UBUF_RESIZE_PICTURE, hskip, vskip,
                        new_hsize, new_vsize);
}

/** @This blits a picture ubuf to another ubuf.
 *
 * @param dest destination ubuf
 * @param src source ubuf
 * @param dest_hoffset number of pixels to seek at the beginning of each line of
 * dest
 * @param dest_voffset number of lines to seek at the beginning of dest
 * @param src_hoffset number of pixels to skip at the beginning of each line of
 * src
 * @param src_voffset number of lines to skip at the beginning of src
 * @param extract_hsize horizontal size to copy
 * @param extract_vsize vertical size to copy
 * @param alpha_plane pointer to alpha plane buffer, if any
 * @param alpha_stride horizontal stride of the alpha plane buffer
 * @param alpha alpha multiplier
 * @param threshold alpha blending method
 *    0 means ignore alpha
 *    255 means blends src and dest together using alpha levels (slow)
 *    Any value in between means using the src pixels if and only if
 *      their alpha value is more than this value
 * @return an error code
 */
static inline int ubuf_pic_blit_alpha(struct ubuf *dest, struct ubuf *src,
                                int dest_hoffset, int dest_voffset,
                                int src_hoffset, int src_voffset,
                                int extract_hsize, int extract_vsize,
                                const uint8_t *alpha_plane, int alpha_stride,
                                const uint8_t alpha, const uint8_t threshold)
{
    if (alpha_plane == NULL && alpha < threshold && threshold != 0xff)
        return UBASE_ERR_NONE; /* nothing to do */

    uint8_t src_macropixel;
    UBASE_RETURN(ubuf_pic_size(src, NULL, NULL, &src_macropixel))
    uint8_t dest_macropixel;
    UBASE_RETURN(ubuf_pic_size(dest, NULL, NULL, &dest_macropixel))
    if (unlikely(dest_macropixel != src_macropixel))
        return UBASE_ERR_INVALID;

    const char *chroma;
    ubuf_pic_foreach_plane(dest, chroma) {
        size_t src_stride;
        uint8_t src_hsub, src_vsub, src_macropixel_size;
        UBASE_RETURN(ubuf_pic_plane_size(src, chroma, &src_stride,
                    &src_hsub, &src_vsub, &src_macropixel_size))

        size_t dest_stride;
        uint8_t dest_hsub, dest_vsub, dest_macropixel_size;
        UBASE_RETURN(ubuf_pic_plane_size(dest, chroma,
                     &dest_stride, &dest_hsub, &dest_vsub,
                     &dest_macropixel_size))

        if (unlikely(src_hsub != dest_hsub || src_vsub != dest_vsub ||
                     src_macropixel_size != dest_macropixel_size))
            return UBASE_ERR_INVALID;

        uint8_t *dest_buffer;
        const uint8_t *src_buffer;
        UBASE_RETURN(ubuf_pic_plane_write(dest, chroma,
                    dest_hoffset, dest_voffset,
                    extract_hsize, extract_vsize, &dest_buffer))
        int err = ubuf_pic_plane_read(src, chroma, src_hoffset, src_voffset,
                                      extract_hsize, extract_vsize,
                                      &src_buffer);
        if (unlikely(!ubase_check(err))) {
            ubuf_pic_plane_unmap(dest, chroma,
                                 dest_hoffset, dest_voffset,
                                 extract_hsize, extract_vsize);
            return err;
        }

        int plane_hsize = extract_hsize / src_hsub / src_macropixel *
                          src_macropixel_size;
        int plane_vsize = extract_vsize / src_vsub;

        for (int i = 0; i < plane_vsize; i++) {
            if ((!alpha_plane && alpha == 0xff) || threshold == 0) {
                memcpy(dest_buffer, src_buffer, plane_hsize);
            } else if (!alpha_plane) {
                for (int j = 0; j < plane_hsize; j++) {
                    dest_buffer[j] = (dest_buffer[j] * (0xff - alpha) + src_buffer[j] * alpha) / 0xff;
                }
            } else if (threshold != 0xff) {
                /* This is an on/off blending
                 * if alpha is over the threshold, we use the subpicture pixel.
                 */
                if (alpha == 0xff) {
                    for (int j = 0; j < plane_hsize; j++) {
                        const uint8_t a = alpha_plane[alpha_stride * (i * src_vsub) + j * src_hsub];
                        if (a > threshold) dest_buffer[j] = src_buffer[j];
                    }
                } else {
                    for (int j = 0; j < plane_hsize; j++) {
                        const uint8_t a = (uint16_t)alpha_plane[alpha_stride * (i * src_vsub) + j * src_hsub] * (uint16_t)alpha / 0xff;
                        if (a > threshold) dest_buffer[j] = src_buffer[j];
                    }
                }
            } else {
                /* smooth and slow blending */
                if (alpha == 0xff) {
                    for (int j = 0; j < plane_hsize; j++) {
                        const uint8_t a = alpha_plane[alpha_stride * (i * src_vsub) + j * src_hsub];
                        dest_buffer[j] = (dest_buffer[j] * (0xff - a) + src_buffer[j] * a) / 0xff;
                    }
                } else {
                    for (int j = 0; j < plane_hsize; j++) {
                        const uint8_t a = (uint16_t)alpha_plane[alpha_stride * (i * src_vsub) + j * src_hsub] * (uint16_t)alpha / 0xff;
                        dest_buffer[j] = (dest_buffer[j] * (0xff - a) + src_buffer[j] * a) / 0xff;
                    }
                }
            }
            dest_buffer += dest_stride;
            src_buffer += src_stride;
        }

        err = ubuf_pic_plane_unmap(dest, chroma,
                                   dest_hoffset, dest_voffset,
                                   extract_hsize, extract_vsize);
        UBASE_RETURN(ubuf_pic_plane_unmap(src, chroma,
                                          src_hoffset, src_voffset,
                                          extract_hsize, extract_vsize))
        UBASE_RETURN(err)
    }
    return UBASE_ERR_NONE;
}

/** @This blits a picture ubuf to another ubuf.
 *
 * @param dest destination ubuf
 * @param src source ubuf
 * @param dest_hoffset number of pixels to seek at the beginning of each line of
 * dest
 * @param dest_voffset number of lines to seek at the beginning of dest
 * @param src_hoffset number of pixels to skip at the beginning of each line of
 * src
 * @param src_voffset number of lines to skip at the beginning of src
 * @param extract_hsize horizontal size to copy
 * @param extract_vsize vertical size to copy
 * @param alpha alpha multiplier
 * @param threshold threshold parameter for alpha
 * @return an error code
 */
static inline int ubuf_pic_blit(struct ubuf *dest, struct ubuf *src,
                                int dest_hoffset, int dest_voffset,
                                int src_hoffset, int src_voffset,
                                int extract_hsize, int extract_vsize,
                                const uint8_t alpha, const uint8_t threshold)
{
    const uint8_t *alpha_plane;
    size_t alpha_stride = 0;
    int ret;

    if (!ubase_check(ubuf_pic_plane_read(src, "a8", 0, 0, -1, -1, &alpha_plane))) {
        alpha_plane = NULL;
    } else if (unlikely(!ubase_check(ubuf_pic_plane_size(src, "a8", &alpha_stride,
                            NULL, NULL, NULL)))) {
        ret = UBASE_ERR_INVALID;
        goto end;
    }

    ret = ubuf_pic_blit_alpha(dest, src, dest_hoffset, dest_voffset,
                                src_hoffset, src_voffset,
                                extract_hsize, extract_vsize,
                                alpha_plane, alpha_stride, alpha, threshold);

end:
    if (alpha_plane)
        ubuf_pic_plane_unmap(src, "a8", 0, 0, -1, -1);

    return ret;
}

/** @This copies a picture ubuf to a newly allocated ubuf, and doesn't deal
 * with the old ubuf or a dictionary.
 *
 * @param mgr management structure for this ubuf type
 * @param ubuf pointer to ubuf to copy
 * @param hskip number of pixels to skip at the beginning of each line (if < 0,
 * extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @param new_hsize final horizontal size of the buffer, in pixels (if set
 * to -1, keep same line ends)
 * @param new_vsize final vertical size of the buffer, in lines (if set
 * to -1, keep same last line)
 * @return pointer to newly allocated ubuf or NULL in case of error
 */
static inline struct ubuf *ubuf_pic_copy(struct ubuf_mgr *mgr,
                                         struct ubuf *ubuf,
                                         int hskip, int vskip,
                                         int new_hsize, int new_vsize)
{
    size_t ubuf_hsize, ubuf_vsize;
    if (unlikely(!ubase_check(ubuf_pic_check_resize(ubuf, &hskip, &vskip,
            &new_hsize, &new_vsize, &ubuf_hsize, &ubuf_vsize, NULL))))
        return NULL;

    struct ubuf *new_ubuf = ubuf_pic_alloc(mgr, new_hsize, new_vsize);
    if (unlikely(new_ubuf == NULL))
        return NULL;

    int dest_hoffset, src_hoffset;
    int dest_voffset, src_voffset;
    int extract_hsize, extract_vsize;

    if (hskip < 0) {
        dest_hoffset = -hskip;
        src_hoffset = 0;
    } else {
        dest_hoffset = 0;
        src_hoffset = hskip;
    }
    extract_hsize =
        new_hsize - dest_hoffset <= (int)ubuf_hsize - src_hoffset ?
        new_hsize - dest_hoffset : (int)ubuf_hsize - src_hoffset;

    if (vskip < 0) {
        dest_voffset = -vskip;
        src_voffset = 0;
    } else {
        dest_voffset = 0;
        src_voffset = vskip;
    }
    extract_vsize =
        new_vsize - dest_voffset <= (int)ubuf_vsize - src_voffset ?
        new_vsize - dest_voffset : (int)ubuf_vsize - src_voffset;

    if (unlikely(!ubase_check(ubuf_pic_blit(new_ubuf, ubuf,
                        dest_hoffset, dest_voffset,
                        src_hoffset, src_voffset,
                        extract_hsize, extract_vsize, 0xff, 0)))) {
        ubuf_free(new_ubuf);
        return NULL;
    }

    return new_ubuf;
}

/** @This copies part of a ubuf to a newly allocated ubuf, and replaces the
 * old ubuf with the new ubuf.
 *
 * @param mgr management structure for this ubuf type
 * @param ubuf_p reference to a pointer to ubuf to replace with a new picture
 * ubuf
 * @param hskip number of pixels to skip at the beginning of each line (if < 0,
 * extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @param new_hsize final horizontal size of the buffer, in pixels (if set
 * to -1, keep same line ends)
 * @param new_vsize final vertical size of the buffer, in lines (if set
 * to -1, keep same last line)
 * @return an error code
 */
static inline int ubuf_pic_replace(struct ubuf_mgr *mgr, struct ubuf **ubuf_p,
                                   int hskip, int vskip,
                                   int new_hsize, int new_vsize)
{
    struct ubuf *new_ubuf = ubuf_pic_copy(mgr, *ubuf_p, hskip, vskip,
                                          new_hsize, new_vsize);
    if (unlikely(new_ubuf == NULL))
        return UBASE_ERR_ALLOC;

    ubuf_free(*ubuf_p);
    *ubuf_p = new_ubuf;
    return UBASE_ERR_NONE;
}

/** @This clears (part of) the specified plane, depending on plane type
 * and size (set U/V chroma to 0x80 instead of 0 for instance)
 *
 * @param ubuf pointer to ubuf
 * @param chroma chroma type (see chroma reference)
 * @param hoffset horizontal offset of the picture area wanted in the whole
 * picture, negative values start from the end of lines, in pixels (before
 * dividing by macropixel and hsub)
 * @param voffset vertical offset of the picture area wanted in the whole
 * picture, negative values start from the last line, in lines (before dividing
 * by vsub)
 * @param hsize number of pixels wanted per line, or -1 for until the end of
 * the line
 * @param vsize number of lines wanted in the picture area, or -1 for until the
 * last line
 * @return an error code
 */
int ubuf_pic_plane_clear(struct ubuf *ubuf, const char *chroma,
                         int hoffset, int voffset, int hsize, int vsize,
                         int fullrange);

/** @This clears (part of) the specified picture, depending on plane type
 * and size (set U/V chroma to 0x80 instead of 0 for instance)
 *
 * @param ubuf pointer to ubuf
 * @param hoffset horizontal offset of the picture area wanted in the whole
 * picture, negative values start from the end of lines, in pixels (before
 * dividing by macropixel and hsub)
 * @param voffset vertical offset of the picture area wanted in the whole
 * picture, negative values start from the last line, in lines (before dividing
 * by vsub)
 * @param hsize number of pixels wanted per line, or -1 for until the end of
 * the line
 * @param vsize number of lines wanted in the picture area, or -1 for until the
 * last line
 * @param fullrange whether the input is full-range
 * @return an error code
 */
int ubuf_pic_clear(struct ubuf *ubuf, int hoffset, int voffset,
                   int hsize, int vsize, int fullrange);

/** @This converts 8 bits RGB color to 8 bits YUV.
 *
 * @param rgb RGB color to convert
 * @param fullrange use full range if not 0
 * @param yuv filled with the converted YUV color
 */
void ubuf_pic_rgb_to_yuv(const uint8_t rgb[3], int fullrange, uint8_t yuv[3]);

/** @This converts 8 bits RGBA color to 8 bits YUVA.
 *
 * @param rgba RGBA color to convert
 * @param fullrange use full range if not 0
 * @param yuva filled with the converted YUVA color
 */
static inline void ubuf_pic_rgba_to_yuva(const uint8_t rgba[4],
                                         int fullrange, uint8_t yuva[4])
{
    ubuf_pic_rgb_to_yuv(rgba, fullrange, yuva);
    yuva[3] = rgba[3];
}

/** @This parses a 8 bits RGB value.
 *
 * @param value value to parse
 * @param rgb filled with the parsed value
 * @return an error code
 */
int ubuf_pic_parse_rgb(const char *value, uint8_t rgb[3]);

/** @This parses a 8 bits RGBA value.
 *
 * @param value value to parse
 * @param rgba filled with the parsed value
 * @return an error code
 */
int ubuf_pic_parse_rgba(const char *value, uint8_t rgba[4]);

#ifdef __cplusplus
}
#endif
#endif
