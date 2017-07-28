#include <upipe/uref_dump.h>
#include <upipe/uref_block.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_urefcount.h>
#include <upipe/upipe_helper_void.h>
#include <upipe/upipe_helper_output.h>
#include <upipe-modules/upipe_dump.h>

#include <ctype.h>

struct upipe_dump {
    /** urefcount management structure */
    struct urefcount urefcount;
    /** public upipe structure */
    struct upipe upipe;

    /** for UPIPE_HELPER_OUTPUT */
    struct upipe *output;
    enum upipe_helper_output_state output_state;
    struct uref *flow_def;
    struct uchain request_list;

    /** max dump len */
    size_t max_len;
};

UPIPE_HELPER_UPIPE(upipe_dump, upipe, UPIPE_DUMP_SIGNATURE)
UPIPE_HELPER_UREFCOUNT(upipe_dump, urefcount, upipe_dump_free)
UPIPE_HELPER_VOID(upipe_dump)
UPIPE_HELPER_OUTPUT(upipe_dump, output, flow_def, output_state, request_list)

static struct upipe *upipe_dump_alloc(struct upipe_mgr *mgr,
                                      struct uprobe *uprobe,
                                      uint32_t signature,
                                      va_list args)
{
    struct upipe *upipe = upipe_dump_alloc_void(mgr, uprobe, signature, args);

    upipe_dump_init_urefcount(upipe);
    upipe_dump_init_output(upipe);

    struct upipe_dump *upipe_dump = upipe_dump_from_upipe(upipe);
    upipe_dump->max_len = (size_t)-1;

    upipe_throw_ready(upipe);

    return upipe;
}

static void upipe_dump_free(struct upipe *upipe)
{
    upipe_throw_dead(upipe);

    upipe_dump_clean_output(upipe);
    upipe_dump_clean_urefcount(upipe);
    upipe_dump_free_void(upipe);
}

static void upipe_dump_line(struct upipe *upipe, unsigned int at,
                            uint8_t *line, int size)
{
    char hex[16 * 2 + 16 + 1];
    char *tmp = hex;

    if (!size)
        return;

    for (unsigned i = 0; i < 16; i++) {
        const char *sep = i ? (i == 8 ? "  " : " "): "";
        if (i < size)
            tmp += snprintf(tmp, hex + sizeof (hex) - tmp,
                            "%s%02x", sep, line[i]);
        else
            tmp += snprintf(tmp, hex + sizeof (hex) - tmp, "%s  ", sep);
        assert(tmp < hex + sizeof (hex));
    }

    char ascii[16 + 1];
    tmp = ascii;
    for (unsigned i = 0; i < size; i++) {
        tmp += snprintf(tmp, ascii + sizeof (ascii) - tmp,
                        "%c", isprint(line[i]) ? line[i] : '.');
        assert(tmp < ascii + sizeof (ascii));
    }

    upipe_notice_va(upipe, "hexdump: %08x  %s  |%s|", at, hex, ascii);
}

static void upipe_dump_input(struct upipe *upipe, struct uref *uref,
                             struct upump **upump_p)
{
    struct upipe_dump *upipe_dump = upipe_dump_from_upipe(upipe);

    uref_dump(uref, upipe->uprobe);

    size_t total_size;
    ubase_assert(uref_block_size(uref, &total_size));

    upipe_notice_va(upipe, "dumping ubuf %p of size %zu",
                    uref->ubuf, total_size);

    if (upipe_dump->max_len != (size_t)-1 &&
        total_size > upipe_dump->max_len)
        total_size = upipe_dump->max_len;

    unsigned int count = 0;
    uint8_t line[16];

    int offset = 0;
    while (total_size) {
        const uint8_t *buf;
        int size = total_size;

        ubase_assert(uref_block_read(uref, offset, &size, &buf));
        assert(size != 0);

        total_size -= size;

        for (unsigned i = 0; i < size; i++, count++) {
            line[count % 16] = buf[i];

            if (!((count + 1) % 16) || (!total_size && (i + 1 == size)))
                upipe_dump_line(upipe, count - (count % 16),
                                line, (count % 16) + 1);
        }

        ubase_assert(uref_block_unmap(uref, offset));
        offset += size;
    }
#define UPIPE_DUMP_SEP \
    "--------------------------------------------" \
    "-------------------------------------------"
    upipe_notice(upipe, UPIPE_DUMP_SEP);
    upipe_dump_output(upipe, uref, upump_p);
}

static int _upipe_dump_set_max_len(struct upipe *upipe, size_t max_len)
{
    struct upipe_dump *upipe_dump = upipe_dump_from_upipe(upipe);
    upipe_dump->max_len = max_len;
    return UBASE_ERR_NONE;
}

static int upipe_dump_set_flow_def(struct upipe *upipe, struct uref *flow_def)
{
    int ret = uref_flow_match_def(flow_def, "block.");
    if (!ubase_check(ret))
        return ret;

    struct uref *flow_def_dup = uref_dup(flow_def);
    if (unlikely(flow_def_dup == NULL))
        return UBASE_ERR_ALLOC;

    upipe_dump_store_flow_def(upipe, flow_def_dup);
    return UBASE_ERR_NONE;
}

static int upipe_dump_control(struct upipe *upipe, int command, va_list args)
{
    UBASE_HANDLED_RETURN(upipe_dump_control_output(upipe, command, args));
    switch (command) {
    case UPIPE_SET_FLOW_DEF: {
        struct uref *flow_def = va_arg(args, struct uref *);
        return upipe_dump_set_flow_def(upipe, flow_def);
    }

    case UPIPE_DUMP_SET_MAX_LEN: {
        UBASE_SIGNATURE_CHECK(args, UPIPE_DUMP_SIGNATURE)
        size_t max_len = va_arg(args, size_t);
        return _upipe_dump_set_max_len(upipe, max_len);
    }

    default:
        return UBASE_ERR_UNHANDLED;
    }
}

static struct upipe_mgr upipe_dump_mgr = {
    .refcount = NULL,
    .signature = UPIPE_DUMP_SIGNATURE,

    .upipe_alloc = upipe_dump_alloc,
    .upipe_input = upipe_dump_input,
    .upipe_control = upipe_dump_control,

    .upipe_mgr_control = NULL,
};

struct upipe_mgr *upipe_dump_mgr_alloc(void)
{
    return &upipe_dump_mgr;
}
