/*****************************************************************************
 * uref_mgr.c: standard struct uref manager
 *****************************************************************************
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
 *****************************************************************************/

/*
 * Please note that you must maintain at least one manager per thread,
 * because due to the pool implementation, only one thread can make
 * allocations (structures can be released from any thread though).
 */

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uref_attr.h>
#include <upipe/upool.h>
#include <upipe/uref_std.h>

#include <stdlib.h>
#include <assert.h>

/** default minimal size of the attributes structure */
#define UREF_ATTR_MINSIZE 128
/** default minimal size of the attributes structure for control uref */
#define UREF_CONTROL_ATTR_MINSIZE 512

/** explicit names for std_shorthands attributes */
enum uref_attrtype_shorthands {
    UREF_ATTRTYPE_SENTINEL = UREF_ATTRTYPE_SHORTHAND,

    UREF_ATTRTYPE_FLOW_NAME,
    UREF_ATTRTYPE_FLOW_DEF,
    UREF_ATTRTYPE_FLOW_DELETE,
    UREF_ATTRTYPE_FLOW_DISC,

    UREF_ATTRTYPE_CLOCK_SYSTIME,
    UREF_ATTRTYPE_CLOCK_PTS,
    UREF_ATTRTYPE_CLOCK_PTS_ORIG,
    UREF_ATTRTYPE_CLOCK_PTS_SYS,
    UREF_ATTRTYPE_CLOCK_DTSDELAY,
    UREF_ATTRTYPE_CLOCK_VBVDELAY,

    UREF_ATTRTYPE_BLOCK_OFFSET,
    UREF_ATTRTYPE_BLOCK_SIZE,

    UREF_ATTRTYPE_PIC_HOFFSET,
    UREF_ATTRTYPE_PIC_VOFFSET,
    UREF_ATTRTYPE_PIC_HSIZE,
    UREF_ATTRTYPE_PIC_VSIZE,
    UREF_ATTRTYPE_PIC_HPOSITION,
    UREF_ATTRTYPE_PIC_VPOSITION,
    UREF_ATTRTYPE_PIC_ASPECT,
    UREF_ATTRTYPE_PIC_INTERLACED,
    UREF_ATTRTYPE_PIC_TFF,
    UREF_ATTRTYPE_PIC_FIELDS
};

/** @internal @This represents a shorthand attribute type. */
struct std_shorthand {
    uint8_t type;
    const char *name;
    enum uref_attrtype base_type;
};

/** @This stores a list of std_shorthands attributes.
 *
 * Please note that the code expects the first line to be
 * UREF_ATTRTYPE_SHORTHAND + 1, and the last UREF_ATTRTYPE_END, and there
 * should be no gap in number in-between.
 */
static const struct std_shorthand std_shorthands[] = {
    { UREF_ATTRTYPE_FLOW_NAME, "f.flow", UREF_ATTRTYPE_STRING },
    { UREF_ATTRTYPE_FLOW_DEF, "f.def", UREF_ATTRTYPE_STRING },
    { UREF_ATTRTYPE_FLOW_DELETE, "f.delete", UREF_ATTRTYPE_VOID },
    { UREF_ATTRTYPE_FLOW_DISC, "f.disc", UREF_ATTRTYPE_VOID },

    { UREF_ATTRTYPE_CLOCK_SYSTIME, "k.systime", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_CLOCK_PTS, "k.pts", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_CLOCK_PTS_ORIG, "k.pts.orig", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_CLOCK_PTS_SYS, "k.pts.sys", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_CLOCK_DTSDELAY, "k.dtsdelay", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_CLOCK_VBVDELAY, "k.vbvdelay", UREF_ATTRTYPE_UNSIGNED },

    { UREF_ATTRTYPE_BLOCK_OFFSET, "b.offset", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_BLOCK_SIZE, "b.size", UREF_ATTRTYPE_UNSIGNED },

    { UREF_ATTRTYPE_PIC_HOFFSET, "p.hoffset", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_PIC_VOFFSET, "p.voffset", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_PIC_HSIZE, "p.hsize", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_PIC_VSIZE, "p.vsize", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_PIC_HPOSITION, "p.hposition", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_PIC_VPOSITION, "p.vposition", UREF_ATTRTYPE_UNSIGNED },
    { UREF_ATTRTYPE_PIC_ASPECT, "p.aspect", UREF_ATTRTYPE_RATIONAL },
    { UREF_ATTRTYPE_PIC_INTERLACED, "p.interlaced", UREF_ATTRTYPE_VOID },
    { UREF_ATTRTYPE_PIC_TFF, "p.tff", UREF_ATTRTYPE_VOID },
    { UREF_ATTRTYPE_PIC_FIELDS, "p.fields", UREF_ATTRTYPE_SMALL_UNSIGNED },

    { UREF_ATTRTYPE_END, "", UREF_ATTRTYPE_END }
};

