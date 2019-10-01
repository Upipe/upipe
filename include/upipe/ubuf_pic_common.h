/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
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
 * @short Upipe useful common definitions for picture managers
 */

#ifndef _UPIPE_UBUF_PIC_COMMON_H_
/** @hidden */
#define _UPIPE_UBUF_PIC_COMMON_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ubuf.h>

#include <stdint.h>
#include <stdbool.h>

/** @This is a sub-structure of @ref ubuf_pic_common, pointing to the buffer
 * space of individual planes. It is only needed to allocate and init it
 * if you plan to use @ref ubuf_pic_common_plane_map. */
struct ubuf_pic_common_plane {
    /** pointer to buffer space */
    uint8_t *buffer;
    /** horizontal stride */
    size_t stride;
};

/** @This is a proposed common section of picture ubuf, allowing to window
 * data. In an opaque area you would typically store a pointer to shared
 * buffer space.
 *
 * Since it features a flexible array member, it must be placed at the end of
 * another structure. */
struct ubuf_pic_common {
    /** extra macropixels added before lines */
    size_t hmprepend;
    /** extra macropixels added after lines */
    size_t hmappend;
    /** requested horizontal number of macropixels */
    size_t hmsize;
    /** extra lines added before buffer */
    size_t vprepend;
    /** extra lines added after buffer */
    size_t vappend;
    /** requested vertical number of lines */
    size_t vsize;

    /** common structure */
    struct ubuf ubuf;

    /** planes buffers */
    struct ubuf_pic_common_plane planes[];
};

/** @This is a sub-structure of @ref ubuf_pic_common_mgr, describing the
 * allocation of individual planes. */
struct ubuf_pic_common_mgr_plane {
    /** chroma type */
    char *chroma;
    /** horizontal subsampling */
    uint8_t hsub;
    /** vertical subsampling */
    uint8_t vsub;
    /** macropixel size */
    uint8_t macropixel_size;
};

/** @This is a super-set of the ubuf_mgr structure with additional local
 * members, common to picture managers. */
struct ubuf_pic_common_mgr {
    /** number of pixels in a macropixel */
    uint8_t macropixel;
    /** number of planes to allocate */
    uint8_t nb_planes;
    /** planes description */
    struct ubuf_pic_common_mgr_plane **planes;

    /** common management structure */
    struct ubuf_mgr mgr;
};

UBASE_FROM_TO(ubuf_pic_common, ubuf, ubuf, ubuf)
UBASE_FROM_TO(ubuf_pic_common_mgr, ubuf_mgr, ubuf_mgr, mgr)

/** @internal @This returns the plane number corresponding to a chroma.
 *
 * @param mgr common management structure
 * @param chroma chroma type
 * @return number of the plane, or -1 if not found
 */
static inline int ubuf_pic_common_plane(struct ubuf_mgr *mgr,
                                        const char *chroma)
{
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(mgr);
    for (int i = 0; i < common_mgr->nb_planes; i++)
        if (!strcmp(common_mgr->planes[i]->chroma, chroma))
            return i;
    return -1;
}

/** @This returns the number of extra octets needed when allocating a picture
 * ubuf. It is only necessary to allocate them if you plan to use
 * @ref ubuf_pic_common_plane_map.
 *
 * @param mgr description structure of the ubuf manager
 * @return number of extra octets needed
 */
static inline size_t ubuf_pic_common_sizeof(struct ubuf_mgr *mgr)
{
    struct ubuf_pic_common_mgr *common_mgr =
        ubuf_pic_common_mgr_from_ubuf_mgr(mgr);
    return sizeof(struct ubuf_pic_common_plane) * common_mgr->nb_planes;
}

/** @This initializes the common fields of a picture ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param hmprepend extra macropixels added before each line
 * @param hmappend extra macropixels added after each line
 * @param hmsize number of macropixels in a line, excluding hmprepend and
 * hmappend
 * @param vprepend extra lines added before the first line
 * @param vappend extra lines added after the first line
 * @param vsize number of lines, excluding vprepend and vappend
 */
static inline void ubuf_pic_common_init(struct ubuf *ubuf,
                                        size_t hmprepend, size_t hmappend,
                                        size_t hmsize,
                                        size_t vprepend, size_t vappend,
                                        size_t vsize)
{
    struct ubuf_pic_common *common = ubuf_pic_common_from_ubuf(ubuf);
    common->hmprepend = hmprepend;
    common->hmappend = hmappend;
    common->hmsize = hmsize;
    common->vprepend = vprepend;
    common->vappend = vappend;
    common->vsize = vsize;
    uchain_init(&ubuf->uchain);
}

/** @This cleans up the common fields of a picture ubuf (currently no-op).
 *
 * @param ubuf pointer to ubuf
 */
static inline void ubuf_pic_common_clean(struct ubuf *ubuf)
{
}

/** @This initializes a plane sub-structure of a picture ubuf. It is only
 * necessary to call this function if you plan to use
 * @ref ubuf_pic_common_plane_map.
 *
 * @param ubuf pointer to ubuf
 * @param plane index of the plane
 * @param buffer pointer to memory buffer (in front of vprepend and hmprepend)
 * @param stride line stride
 */
static inline void ubuf_pic_common_plane_init(struct ubuf *ubuf, uint8_t plane,
                                              uint8_t *buffer, size_t stride)
{
    struct ubuf_pic_common *common = ubuf_pic_common_from_ubuf(ubuf);
    common->planes[plane].buffer = buffer;
    common->planes[plane].stride = stride;
}

