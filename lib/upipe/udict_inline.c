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
 * @short Upipe inline manager of dictionary of attributes
 * This manager stores all attributes inline inside a single umem block.
 * This is designed in order to minimize calls to memory allocators, and
 * to transmit dictionaries over streams.
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/upool.h>
#include <upipe/umem.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>

#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

/** define to activate statistics */
#undef STATS
/** default minimal size of the dictionary */
#define UDICT_MIN_SIZE 128
/** default extra space added on udict expansion */
#define UDICT_EXTRA_SIZE 64

/** @internal @This represents a shorthand attribute type. */
struct inline_shorthand {
    /** name of the attribute */
    const char *name;
    /** base type of the attribute */
    enum udict_type base_type;
};

/** @This stores a list of inline_shorthands attributes.
 *
 * Please note that the code expects the first line to be
 * UDICT_TYPE_SHORTHAND + 1.
 */
static const struct inline_shorthand inline_shorthands[] = {
    { "f.random", UDICT_TYPE_VOID },
    { "f.error", UDICT_TYPE_VOID },
    { "f.def", UDICT_TYPE_STRING },
    { "f.id", UDICT_TYPE_UNSIGNED },
    { "f.rawdef", UDICT_TYPE_STRING },
    { "f.langs", UDICT_TYPE_SMALL_UNSIGNED },

    { "e.events", UDICT_TYPE_UNSIGNED },

    { "k.duration", UDICT_TYPE_UNSIGNED },
    { "k.rate", UDICT_TYPE_RATIONAL },
    { "k.latency", UDICT_TYPE_UNSIGNED },

    { "b.end", UDICT_TYPE_VOID },

    { "p.num", UDICT_TYPE_UNSIGNED },
    { "p.key", UDICT_TYPE_VOID },
    { "p.hsize", UDICT_TYPE_UNSIGNED },
    { "p.vsize", UDICT_TYPE_UNSIGNED },
    { "p.hsizevis", UDICT_TYPE_UNSIGNED },
    { "p.vsizevis", UDICT_TYPE_UNSIGNED },
    { "p.format", UDICT_TYPE_STRING },
    { "p.fullrange", UDICT_TYPE_VOID },
    { "p.colorprim", UDICT_TYPE_STRING },
    { "p.transfer", UDICT_TYPE_STRING },
    { "p.colmatrix", UDICT_TYPE_STRING },
    { "p.hposition", UDICT_TYPE_UNSIGNED },
    { "p.vposition", UDICT_TYPE_UNSIGNED },
    { "p.sar", UDICT_TYPE_RATIONAL },
    { "p.overscan", UDICT_TYPE_VOID },
    { "p.progressive", UDICT_TYPE_VOID },
    { "p.tf", UDICT_TYPE_VOID },
    { "p.bf", UDICT_TYPE_VOID },
    { "p.tff", UDICT_TYPE_VOID },
    { "p.afd", UDICT_TYPE_SMALL_UNSIGNED },
    { "p.cea_708", UDICT_TYPE_OPAQUE }
};

/** @This stores the size of the value of basic attribute types. */
static const size_t attr_sizes[] = { 0, 0, 0, 0, 1, 1, 1, 8, 8, 16, 8 };

/** super-set of the udict_mgr structure with additional local members */
struct udict_inline_mgr {
    /** refcount management structure */
    struct urefcount urefcount;

    /** minimum space at allocation */
    size_t min_size;
    /** extra space added when the umem is expanded */
    size_t extra_size;

    /** udict pool */
    struct upool udict_pool;
    /** umem allocator */
    struct umem_mgr *umem_mgr;

#ifdef STATS
    uint64_t stats[sizeof(inline_shorthands) / sizeof(struct inline_shorthand)];
#endif

    /** common management structure */
    struct udict_mgr mgr;
};

UBASE_FROM_TO(udict_inline_mgr, udict_mgr, udict_mgr, mgr)
UBASE_FROM_TO(udict_inline_mgr, urefcount, urefcount, urefcount)
UBASE_FROM_TO(udict_inline_mgr, upool, udict_pool, udict_pool)