/** @This stores the size of the value of basic attribute types. */
static const size_t attr_sizes[] = { 0, 0, 0, 0, 1, 1, 1, 8, 8, 16, 8 };

/** super-set of the uref_mgr structure with additional local members */
struct uref_std_mgr {
    /** extra space added at allocation and re-allocation */
    size_t attr_size;

    /** struct uref pool */
    struct upool pool;

    /** common management structure */
    struct uref_mgr mgr;
};

/** super-set of the uref structure with additional local members */
struct uref_std {
    /** common structure */
    struct uref uref;

    /** allocated size of the attr array */
    size_t attr_size;
    /** attributes storage */
    uint8_t attr[];
};

/** @internal @This returns the high-level uref structure.
 *
 * @param std pointer to the uref_std structure
 * @return pointer to the uref structure
 */
static inline struct uref *uref_std_to_uref(struct uref_std *std)
{
    return &std->uref;
}

/** @internal @This returns the private uref_std structure.
 *
 * @param mgr description structure of the uref mgr
 * @return pointer to the uref_std structure
 */
static inline struct uref_std *uref_std_from_uref(struct uref *uref)
{
    return container_of(uref, struct uref_std, uref);
}

/** @internal @This returns the high-level uref_mgr structure.
 *
 * @param std_mgr pointer to the uref_std_mgr structure
 * @return pointer to the uref_mgr structure
 */
static inline struct uref_mgr *uref_std_mgr_to_uref_mgr(struct uref_std_mgr *std_mgr)
{
    return &std_mgr->mgr;
}

/** @internal @This returns the private uref_std_mgr structure.
 *
 * @param mgr description structure of the uref mgr
 * @return pointer to the uref_std_mgr structure
 */
static inline struct uref_std_mgr *uref_std_mgr_from_uref_mgr(struct uref_mgr *mgr)
{
    return container_of(mgr, struct uref_std_mgr, mgr);
}

/** @internal @This re-allocates a uref_std with a different attributes space.
 *
 * @param uref_p reference to a pointer to a uref to resize
 * @param attr_size required attributes structure size
 * @return false in case of allocation error (unchanged struct uref)
 */
static bool uref_std_resize(struct uref **uref_p, size_t attr_size)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr((*uref_p)->mgr);
    struct uref_std *std = uref_std_from_uref(*uref_p);
    std = realloc(std, sizeof(struct uref_std) + std_mgr->attr_size + attr_size);
    if (unlikely(std == NULL)) return false;
    std->attr_size = std_mgr->attr_size + attr_size;
    *uref_p = uref_std_to_uref(std);
    return true;
}

/** @internal @This allocates a uref with attributes space.
 *
 * @param mgr common management structure
 * @param attr_size required attributes structure size
 * @return pointer to struct uref or NULL in case of allocation error
 */
