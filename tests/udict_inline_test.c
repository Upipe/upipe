/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for the inline manager of dictionary attributes
 */

#undef NDEBUG

#include "upipe/umem.h"
#include "upipe/umem_alloc.h"
#include "upipe/udict.h"
#include "upipe/udict_inline.h"
#include "upipe/udict_dump.h"
#include "upipe/uprobe.h"
#include "upipe/uprobe_stdio.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 1

#define SALUTATION "Hello everyone, this is just some padding to make the structure bigger, if you don't mind."

/** iterates over a udict and checks that it returns the given attributes in
 * order; with copy set, each name is given back as a copy, so that the
 * attribute is looked up rather than resumed from */
static void check_iterate(struct udict *udict, unsigned nb,
                          const enum udict_type *types,
                          const char *const *names, bool copy)
{
    const char *name = NULL;
    enum udict_type type = UDICT_TYPE_END;
    char buffer[16];
    for (unsigned i = 0; i < nb; i++) {
        ubase_assert(udict_iterate(udict, &name, &type));
        assert(type == types[i]);
        if (names[i] == NULL)
            assert(name == NULL);
        else
            assert(name != NULL && !strcmp(name, names[i]));
        if (copy && name != NULL) {
            snprintf(buffer, sizeof(buffer), "%s", name);
            name = buffer;
        }
    }
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_END);
}

/** allocates a udict whose second attribute is a shorthand which a change to
 * the first attribute moves: its old position then holds a byte of the
 * opaque value of the third attribute, equal to its type, so that resuming
 * from the old position would go wrong */
static struct udict *moving_udict(struct udict_mgr *mgr)
{
    struct udict *udict = udict_alloc(mgr, 0);
    assert(udict != NULL);
    ubase_assert(udict_set_string(udict, "ab", UDICT_TYPE_STRING, "x.s"));
    ubase_assert(udict_set_void(udict, NULL, UDICT_TYPE_FLOW_ERROR, NULL));
    uint8_t value[16];
    memset(value, UDICT_TYPE_FLOW_ERROR, sizeof(value));
    struct udict_opaque opaque = { .v = value, .size = sizeof(value) };
    ubase_assert(udict_set_opaque(udict, opaque, UDICT_TYPE_OPAQUE, "x.o"));

    const char *name = NULL;
    enum udict_type type = UDICT_TYPE_END;
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_STRING && !strcmp(name, "x.s"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_FLOW_ERROR && name == NULL);
    return udict;
}

/** checks that an iteration resumes from the attribute it returned last when
 * given it back, and looks the attribute up otherwise */