/** super-set of the udict structure with additional local members */
struct udict_inline {
    /** umem structure pointing to buffer */
    struct umem umem;
    /** used size */
    size_t size;

    /** common structure */
    struct udict udict;
};

UBASE_FROM_TO(udict_inline, udict, udict, udict)

/** @This allocates a udict with attributes space.
 *
 * @param mgr common management structure
 * @param size initial size of the attribute space
 * @return pointer to udict or NULL in case of allocation error
 */
static struct udict *udict_inline_alloc(struct udict_mgr *mgr, size_t size)
{
    struct udict_inline_mgr *inline_mgr = udict_inline_mgr_from_udict_mgr(mgr);
    struct udict_inline *inl = upool_alloc(&inline_mgr->udict_pool,
                                           struct udict_inline *);
    struct udict *udict = udict_inline_to_udict(inl);

    if (size < inline_mgr->min_size)
        size = inline_mgr->min_size;
    if (unlikely(!umem_alloc(inline_mgr->umem_mgr, &inl->umem, size))) {
        upool_free(&inline_mgr->udict_pool, inl);
        return NULL;
    }

    uint8_t *buffer = umem_buffer(&inl->umem);
    buffer[0] = UDICT_TYPE_END;
    inl->size = 1;

    udict_mgr_use(mgr);
    return udict;
}

/** @This duplicates a given udict.
 *
 * @param udict pointer to udict
 * @param new_udict_p reference written with a pointer to the newly allocated
 * udict
 * @return an error code
 */
static int udict_inline_dup(struct udict *udict, struct udict **new_udict_p)
{
    assert(new_udict_p != NULL);
    struct udict_inline *inl = udict_inline_from_udict(udict);
    struct udict *new_udict = udict_inline_alloc(udict->mgr, inl->size);
    if (unlikely(new_udict == NULL))
        return UBASE_ERR_ALLOC;

    *new_udict_p = new_udict;

    struct udict_inline *new_inl = udict_inline_from_udict(new_udict);
    memcpy(umem_buffer(&new_inl->umem), umem_buffer(&inl->umem), inl->size);
    new_inl->size = inl->size;
    return UBASE_ERR_NONE;
}

/** @internal @This looks up a shorthand attribute in the list of shorthands.
 *
 * @param type shorthand attribute
 * @return pointer to the found shorthand entry, or NULL
 */
static const struct inline_shorthand *
    udict_inline_shorthand(enum udict_type type)
{
    if (unlikely(type > UDICT_TYPE_SHORTHAND + 1 + sizeof(inline_shorthands) /
                                               sizeof(struct inline_shorthand)))
        return NULL;
    return &inline_shorthands[type - UDICT_TYPE_SHORTHAND - 1];
}

/** @internal @This jumps to the next attribute.
 *
 * @param attr attribute to iterate from
 * @return pointer to the next valid attribute, or NULL
 */
static uint8_t *udict_inline_next(uint8_t *attr)
{
    if (*attr == UDICT_TYPE_END)
        return NULL;

    if (likely(*attr > UDICT_TYPE_SHORTHAND)) {
        const struct inline_shorthand *shorthand =
            udict_inline_shorthand(*attr);
        if (unlikely(shorthand == NULL))
            return NULL;
        if (shorthand->base_type != UDICT_TYPE_OPAQUE &&
            shorthand->base_type != UDICT_TYPE_STRING)
            return attr + attr_sizes[shorthand->base_type] + 1;
    }

    uint16_t size = (attr[1] << 8) | attr[2];
    return attr + 3 + size;
}

/** @internal @This finds an attribute (shorthand or not) of the given name
 * and type and returns a pointer to its beginning.
 *
 * @param udict pointer to the udict
 * @param name name of the attribute
 * @param type type of the attribute (excluding inline_shorthands)
 * @return pointer to the attribute, or NULL
 */