static struct uref *uref_std_alloc(struct uref_mgr *mgr, size_t attr_size)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    struct uref_std *std = NULL;

    if (likely(attr_size < std_mgr->mgr.control_attr_size)) {
        struct uchain *uchain = upool_pop(&std_mgr->pool);
        if (likely(uchain != NULL)) {
            std = uref_std_from_uref(uref_from_uchain(uchain));
            if (unlikely(std->attr_size < std_mgr->attr_size + attr_size)) {
                struct uref *uref = uref_std_to_uref(std);
                if (unlikely(!uref_std_resize(&uref, attr_size))) {
                    free(std);
                    return NULL;
                }
                std = uref_std_from_uref(uref);
            }
        }
    }

    if (unlikely(std == NULL)) {
        std = malloc(sizeof(struct uref_std) + std_mgr->attr_size + attr_size);
        if (unlikely(std == NULL)) return NULL;

        std->uref.mgr = mgr;
        std->attr_size = std_mgr->attr_size + attr_size;
    }

    uref_mgr_use(mgr);

    uchain_init(&std->uref.uchain);
    std->uref.ubuf = NULL;
    std->attr[0] = UREF_ATTRTYPE_END;

    return uref_std_to_uref(std);
}

/** @internal @This duplicates a uref.
 *
 * @param uref source structure to duplicate
 * @return duplicated struct uref or NULL in case of allocation failure
 */
static struct uref *uref_std_dup(struct uref_mgr *mgr, struct uref *uref)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    struct uref_std *std = uref_std_from_uref(uref);
    struct uref *new_uref = uref_std_alloc(mgr,
                                           std->attr_size - std_mgr->attr_size);
    if (unlikely(new_uref == NULL)) return NULL;

    if (likely(uref->ubuf != NULL)) {
        ubuf_use(uref->ubuf);
        new_uref->ubuf = uref->ubuf;
    }

    struct uref_std *new_std = uref_std_from_uref(new_uref);
    memcpy(new_std->attr, std->attr, std->attr_size);
    return new_uref;
}

/** @internal @This frees a uref.
 *
 * @param uref pointer to a uref structure to free
 */
static void uref_std_free(struct uref *uref)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(uref->mgr);
    struct uref_std *std = uref_std_from_uref(uref);
    if (likely(std->attr_size < std_mgr->mgr.control_attr_size +
                                std_mgr->attr_size)) {
        if (likely(upool_push(&std_mgr->pool, uref_to_uchain(uref))))
            uref = NULL;
    }
    if (unlikely(uref != NULL))
        free(std);

    uref_mgr_release(&std_mgr->mgr);
}

/** @internal @This looks up an attribute in the list of shorthands.
 *
 * @param name name of the attribute
 * @param type type of the attribute
 * @return pointer to the found shorthand entry, or NULL
 */
static const struct std_shorthand *uref_std_attr_shorthand(const char *name,
                                                      enum uref_attrtype type)
{
    const struct std_shorthand *shorthands = std_shorthands;
    while (shorthands->base_type != UREF_ATTRTYPE_END) {
        if (shorthands->base_type == type && !strcmp(shorthands->name, name))
            return shorthands;
        shorthands++;
    }
    return NULL;
}

/** @internal @This jumps to the next attribute.
 *
 * @param attr attribute to iterate from
 * @return pointer to the next valid attribute, or NULL
 */
static uint8_t *uref_std_attr_next(uint8_t *attr)
{
    if (unlikely(*attr == UREF_ATTRTYPE_END))
        return NULL;

    if (likely(*attr > UREF_ATTRTYPE_SHORTHAND)) {
        const struct std_shorthand *shorthand = std_shorthands +
                (*attr - UREF_ATTRTYPE_SHORTHAND - 1);
        if (likely(shorthand->base_type != UREF_ATTRTYPE_OPAQUE &&
                   shorthand->base_type != UREF_ATTRTYPE_STRING)) {
            assert(shorthand->base_type <= UREF_ATTRTYPE_FLOAT);
            return attr + attr_sizes[shorthand->base_type] + 1;
        }
    }

    uint16_t size = (attr[1] << 8) | attr[2];
    return attr + 3 + size;
}

/** @internal @This finds an attribute of the given type.
 *
 * @param uref pointer to the uref
 * @param type type of the attribute (including std_shorthands)
 * @return pointer to the found attribute, or NULL
 */