/** @This cleans up a plane sub-structure of a picture ubuf (currently no-op).
 *
 * @param ubuf pointer to ubuf
 * @param plane index of the plane
 */
static inline void ubuf_pic_common_plane_clean(struct ubuf *ubuf, uint8_t plane)
{
}

/** @This checks whether the requested picture size can be allocated with the
 * manager.
 *
 * @param mgr common management structure
 * @param hsize horizontal size in pixels
 * @param vsize vertical size in lines
 * @return an error code
 */
int ubuf_pic_common_check_size(struct ubuf_mgr *mgr, int hsize, int vsize);

/** @This duplicates the content of the common structure for picture ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @return an error code
 */
int ubuf_pic_common_dup(struct ubuf *ubuf, struct ubuf *new_ubuf);

/** @This duplicates the content of the plane sub-structure for picture ubuf.
 * It is only necessary to call this function if you plan to use
 * @ref ubuf_pic_common_plane_map.
 *
 * @param ubuf pointer to ubuf
 * @param new_ubuf pointer to ubuf to overwrite
 * @param plane index of the plane
 * @return an error code
 */
int ubuf_pic_common_plane_dup(struct ubuf *ubuf, struct ubuf *new_ubuf,
                              uint8_t plane);

/** @This returns the sizes of the picture ubuf.
 *
 * @param ubuf pointer to ubuf
 * @param hsize_p reference written with the horizontal size of the picture
 * if not NULL
 * @param vsize_p reference written with the vertical size of the picture
 * if not NULL
 * @param macropixel_p reference written with the number of pixels in a
 * macropixel if not NULL
 * @return an error code
 */
int ubuf_pic_common_size(struct ubuf *ubuf, size_t *hsize_p, size_t *vsize_p,
                         uint8_t *macropixel_p);

/** @This iterates on picture planes chroma types. Start by initializing
 * *chroma_p to NULL. If *chroma_p is NULL after running this function, there
 * are no more planes in this picture. Otherwise the string pointed to by
 * *chroma_p remains valid until the ubuf picture manager is deallocated.
 *
 * @param ubuf pointer to ubuf
 * @param chroma_p reference written with chroma type of the next plane
 * @return an error code
 */
int ubuf_pic_common_iterate_plane(struct ubuf *ubuf, const char **chroma_p);

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
int ubuf_pic_common_plane_size(struct ubuf *ubuf, const char *chroma,
                               size_t *stride_p,
                               uint8_t *hsub_p, uint8_t *vsub_p,
                               uint8_t *macropixel_size_p);

/** @This returns a pointer to the buffer space of a plane.
 *
 * To use this function, @ref ubuf_pic_common_plane must be allocated and
 * correctly filled in.
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
int ubuf_pic_common_plane_map(struct ubuf *ubuf, const char *chroma,
                              int hoffset, int voffset, int hsize, int vsize,
                              uint8_t **buffer_p);

/** @This checks whether the requested picture resize can be performed with
 * this manager.
 *
 * @param mgr common management structure
 * @param hskip number of pixels to skip at the beginning of each line (if < 0,
 * extend the picture leftwards)
 * @param vskip number of lines to skip at the beginning of the picture (if < 0,
 * extend the picture upwards)
 * @return an error code
 */
int ubuf_pic_common_check_skip(struct ubuf_mgr *mgr, int hskip, int vskip);

/** @This splits an interlaced picture ubuf in its two fields.
 *
 * Two extra ubufs are allocated, one per field.
 *
 * @param ubuf pointer to ubuf
 * @param odd pointer to pointer to odd field ubuf
 * @param even pointer to pointer to even field ubuf
 * @return an error code
*/
int ubuf_pic_common_split_fields(struct ubuf *ubuf, struct ubuf **odd,
        struct ubuf **even);

/** @This resizes a picture ubuf, if the ubuf has enough space, and it is not
 * shared.
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
int ubuf_pic_common_resize(struct ubuf *ubuf, int hskip, int vskip,
                           int new_hsize, int new_vsize);

/** @This frees memory allocated by @ref ubuf_pic_common_mgr_init and
 * @ref ubuf_pic_common_mgr_add_plane.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a ubuf_pic_common_mgr
 */
void ubuf_pic_common_mgr_clean(struct ubuf_mgr *mgr);

/** @This allocates a new instance of the ubuf manager for picture formats.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a ubuf_pic_common_mgr
 * @param macropixel number of pixels in a macropixel (typically 1)
 */
void ubuf_pic_common_mgr_init(struct ubuf_mgr *mgr, uint8_t macropixel);

/** @This adds a new plane to a ubuf manager for picture formats.
 *
 * @param mgr pointer to a ubuf_mgr structure contained in a ubuf_pic_common_mgr
 * @param chroma chroma type (see chroma reference)
 * @param hsub horizontal subsamping
 * @param vsub vertical subsamping
 * @param macropixel_size size of a macropixel in octets
 * @return an error code
 */
int ubuf_pic_common_mgr_add_plane(struct ubuf_mgr *mgr, const char *chroma,
                                  uint8_t hsub, uint8_t vsub,
                                  uint8_t macropixel_size);

#ifdef __cplusplus
}
#endif
#endif