static uint8_t *udict_inline_find(struct udict *udict, const char *name,
                                  enum udict_type type)
{
    struct udict_inline *inl = udict_inline_from_udict(udict);
#ifdef STATS
    if (type > UDICT_TYPE_SHORTHAND) {
        struct udict_inline_mgr *inline_mgr =
            udict_inline_mgr_from_udict_mgr(udict->mgr);
        inline_mgr->stats[type - UDICT_TYPE_SHORTHAND - 1]++;
    }
#endif
    uint8_t *attr = umem_buffer(&inl->umem);
    while (attr != NULL) {
        if (*attr == type &&
             (type > UDICT_TYPE_SHORTHAND || type == UDICT_TYPE_END ||
              !strcmp((const char *)(attr + 3), name)))
            return attr;
        attr = udict_inline_next(attr);
    }
    return NULL;
}

/** @internal @This finds an attribute (shorthand or not) of the given name
 * and type and returns the name and type of the next attribute.
 *
 * @param udict pointer to the udict
 * @param name_p reference to the name of the attribute to find, changed during
 * execution to the name of the next attribute, or NULL if it is a shorthand
 * @param type_p reference to the type of the attribute, changed to
 * UDICT_TYPE_END at the end of the iteration; start with UDICT_TYPE_END as well
 */
static void udict_inline_iterate(struct udict *udict, const char **name_p,
                                 enum udict_type *type_p)
{
    assert(name_p != NULL);
    assert(type_p != NULL);
    struct udict_inline *inl = udict_inline_from_udict(udict);
    uint8_t *attr;

    if (likely(*type_p != UDICT_TYPE_END)) {
        attr = udict_inline_find(udict, *name_p, *type_p);
        if (likely(attr != NULL))
            attr = udict_inline_next(attr);
    } else
        attr = umem_buffer(&inl->umem);
    if (unlikely(attr == NULL || *attr == UDICT_TYPE_END)) {
        *type_p = UDICT_TYPE_END;
        return;
    }

    *type_p = *attr;
    *name_p = *attr > UDICT_TYPE_SHORTHAND ? NULL : (const char *)(attr + 3);
}

/** @internal @This finds an attribute (shorthand or not) of the given name
 * and type and returns a pointer to the beginning of its value.
 *
 * @param udict pointer to the udict
 * @param name name of the attribute
 * @param type type of the attribute (excluding inline_shorthands)
 * @param size_p size of the value, written on execution (can be NULL)
 * @return pointer to the value of the found attribute, or NULL
 */
static uint8_t *_udict_inline_get(struct udict *udict, const char *name,
                                  enum udict_type type, size_t *size_p)
{
    uint8_t *attr = udict_inline_find(udict, name, type);
    if (unlikely(attr == NULL))
        return NULL;

    if (likely(type > UDICT_TYPE_SHORTHAND)) {
        const struct inline_shorthand *shorthand =
            udict_inline_shorthand(*attr);
        if (unlikely(shorthand == NULL))
            return NULL;

        if (shorthand->base_type != UDICT_TYPE_OPAQUE &&
            shorthand->base_type != UDICT_TYPE_STRING) {
            if (likely(size_p != NULL))
                *size_p = attr_sizes[shorthand->base_type];
            attr++;
        } else {
            uint16_t size = (attr[1] << 8) | attr[2];
            if (likely(size_p != NULL))
                *size_p = size;
            attr += 3;
        }
    } else {
        uint16_t size = (attr[1] << 8) | attr[2];
        size_t namelen = strlen(name);
        assert(size > namelen);
        if (likely(size_p != NULL))
            *size_p = size - namelen - 1;
        attr += 4 + namelen;
    }
    return attr;
}

/** @internal @This finds an attribute (shorthand or not) of the given name
 * and type and returns a pointer to the beginning of its value (const version).
 *
 * @param udict pointer to the udict
 * @param name name of the attribute
 * @param type type of the attribute (excluding inline_shorthands)
 * @param size_p size of the value, written on execution (can be NULL)
 * @param attr_p pointer to the value of the found attribute, written on
 * execution
 * @return an error code
 */
static int udict_inline_get(struct udict *udict, const char *name,
                            enum udict_type type, size_t *size_p,
                            const uint8_t **attr_p)
{
    uint8_t *attr = _udict_inline_get(udict, name, type, size_p);
    if (unlikely(attr == NULL))
        return UBASE_ERR_INVALID;
    if (attr_p != NULL)
        *attr_p = attr;
    return UBASE_ERR_NONE;
}