static uint8_t *uref_std_attr_find_type(struct uref *uref, uint8_t type)
{
    struct uref_std *std = uref_std_from_uref(uref);
    uint8_t *attr = std->attr;
    while (likely(attr != NULL)) {
        if (unlikely(*attr == type))
            return attr;
        attr = uref_std_attr_next(attr);
    }
    return NULL;
}

/** @internal @This finds a non-shorthand attribute of the given name and type.
 *
 * @param uref pointer to the uref
 * @param name name of the attribute
 * @param type type of the attribute (excluding std_shorthands)
 * @return pointer to the found attribute, or NULL
 */
static uint8_t *uref_std_attr_find_name(struct uref *uref, const char *name,
                                        enum uref_attrtype type)
{
    /* This is not to be used for shorthands */
    assert(type < UREF_ATTRTYPE_SHORTHAND);
    struct uref_std *std = uref_std_from_uref(uref);
    uint8_t *attr = std->attr;
    while (likely(attr != NULL)) {
        const char *attr_name = (const char *)(attr + 3);
        if (unlikely(*attr == type && !strcmp(attr_name, name)))
            return attr;
        attr = uref_std_attr_next(attr);
    }
    return NULL;
}

/** @internal @This finds an attribute (shorthand or not) of the given name
 * and type and returns a pointer to its beginning.
 *
 * @param uref pointer to the uref
 * @param name name of the attribute
 * @param type type of the attribute (excluding std_shorthands)
 * @return pointer to the attribute, or NULL
 */
static uint8_t *uref_std_attr_find(struct uref *uref, const char *name,
                                   enum uref_attrtype type)
{
    const struct std_shorthand *shorthand = uref_std_attr_shorthand(name, type);
    if (likely(shorthand != NULL))
        return uref_std_attr_find_type(uref, shorthand->type);

    return uref_std_attr_find_name(uref, name, type);
}

/** @internal @This finds an attribute (shorthand or not) of the given name
 * and type and returns the name and type of the next attribute.
 *
 * @param uref pointer to the uref
 * @param name_p reference to the name of the attribute to find, changed during
 * execution to the name of the next attribute, or NULL if it was the last
 * attribute; if it was NULL, it is changed to the name of the first attribute
 * @param type_p reference to the type of the attribute, if the name is valid
 */
static void uref_std_attr_iterate(struct uref *uref, const char **name_p,
                                  enum uref_attrtype *type_p)
{
    assert(name_p != NULL);
    assert(type_p != NULL);
    struct uref_std *std = uref_std_from_uref(uref);
    uint8_t *attr;

    if (likely(*name_p != NULL)) {
        attr = uref_std_attr_find(uref, *name_p, *type_p);
        if (likely(attr != NULL))
            attr = uref_std_attr_next(attr);
    } else
        attr = std->attr;
    if (unlikely(attr == NULL || *attr == UREF_ATTRTYPE_END)) {
        *name_p = NULL;
        return;
    }

    if (likely(*attr > UREF_ATTRTYPE_SHORTHAND)) {
        const struct std_shorthand *shorthand = std_shorthands +
                (*attr - UREF_ATTRTYPE_SHORTHAND - 1);
        *name_p = shorthand->name;
        *type_p = shorthand->base_type;
        return;
    }

    *name_p = (const char *)(attr + 3);
    *type_p = *attr;
}

/** @internal @This finds an attribute (shorthand or not) of the given name
 * and type and returns a pointer to the beginning of its value.
 *
 * @param uref pointer to the uref
 * @param name name of the attribute
 * @param type type of the attribute (excluding std_shorthands)
 * @param size_p size of the value, written on execution (can be NULL)
 * @return pointer to the value of the found attribute, or NULL
 */
