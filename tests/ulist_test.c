#include <upipe/ubase.h>
#include <upipe/ulist.h>

#include <assert.h>

struct item {
    struct uchain uchain;
    uint64_t id;
};

UBASE_FROM_TO(item, uchain, uchain, uchain)

int main(int argc, char **argv)
{
    struct uchain list;
    struct item items[1024];

    ulist_init(&list);
    assert(ulist_empty(&list));

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(items); i++) {
        uchain_init(&items[i].uchain);
        assert(!ulist_is_in(&items[i].uchain));
        items[i].id = i;
        ulist_add(&list, &items[i].uchain);
    }

    assert(ulist_is_first(&list, &items[0].uchain));
    for (unsigned i = 1; i < UBASE_ARRAY_SIZE(items); i++)
         assert(!ulist_is_first(&list, &items[i].uchain));
    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(items) - 1; i++)
        assert(!ulist_is_last(&list, &items[i].uchain));
    assert(ulist_is_last(&list, &items[1023].uchain));

    unsigned count = 0;
    struct uchain *uchain;
    ulist_foreach(&list, uchain) {
        struct item *item = item_from_uchain(uchain);
        assert(item->id == count++);
    }
    ulist_foreach_reverse(&list, uchain) {
        struct item *item = item_from_uchain(uchain);
        assert(item->id == --count);
    }
    assert(count == 0);

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(items); i++)
        assert(ulist_is_in(&items[i].uchain));

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(items); i++) {
        struct uchain *uchain = ulist_at(&list, i);
        assert(uchain != NULL);
        struct item *item = item_from_uchain(uchain);
        assert(item->id == i);
    }
    assert(ulist_at(&list, UBASE_ARRAY_SIZE(items)) == NULL);

    struct uchain *uchain_tmp;
    ulist_delete_foreach(&list, uchain, uchain_tmp) {
        ulist_delete(uchain);
        assert(!ulist_is_in(uchain));
    }

    for (unsigned i = UBASE_ARRAY_SIZE(items); i; i--) {
        ulist_unshift(&list, &items[i - 1].uchain);
        assert(ulist_is_in(&items[i - 1].uchain));
        uchain = ulist_peek(&list);
        struct item *item = item_from_uchain(uchain);
        assert(item->id == i - 1);
    }

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(items); i++) {
        uchain = ulist_pop(&list);
        struct item *item = item_from_uchain(uchain);
        assert(item->id == i);
    }

    assert(ulist_empty(&list));

    return 0;
}