/** @internal @This deletes an attribute.
 *
 * @param udict pointer to the udict
 * @param name name of the attribute
 * @param type type of the attribute
 * @return an error code
 */
static int udict_inline_delete(struct udict *udict, const char *name,
                               enum udict_type type)
{
    assert(type != UDICT_TYPE_END);
    struct udict_inline *inl = udict_inline_from_udict(udict);
    uint8_t *attr = udict_inline_find(udict, name, type);
    if (unlikely(attr == NULL))
        return UBASE_ERR_INVALID;

    uint8_t *end = udict_inline_next(attr);
    memmove(attr, end, umem_buffer(&inl->umem) + inl->size - end);
    inl->size -= end - attr;
    return UBASE_ERR_NONE;
}

/** @internal @This adds or changes an attribute (excluding the value itself).
 *
 * @param udict pointer to the udict
 * @param name name of the attribute
 * @param type type of the attribute
 * @param attr_size size needed to store the value of the attribute
 * @param attr_p pointer to the value of the attribute
 * @return an error code
 */
static int udict_inline_set(struct udict *udict, const char *name,
                            enum udict_type type, size_t attr_size,
                            uint8_t **attr_p)
{
    struct udict_inline *inl = udict_inline_from_udict(udict);
    const struct inline_shorthand *shorthand = NULL;
    enum udict_type base_type = type;
    if (likely(type > UDICT_TYPE_SHORTHAND)) {
        shorthand = udict_inline_shorthand(type);
        if (unlikely(shorthand == NULL))
            return UBASE_ERR_INVALID;
        base_type = shorthand->base_type;
    }

    /* check if it already exists */
    size_t current_size;
    uint8_t *attr = _udict_inline_get(udict, name, type, &current_size);
    if (unlikely(attr != NULL)) {
        if ((base_type != UDICT_TYPE_OPAQUE &&
             base_type != UDICT_TYPE_STRING) ||
            current_size == attr_size) {
            if (attr_p != NULL)
                *attr_p = attr;
            return UBASE_ERR_NONE;
        }
        if (likely(base_type == UDICT_TYPE_STRING &&
                   current_size > attr_size)) {
            /* Just zero out superfluous bytes */
            memset(attr + attr_size, 0, current_size - attr_size);
            if (attr_p != NULL)
                *attr_p = attr;
            return UBASE_ERR_NONE;
        }
        udict_inline_delete(udict, name, type);
    }

    /* calculate header size */
    size_t header_size = 1;
    size_t namelen = 0;
    if (likely(shorthand != NULL)) {
        if (base_type == UDICT_TYPE_OPAQUE || base_type == UDICT_TYPE_STRING)
            header_size += 2;
    } else {
        namelen = strlen(name);
        header_size += 2 + namelen + 1;
    }

    /* check total attributes size */
    attr = umem_buffer(&inl->umem) + inl->size - 1;
    size_t total_size = (attr - umem_buffer(&inl->umem)) + header_size +
                        attr_size + 1;
    if (unlikely(total_size >= umem_size(&inl->umem))) {
        struct udict_inline_mgr *inline_mgr =
            udict_inline_mgr_from_udict_mgr(udict->mgr);
        if (unlikely(!umem_realloc(&inl->umem, total_size +
                                               inline_mgr->extra_size)))
            return UBASE_ERR_ALLOC;

        attr = umem_buffer(&inl->umem) + inl->size - 1;
    }
    assert(*attr == UDICT_TYPE_END);

    /* write attribute header */
    if (unlikely(shorthand == NULL)) {
        assert(namelen + 1 + attr_size <= UINT16_MAX);
        uint16_t size = namelen + 1 + attr_size;
        *attr++ = type;
        *attr++ = size >> 8;
        *attr++ = size & 0xff;
        memcpy(attr, name, namelen + 1);
        attr += namelen + 1;
   } else if (shorthand->base_type == UDICT_TYPE_OPAQUE ||
              shorthand->base_type == UDICT_TYPE_STRING) {
        assert(attr_size <= UINT16_MAX);
        uint16_t size = attr_size;
        *attr++ = type;
        *attr++ = size >> 8;
        *attr++ = size & 0xff;
   } else
        *attr++ = type;

    attr[attr_size] = UDICT_TYPE_END;
    if (attr_p != NULL)
        *attr_p = attr;
    inl->size += header_size + attr_size;
    return UBASE_ERR_NONE;
}