static uint8_t *_uref_std_attr_get(struct uref *uref, const char *name,
                                   enum uref_attrtype type, size_t *size_p)
{
    assert(type < UREF_ATTRTYPE_SHORTHAND);
    uint8_t *attr;
    const struct std_shorthand *shorthand = uref_std_attr_shorthand(name, type);
    if (likely(shorthand != NULL)) {
        attr = uref_std_attr_find_type(uref, shorthand->type);
        if (likely(attr != NULL)) {
            if (likely(type != UREF_ATTRTYPE_OPAQUE &&
                       type != UREF_ATTRTYPE_STRING)) {
                if (likely(size_p != NULL))
                    *size_p = attr_sizes[shorthand->base_type];
                attr++;
            } else {
                uint16_t size = (attr[1] << 8) | attr[2];
                if (likely(size_p != NULL))
                    *size_p = size;
                attr += 3;
            }
        }
        return attr;
    }

    attr = uref_std_attr_find_name(uref, name, type);
    if (likely(attr != NULL)) {
        uint16_t size = (attr[1] << 8) | attr[2];
        size_t namelen = strlen(name);
        assert(size > namelen);
        if (likely(size_p != NULL))
            *size_p = size - namelen - 1;
        attr += 4 + strlen(name);
    }
    return attr;
}

/** @internal @This finds an attribute (shorthand or not) of the given name
 * and type and returns a pointer to the beginning of its value (const version).
 *
 * @param uref pointer to the uref
 * @param name name of the attribute
 * @param type type of the attribute (excluding std_shorthands)
 * @param size_p size of the value, written on execution (can be NULL)
 * @return pointer to the value of the found attribute, or NULL
 */
static const uint8_t *uref_std_attr_get(struct uref *uref, const char *name,
                                        enum uref_attrtype type, size_t *size_p)
{
    return _uref_std_attr_get(uref, name, type, size_p);
}

/** @internal @This deletes an attribute.
 *
 * @param uref pointer to the uref
 * @param name name of the attribute
 * @param type type of the attribute
 * @return true if the attribute existed before
 */
static bool uref_std_attr_delete(struct uref *uref, const char *name,
                                 enum uref_attrtype type)
{
    assert(type < UREF_ATTRTYPE_SHORTHAND && type != UREF_ATTRTYPE_END);
    struct uref_std *std = uref_std_from_uref(uref);
    uint8_t *attr, *end;
    const struct std_shorthand *shorthand = uref_std_attr_shorthand(name, type);
    if (likely(shorthand != NULL))
        attr = uref_std_attr_find_type(uref, shorthand->type);
    else
        attr = uref_std_attr_find_name(uref, name, type);
    if (unlikely(attr == NULL)) return false;

    end = uref_std_attr_next(attr);
    memmove(attr, end, std->attr + std->attr_size - end);
    return true;
}

/** @internal @This adds or changes an attribute (excluding the value itself).
 *
 * @param uref_p pointer to the pointer to the uref (possibly modified)
 * @param name name of the attribute
 * @param type type of the attribute
 * @param attr_size size needed to store the value of the attribute
 * @return pointer to the value of the attribute
 */
static uint8_t *uref_std_attr_set(struct uref **uref_p, const char *name,
                                  enum uref_attrtype type, size_t attr_size)
{
    struct uref *uref = *uref_p;
    struct uref_std *std = uref_std_from_uref(uref);
    /* check if it already exists */
    size_t current_size;
    uint8_t *attr = _uref_std_attr_get(uref, name, type, &current_size);
    if (unlikely(attr != NULL)) {
        if (likely((type != UREF_ATTRTYPE_OPAQUE &&
                    type != UREF_ATTRTYPE_STRING) ||
                   current_size == attr_size))
            return attr;
        if (likely(type == UREF_ATTRTYPE_STRING && current_size > attr_size)) {
            /* Just zero out superfluous bytes */
            memset(attr + attr_size, 0, current_size - attr_size);
            return attr;
        }
        uref_std_attr_delete(uref, name, type);
    }

    /* calculate header size */
    size_t header_size = 1;
    size_t namelen = strlen(name);
    const struct std_shorthand *shorthand = uref_std_attr_shorthand(name, type);
    if (unlikely(shorthand == NULL))
        header_size += 2 + namelen + 1;
    else if (unlikely(type == UREF_ATTRTYPE_OPAQUE ||
                      type == UREF_ATTRTYPE_STRING))
        header_size += 2;

