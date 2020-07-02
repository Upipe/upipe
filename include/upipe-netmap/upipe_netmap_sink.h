/** @file
 * @short Upipe netmap_enc module
 */

#ifndef _UPIPE_MODULES_UPIPE_NETMAP_SINK_H_
/** @hidden */
#define _UPIPE_MODULES_UPIPE_NETMAP_SINK_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_NETMAP_SINK_SIGNATURE UBASE_FOURCC('n','t','m','k')

/** @This extends upipe_command with specific commands. */
enum upipe_netmap_sink_command {
    UPIPE_NETMAP_SINK_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the audio subpipe (struct upipe **) */
    UPIPE_NETMAP_SINK_GET_AUDIO_SUB,
};

/** @This returns the audio subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the audio subpipe
 * @return an error code
 */
static inline int upipe_netmap_sink_get_audio_sub(struct upipe *upipe,
        struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_NETMAP_SINK_GET_AUDIO_SUB,
            UPIPE_NETMAP_SINK_SIGNATURE, upipe_p);
}

/** @This returns the management structure for netmap_sink pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_netmap_sink_mgr_alloc(void);

/** @hidden */
#define ARGS_DECL , const char *device
/** @hidden */
#define ARGS , device
UPIPE_HELPER_ALLOC(netmap_sink, UPIPE_NETMAP_SINK_SIGNATURE)
#undef ARGS
#undef ARGS_DECL

/*
 * Audio subpipe.
 */

#define UPIPE_NETMAP_SINK_AUDIO_SIGNATURE UBASE_FOURCC('n','t','m','a')

/** @This extends upipe_command with specific commands for netmap audio sink. */
enum upipe_netmap_sink_audio_command {
    UPIPE_NETMAP_SINK_AUDIO_SENTINEL = UPIPE_CONTROL_LOCAL,
    /* int, const char*, const char* */
    UPIPE_NETMAP_SINK_AUDIO_SET_FLOW_DESTINATION,
};

/** @This sets the destination details for the given flow.
 *
 * @param upipe description structure of the pipe
 * @param flow which flow this represents
 * @param path_1 destination IP and port for the first path.
 * @param path_2 destination IP and port for the second path. Can be NULL if
 * second path is not used.
 * @return an error code
 */
static inline int upipe_netmap_sink_audio_set_flow_destination(struct upipe *upipe,
        int flow, const char *path_1, const char *path_2)
{
    return upipe_control(upipe, UPIPE_NETMAP_SINK_AUDIO_SET_FLOW_DESTINATION,
            UPIPE_NETMAP_SINK_AUDIO_SIGNATURE, flow, path_1, path_2);
}

#ifdef __cplusplus
}
#endif
#endif