/** @internal @This names a shorthand attribute.
 *
 * @param type shorthand type
 * @param name_p filled in with the name of the shorthand attribute
 * @param base_type_p filled in with the base type of the shorthand attribute
 * @return an error code
 */
static int udict_inline_name(enum udict_type type, const char **name_p,
                             enum udict_type *base_type_p)
{
    if (type <= UDICT_TYPE_SHORTHAND)
        return UBASE_ERR_INVALID;

    const struct inline_shorthand *shorthand = udict_inline_shorthand(type);
    if (unlikely(shorthand == NULL))
        return UBASE_ERR_INVALID;

    *name_p = shorthand->name;
    *base_type_p = shorthand->base_type;
    return UBASE_ERR_NONE;
}

/** @This handles control commands.
 *
 * @param udict pointer to udict
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int udict_inline_control(struct udict *udict, int command, va_list args)
{
    switch (command) {
        case UDICT_DUP: {
            struct udict **udict_p = va_arg(args, struct udict **);
            return udict_inline_dup(udict, udict_p);
        }
        case UDICT_ITERATE: {
            const char **name_p = va_arg(args, const char **);
            enum udict_type *type_p = va_arg(args, enum udict_type *);
            udict_inline_iterate(udict, name_p, type_p);
            return UBASE_ERR_NONE;
        }
        case UDICT_GET: {
            const char *name = va_arg(args, const char *);
            enum udict_type type = va_arg(args, enum udict_type);
            size_t *size_p = va_arg(args, size_t *);
            const uint8_t **attr_p = va_arg(args, const uint8_t **);
            return udict_inline_get(udict, name, type, size_p, attr_p);
        }
        case UDICT_SET: {
            const char *name = va_arg(args, const char *);
            enum udict_type type = va_arg(args, enum udict_type);
            size_t size = va_arg(args, size_t);
            uint8_t **attr_p = va_arg(args, uint8_t **);
            return udict_inline_set(udict, name, type, size, attr_p);
        }
        case UDICT_DELETE: {
            const char *name = va_arg(args, const char *);
            enum udict_type type = va_arg(args, enum udict_type);
            return udict_inline_delete(udict, name, type);
        }
        case UDICT_NAME: {
            enum udict_type type = va_arg(args, enum udict_type);
            const char **name_p = va_arg(args, const char **);
            enum udict_type *base_type_p = va_arg(args, enum udict_type *);
            return udict_inline_name(type, name_p, base_type_p);
        }
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a udict.
 *
 * @param udict pointer to a udict structure to free
 */
static void udict_inline_free(struct udict *udict)
{
    struct udict_inline_mgr *inline_mgr =
        udict_inline_mgr_from_udict_mgr(udict->mgr);
    struct udict_inline *inl = udict_inline_from_udict(udict);

    umem_free(&inl->umem);
    upool_free(&inline_mgr->udict_pool, inl);
    udict_mgr_release(&inline_mgr->mgr);
}

/** @internal @This allocates the data structure.
 *
 * @param upool pointer to upool
 * @return pointer to udict_inline or NULL in case of allocation error
 */
static void *udict_inline_alloc_inner(struct upool *upool)
{
    struct udict_inline_mgr *inline_mgr =
        udict_inline_mgr_from_udict_pool(upool);
    struct udict_inline *inl = malloc(sizeof(struct udict_inline));
    if (unlikely(inl == NULL))
        return NULL;
    struct udict *udict = udict_inline_to_udict(inl);
    udict->mgr = udict_inline_mgr_to_udict_mgr(inline_mgr);
    return inl;
}

/** @internal @This frees a udict_inline.
 *
 * @param upool pointer to upool
 * @param inl pointer to a udict_inline structure to free
 */