    /* check total attributes size */
    attr = uref_std_attr_find_type(uref, UREF_ATTRTYPE_END);
    size_t total_size = (attr - std->attr) + header_size + attr_size + 1;
    if (unlikely(total_size >= std->attr_size)) {
        if (unlikely(!uref_std_resize(uref_p, total_size)))
            return NULL;

        uref = *uref_p;
        std = uref_std_from_uref(uref);
        attr = uref_std_attr_find_type(uref, UREF_ATTRTYPE_END);
    }

    /* write attribute header */
    if (unlikely(shorthand == NULL)) {
        assert(namelen + 1 + attr_size <= UINT16_MAX);
        uint16_t size = namelen + 1 + attr_size;
        *attr++ = type;
        *attr++ = size >> 8;
        *attr++ = size & 0xff;
        memcpy(attr, name, namelen + 1);
        attr += namelen + 1;
   } else if (unlikely(type == UREF_ATTRTYPE_OPAQUE ||
                       type == UREF_ATTRTYPE_STRING)) {
        assert(attr_size <= UINT16_MAX);
        uint16_t size = attr_size;
        *attr++ = shorthand->type;
        *attr++ = size >> 8;
        *attr++ = size & 0xff;
   } else
        *attr++ = shorthand->type;

    attr[attr_size] = UREF_ATTRTYPE_END;
    return attr;
}

/** @internal @This frees a uref_mgr structure.
 *
 * @param mgr pointer to a uref_mgr structure to free
 */
static void uref_std_mgr_free(struct uref_mgr *mgr)
{
    struct uref_std_mgr *std_mgr = uref_std_mgr_from_uref_mgr(mgr);
    struct uchain *uchain;

    while ((uchain = upool_pop(&std_mgr->pool)) != NULL) {
        struct uref_std *std = uref_std_from_uref(uref_from_uchain(uchain));
        free(std);
    }
    upool_clean(&std_mgr->pool);

    urefcount_clean(&std_mgr->mgr.refcount);
    free(std_mgr);
}

/** @This allocates a new instance of the standard uref manager
 *
 * @param pool_depth maximum number of uref structures in the pool
 * @param attr_size default attributes structure size (if set to -1, a default
 * sensible value is used)
 * @control_attr_size extra attributes space for control packets; also
 * limit from which struct uref structures are not recycled in the pool, but
 * directly allocated and freed (if set to -1, a default sensible value is used)
 * @return pointer to manager, or NULL in case of error
 */
struct uref_mgr *uref_std_mgr_alloc(unsigned int pool_depth,
                               int attr_size, int control_attr_size)
{
    struct uref_std_mgr *std_mgr = malloc(sizeof(struct uref_std_mgr));
    if (unlikely(std_mgr == NULL)) return NULL;

    upool_init(&std_mgr->pool, pool_depth);
    std_mgr->attr_size = attr_size > 0 ? attr_size : UREF_ATTR_MINSIZE;

    urefcount_init(&std_mgr->mgr.refcount);
    std_mgr->mgr.control_attr_size = control_attr_size > 0 ?
                                     control_attr_size :
                                     UREF_CONTROL_ATTR_MINSIZE;
    std_mgr->mgr.uref_alloc = uref_std_alloc;
    std_mgr->mgr.uref_dup = uref_std_dup;
    std_mgr->mgr.uref_free = uref_std_free;
    std_mgr->mgr.uref_attr_iterate = uref_std_attr_iterate;
    std_mgr->mgr.uref_attr_get = uref_std_attr_get;
    std_mgr->mgr.uref_attr_set = uref_std_attr_set;
    std_mgr->mgr.uref_attr_delete = uref_std_attr_delete;
    std_mgr->mgr.uref_mgr_free = uref_std_mgr_free;
    
    return uref_std_mgr_to_uref_mgr(std_mgr);
}
