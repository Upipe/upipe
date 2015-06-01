#ifndef _UPIPE_MODULES_UPIPE_DUMP_H_
# define _UPIPE_MODULES_UPIPE_DUMP_H_
#ifdef __cplusplus
extern "C" {
#endif

# define UPIPE_DUMP_SIGNATURE UBASE_FOURCC('d','u','m','p')

enum upipe_dump_command {
    UPIPE_DUMP_SENTINAL = UPIPE_CONTROL_LOCAL,

    UPIPE_DUMP_SET_MAX_LEN,
};

static inline int upipe_dump_set_max_len(struct upipe *upipe, size_t len)
{
    return upipe_control(upipe, UPIPE_DUMP_SET_MAX_LEN, UPIPE_DUMP_SIGNATURE,
                         len);
}

struct upipe_mgr *upipe_dump_mgr_alloc(void);

#ifdef __cplusplus
}
#endif
#endif
