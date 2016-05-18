#include <upipe/ustring.h>
#include <upipe/uclock.h>

#include <limits.h>
#include <errno.h>

#define USTRING_FROM_STR(str)   { .at = (char *)str, .len = sizeof (str) - 1 }

struct ustring_uint64 ustring_to_uint64(const struct ustring str, int base)
{
    struct ustring_uint64 ret = { ustring_null(), 0 };
    if (ustring_is_null(str))
        return ret;

    char buf[str.len + 1];
    memcpy(buf, str.at, str.len);
    buf[str.len] = '\0';

    char *endptr;
    ret.value = strtoull(buf, &endptr, base);
    if (endptr == buf || (ret.value == ULLONG_MAX && errno == ERANGE))
        return ret;
    ret.str = ustring_truncate(str, endptr - buf);
    return ret;
}

struct ustring_time ustring_to_time(const struct ustring str)
{
    static const struct {
        struct ustring sfx;
        uint64_t mul;
    } sfxs[] = {
        /* milisecond */ { USTRING_FROM_STR("ms"), UCLOCK_FREQ / 1000 },
        /* second */     { USTRING_FROM_STR("s"), UCLOCK_FREQ },
        /* minute */     { USTRING_FROM_STR("m"), UCLOCK_FREQ * 60 },
        /* heure */      { USTRING_FROM_STR("h"), UCLOCK_FREQ * 60 * 60 },
    };

    struct ustring_time ret = { ustring_null(), 0 };
    struct ustring_uint64 value = ustring_to_uint64(str, 10);
    if (ustring_is_empty(value.str))
        return ret;
    ret.str = value.str;
    ret.value = value.value;

    struct ustring sfx = ustring_shift(str, value.str.len);
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(sfxs); i++)
        if (ustring_match(sfx, sfxs[i].sfx)) {
            ret.value *= sfxs[i].mul;
            ret.str = ustring_truncate(str, value.str.len + sfxs[i].sfx.len);
            break;
        }
    return ret;
}

struct ustring_size ustring_to_size(const struct ustring str)
{
    static const struct {
        struct ustring sfx;
        uint64_t mul;
    } sfxs[] = {
        /* Kibi */ { USTRING_FROM_STR("Ki"), 1024 },
        /* Mebi */ { USTRING_FROM_STR("Mi"), 1024 * 1024 },
        /* Gibi */ { USTRING_FROM_STR("Gi"), 1024 * 1024 * 1024 },
        /* Kilo */ { USTRING_FROM_STR("K"), 1000 },
        /* Mega */ { USTRING_FROM_STR("M"), 1000 * 1000 },
        /* Giga */ { USTRING_FROM_STR("G"), 1000 * 1000 * 1000 },
    };

    struct ustring_size ret = { ustring_null(), 0 };
    struct ustring_uint64 value = ustring_to_uint64(str, 10);
    if (ustring_is_empty(value.str))
        return ret;
    ret.str = value.str;
    ret.value = value.value;

    struct ustring sfx = ustring_shift(str, value.str.len);
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(sfxs); i++)
        if (ustring_match(sfx, sfxs[i].sfx)) {
            ret.value *= sfxs[i].mul;
            ret.str = ustring_truncate(str, value.str.len + sfxs[i].sfx.len);
            break;
        }
    return ret;
}