static void test_iterate(struct udict_mgr *mgr)
{
    /* room enough for the attribute added below not to move the others */
    struct udict *udict = udict_alloc(mgr, 1024);
    assert(udict != NULL);
    ubase_assert(udict_set_unsigned(udict, 1, UDICT_TYPE_UNSIGNED, "x.a"));
    ubase_assert(udict_set_string(udict, "def.", UDICT_TYPE_FLOW_DEF, NULL));
    ubase_assert(udict_set_unsigned(udict, 2, UDICT_TYPE_UNSIGNED, "x.b"));
    ubase_assert(udict_set_void(udict, NULL, UDICT_TYPE_FLOW_ERROR, NULL));
    ubase_assert(udict_set_unsigned(udict, 3, UDICT_TYPE_UNSIGNED, "x.c"));
    static const enum udict_type types[] = {
        UDICT_TYPE_UNSIGNED, UDICT_TYPE_FLOW_DEF, UDICT_TYPE_UNSIGNED,
        UDICT_TYPE_FLOW_ERROR, UDICT_TYPE_UNSIGNED
    };
    static const char *const names[] = { "x.a", NULL, "x.b", NULL, "x.c" };

    /* named and shorthand attributes are returned in order, whether the
     * names are given back as returned or as copies, and an iteration can
     * be started again once finished */
    check_iterate(udict, 5, types, names, false);
    check_iterate(udict, 5, types, names, true);
    check_iterate(udict, 5, types, names, false);

    /* a duplicated udict starts afresh: given a name of the original, it
     * looks the attribute up among its own, and the original then resumes
     * from where it was */
    struct udict *dup = udict_dup(udict);
    assert(dup != NULL);
    const char *name = NULL;
    enum udict_type type = UDICT_TYPE_END;
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_UNSIGNED && !strcmp(name, "x.a"));
    const char *dup_name = name;
    enum udict_type dup_type = type;
    ubase_assert(udict_iterate(dup, &dup_name, &dup_type));
    assert(dup_type == UDICT_TYPE_FLOW_DEF && dup_name == NULL);
    ubase_assert(udict_iterate(dup, &dup_name, &dup_type));
    assert(dup_type == UDICT_TYPE_UNSIGNED && !strcmp(dup_name, "x.b"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_FLOW_DEF && name == NULL);
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_UNSIGNED && !strcmp(name, "x.b"));
    assert(name != dup_name);
    udict_free(dup);

    /* given another attribute than the one returned last, the iteration
     * carries on after that one */
    ubase_assert(udict_iterate(udict, &name, &type));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_UNSIGNED && !strcmp(name, "x.c"));
    name = "x.a";
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_FLOW_DEF && name == NULL);
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_UNSIGNED && !strcmp(name, "x.b"));

    /* deleting the attribute returned last and resuming from the one before
     * it carries on with the next one */
    ubase_assert(udict_delete(udict, UDICT_TYPE_UNSIGNED, "x.b"));
    name = NULL;
    type = UDICT_TYPE_FLOW_DEF;
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_FLOW_ERROR && name == NULL);
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_UNSIGNED && !strcmp(name, "x.c"));

    /* an attribute added during an iteration, without moving the others, is
     * returned at its end */
    ubase_assert(udict_set_unsigned(udict, 4, UDICT_TYPE_UNSIGNED, "x.d"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_UNSIGNED && !strcmp(name, "x.d"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_END);
    udict_free(udict);

    /* a deletion moving the attribute returned last makes the iteration
     * look it up again */
    udict = moving_udict(mgr);
    ubase_assert(udict_delete(udict, UDICT_TYPE_STRING, "x.s"));
    name = NULL;
    type = UDICT_TYPE_FLOW_ERROR;
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_OPAQUE && !strcmp(name, "x.o"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_END);
    udict_free(udict);

    /* and so does a change of size, which moves the attribute to the end */
    udict = moving_udict(mgr);
    ubase_assert(udict_set_string(udict, "a longer string", UDICT_TYPE_STRING,
                                  "x.s"));
    name = NULL;
    type = UDICT_TYPE_FLOW_ERROR;
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_OPAQUE && !strcmp(name, "x.o"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_STRING && !strcmp(name, "x.s"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_END);
    udict_free(udict);

    /* and so does an addition reallocating the attributes elsewhere, which
     * a memory checker reports otherwise, as the old buffer is freed */
    udict = moving_udict(mgr);
    static uint8_t big[4096];
    struct udict_opaque opaque = { .v = big, .size = sizeof(big) };
    ubase_assert(udict_set_opaque(udict, opaque, UDICT_TYPE_OPAQUE, "x.big"));
    name = NULL;
    type = UDICT_TYPE_FLOW_ERROR;
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_OPAQUE && !strcmp(name, "x.o"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_OPAQUE && !strcmp(name, "x.big"));
    ubase_assert(udict_iterate(udict, &name, &type));
    assert(type == UDICT_TYPE_END);
    udict_free(udict);
}

int main(int argc, char **argv)
{
    struct uprobe *uprobe = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_DEBUG);
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH, umem_mgr,
                                                   -1, -1);
    assert(mgr != NULL);

    test_iterate(mgr);

    struct udict *udict1 = udict_alloc(mgr, 0);
    assert(udict1 != NULL);

    uint8_t opaque[27];
    memset(opaque, 0xaa, sizeof(opaque));
    struct udict_opaque o;
    o.v = opaque;
    o.size = sizeof(opaque);
    ubase_assert(udict_set_opaque(udict1, o, UDICT_TYPE_OPAQUE, "x.opaque"));
    char opaque_hex[55];
    memset(opaque_hex, 'a', sizeof(opaque_hex) - 1);
    opaque_hex[sizeof(opaque_hex) - 1] = '\0';
    ubase_assert(udict_set_opaque_from_hex(udict1, opaque_hex,
                                           UDICT_TYPE_OPAQUE, "x.opaque_hex"));
    ubase_assert(udict_set_string(udict1, "pouet", UDICT_TYPE_FLOW_DEF, NULL));
    ubase_assert(udict_set_void(udict1, NULL, UDICT_TYPE_FLOW_ERROR, NULL));
    ubase_assert(udict_set_bool(udict1, true, UDICT_TYPE_BOOL, "x.truc"));
    ubase_assert(udict_set_unsigned(udict1, UINT64_MAX, UDICT_TYPE_CLOCK_DURATION,
                              NULL));
    ubase_assert(udict_set_int(udict1, INT64_MAX, UDICT_TYPE_INT, "x.date"));
    ubase_assert(udict_set_float(udict1, 1.0, UDICT_TYPE_FLOAT, "x.version"));
    ubase_assert(udict_set_string(udict1, SALUTATION, UDICT_TYPE_STRING,
                            "x.salutation"));
    struct urational rational = { .num = 64, .den = 45 };
    ubase_assert(udict_set_rational(udict1, rational, UDICT_TYPE_RATIONAL, "x.ar"));

    ubase_assert(udict_get_opaque(udict1, &o, UDICT_TYPE_OPAQUE, "x.opaque"));
    assert(o.size == sizeof(opaque));
    assert(!memcmp(opaque, o.v, sizeof(opaque)));
    ubase_assert(udict_get_opaque(udict1, &o, UDICT_TYPE_OPAQUE, "x.opaque_hex"));
    assert(o.size == sizeof(opaque));
    assert(!memcmp(opaque, o.v, sizeof(opaque)));
    const char *string;
    ubase_assert(udict_get_string(udict1, &string, UDICT_TYPE_FLOW_DEF, NULL));
    assert(!strcmp(string, "pouet"));
    ubase_nassert(udict_get_void(udict1, NULL, UDICT_TYPE_VOID, "f.eof"));
    ubase_assert(udict_get_void(udict1, NULL, UDICT_TYPE_FLOW_ERROR, NULL));

    ubase_assert(udict_delete(udict1, UDICT_TYPE_FLOW_ERROR, NULL));
    ubase_assert(udict_delete(udict1, UDICT_TYPE_FLOW_DEF, NULL));
    ubase_nassert(udict_delete(udict1, UDICT_TYPE_VOID, "x.truc"));
    ubase_nassert(udict_delete(udict1, UDICT_TYPE_BOOL, "k.pts"));

    bool b;
    ubase_assert(udict_get_bool(udict1, &b, UDICT_TYPE_BOOL, "x.truc"));
    assert(b);
    uint64_t u;
    ubase_assert(udict_get_unsigned(udict1, &u, UDICT_TYPE_CLOCK_DURATION, NULL));
    assert(u == UINT64_MAX);
    int64_t d;
    ubase_assert(udict_get_int(udict1, &d, UDICT_TYPE_INT, "x.date"));
    assert(d == INT64_MAX);
    double f;
    ubase_assert(udict_get_float(udict1, &f, UDICT_TYPE_FLOAT, "x.version"));
    assert(f == 1.0);
    ubase_assert(udict_get_string(udict1, &string, UDICT_TYPE_STRING,
                            "x.salutation"));
    assert(!strcmp(string, SALUTATION));
    struct urational r;
    ubase_assert(udict_get_rational(udict1, &r, UDICT_TYPE_RATIONAL, "x.ar"));
    assert(r.num == 64 && r.den == 45);

    /* TODO: Test all shorthand attributes. */

    o.v = opaque;
    o.size = sizeof(opaque);

    ubase_assert(udict_set_opaque(udict1, o, UDICT_TYPE_PIC_BAR_DATA, NULL));
    ubase_assert(udict_set_opaque(udict1, o, UDICT_TYPE_PIC_CEA_708, NULL));

    ubase_assert(udict_get_opaque(udict1, &o, UDICT_TYPE_PIC_CEA_708, NULL));
    assert(o.size == sizeof(opaque));
    assert(!memcmp(opaque, o.v, sizeof(opaque)));
    ubase_assert(udict_get_opaque(udict1, &o, UDICT_TYPE_PIC_BAR_DATA, NULL));
    assert(o.size == sizeof(opaque));
    assert(!memcmp(opaque, o.v, sizeof(opaque)));

    udict_dump(udict1, uprobe);

    struct udict *udict2 = udict_dup(udict1);
    assert(udict2 != NULL);
    udict_dump(udict2, uprobe);
    udict_free(udict2);

    udict2 = udict_copy(mgr, udict1);
    assert(udict2 != NULL);
    udict_dump(udict2, uprobe);
    udict_free(udict2);

    udict_free(udict1);

    {
        struct udict *udict1 = udict_alloc(mgr, 0);
        struct udict *udict2 = udict_alloc(mgr, 0);

        udict_set_string(udict1, "void.", UDICT_TYPE_STRING, "f.def");
        udict_set_string(udict2, "pic.", UDICT_TYPE_STRING, "f.def");
        assert(udict_cmp(udict1, udict2) != 0);
        udict_set_string(udict1, "pic.", UDICT_TYPE_STRING, "f.def");
        assert(udict_cmp(udict1, udict2) == 0);
        udict_free(udict1);
        udict_free(udict2);
    }

    udict_mgr_release(mgr);

    umem_mgr_release(umem_mgr);
    uprobe_release(uprobe);
    return 0;
}