static void udict_inline_free_inner(struct upool *upool, void *inl)
{
    free(inl);
}

/** @internal @This instructs an existing udict manager to release all
 * structures currently kept in pools. It is intended as a debug tool only.
 *
 * @param mgr pointer to udict manager
 */
static void udict_inline_mgr_vacuum(struct udict_mgr *mgr)
{
    struct udict_inline_mgr *inline_mgr = udict_inline_mgr_from_udict_mgr(mgr);
    upool_vacuum(&inline_mgr->udict_pool);
}

/** @This processes control commands on a udict_std_mgr.
 *
 * @param mgr pointer to a udict_mgr structure
 * @param command type of command to process
 * @param args arguments of the command
 * @return an error code
 */
static int udict_inline_mgr_control(struct udict_mgr *mgr,
                                    int command, va_list args)
{
    switch (command) {
        case UDICT_MGR_VACUUM:
            udict_inline_mgr_vacuum(mgr);
            return UBASE_ERR_NONE;
        default:
            return UBASE_ERR_UNHANDLED;
    }
}

/** @This frees a udict manager.
 *
 * @param urefcount pointer to urefcount
 */
static void udict_inline_mgr_free(struct urefcount *urefcount)
{
    struct udict_inline_mgr *inline_mgr =
        udict_inline_mgr_from_urefcount(urefcount);
#ifdef STATS
    int i;
    for (i = 0; i < sizeof(inline_shorthands) / sizeof(struct inline_shorthand);
         i++) {
        const char *name;
        enum udict_type base_type;
        udict_inline_name(UDICT_TYPE_SHORTHAND + 1 + i, &name, &base_type);
        printf("%s: %"PRIu64"\n", name, inline_mgr->stats[i]);
    }
#endif

    upool_clean(&inline_mgr->udict_pool);
    umem_mgr_release(inline_mgr->umem_mgr);

    urefcount_clean(urefcount);
    free(inline_mgr);
}

/** @This allocates a new instance of the inline udict manager.
 *
 * @param udict_pool_depth maximum number of udict structures in the pool
 * @param umem_mgr memory allocator to use for buffers
 * @param min_size minimum allocated space for the udict (if set to -1, a
 * default sensible value is used)
 * @param extra_size extra space added when the udict needs to be resized
 * (if set to -1, a default sensible value is used)
 * @return pointer to manager, or NULL in case of error
 */
struct udict_mgr *udict_inline_mgr_alloc(unsigned int udict_pool_depth,
                                         struct umem_mgr *umem_mgr,
                                         int min_size, int extra_size)
{
    struct udict_inline_mgr *inline_mgr =
        malloc(sizeof(struct udict_inline_mgr) +
               upool_sizeof(udict_pool_depth));
    if (unlikely(inline_mgr == NULL))
        return NULL;

    upool_init(&inline_mgr->udict_pool, udict_pool_depth,
               (void *)inline_mgr + sizeof(struct udict_inline_mgr),
               udict_inline_alloc_inner, udict_inline_free_inner);
    inline_mgr->umem_mgr = umem_mgr;
    umem_mgr_use(umem_mgr);

    inline_mgr->min_size = min_size > 0 ? min_size : UDICT_MIN_SIZE;
    inline_mgr->extra_size = extra_size > 0 ? extra_size : UDICT_EXTRA_SIZE;

    urefcount_init(udict_inline_mgr_to_urefcount(inline_mgr),
                   udict_inline_mgr_free);
    inline_mgr->mgr.refcount = udict_inline_mgr_to_urefcount(inline_mgr);
    inline_mgr->mgr.udict_alloc = udict_inline_alloc;
    inline_mgr->mgr.udict_control = udict_inline_control;
    inline_mgr->mgr.udict_free = udict_inline_free;
    inline_mgr->mgr.udict_mgr_control = udict_inline_mgr_control;

#ifdef STATS
    int i;
    for (i = 0; i < sizeof(inline_shorthands) / sizeof(struct inline_shorthand);
         i++)
        inline_mgr->stats[i] = 0;
#endif
    
    return udict_inline_mgr_to_udict_mgr(inline_mgr);
}
