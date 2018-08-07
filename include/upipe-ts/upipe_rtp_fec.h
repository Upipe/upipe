/** @file
 * @short Upipe rtp-fec (ffmpeg) module
 */

#ifndef _UPIPE_TS_UPIPE_RTP_FEC_H_
/** @hidden */
#define _UPIPE_TS_UPIPE_RTP_FEC_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/upipe.h>

#define UPIPE_RTP_FEC_SIGNATURE UBASE_FOURCC('r','f','c',' ')
#define UPIPE_RTP_FEC_INPUT_SIGNATURE UBASE_FOURCC('r','f','c','i')

/** @This extends upipe_command with specific commands for avcodec decode. */
enum rtp_fec_command {
    UPIPE_RTP_FEC_SENTINEL = UPIPE_CONTROL_LOCAL,

    /** returns the main subpipe (struct upipe **) */
    UPIPE_RTP_FEC_GET_MAIN_SUB,
    /** returns the fec-column subpipe (struct upipe **) */
    UPIPE_RTP_FEC_GET_COL_SUB,
    /** returns the fec-row subpipe (struct upipe **) */
    UPIPE_RTP_FEC_GET_ROW_SUB,
    /** returns the number of non recovered packets (uint64_t *) */
    UPIPE_RTP_FEC_GET_PACKETS_LOST,
    /** returns the number of recovered packets (uint64_t *) */
    UPIPE_RTP_FEC_GET_PACKETS_RECOVERED,
    /** returns the number of rows (uint64_t *) */
    UPIPE_RTP_FEC_GET_ROWS,
    /** returns the number of columns (uint64_t *) */
    UPIPE_RTP_FEC_GET_COLUMNS,
    /** sets expected payload type (unsigned) */
    UPIPE_RTP_FEC_SET_PT,
};

static inline int upipe_rtp_fec_get_rows(struct upipe *upipe,
        uint64_t *rows)
{
    return upipe_control(upipe, UPIPE_RTP_FEC_GET_ROWS,
            UPIPE_RTP_FEC_SIGNATURE, rows);
}

static inline int upipe_rtp_fec_get_columns(struct upipe *upipe,
        uint64_t *columns)
{
    return upipe_control(upipe, UPIPE_RTP_FEC_GET_COLUMNS,
            UPIPE_RTP_FEC_SIGNATURE, columns);
}

static inline int upipe_rtp_fec_get_packets_lost(struct upipe *upipe,
        uint64_t *lost)
{
    return upipe_control(upipe, UPIPE_RTP_FEC_GET_PACKETS_LOST,
            UPIPE_RTP_FEC_SIGNATURE, lost);
}

static inline int upipe_rtp_fec_get_packets_recovered(struct upipe *upipe,
        uint64_t *recovered)
{
    return upipe_control(upipe, UPIPE_RTP_FEC_GET_PACKETS_RECOVERED,
            UPIPE_RTP_FEC_SIGNATURE, recovered);
}

/** @This returns the pic subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the pic subpipe
 * @return an error code
 */
static inline int upipe_rtp_fec_get_main_sub(struct upipe *upipe,
                                             struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_RTP_FEC_GET_MAIN_SUB,
                         UPIPE_RTP_FEC_SIGNATURE, upipe_p);
}

/** @This returns the pic subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the pic subpipe
 * @return an error code
 */
static inline int upipe_rtp_fec_get_col_sub(struct upipe *upipe,
                                            struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_RTP_FEC_GET_COL_SUB,
                         UPIPE_RTP_FEC_SIGNATURE, upipe_p);
}

/** @This returns the pic subpipe. The refcount is not incremented so you
 * have to use it if you want to keep the pointer.
 *
 * @param upipe description structure of the super pipe
 * @param upipe_p filled in with a pointer to the pic subpipe
 * @return an error code
 */
static inline int upipe_rtp_fec_get_row_sub(struct upipe *upipe,
                                            struct upipe **upipe_p)
{
    return upipe_control(upipe, UPIPE_RTP_FEC_GET_ROW_SUB,
                         UPIPE_RTP_FEC_SIGNATURE, upipe_p);
}

/** @This sets the expected payload type
 *
 * @param upipe description structure of the super pipe
 * @param pt expected payload type
 * @return an error code
 */
static inline int upipe_rtp_fec_set_pt(struct upipe *upipe, unsigned pt)
{
    return upipe_control(upipe, UPIPE_RTP_FEC_SET_PT,
                         UPIPE_RTP_FEC_SIGNATURE, pt);
}

/** @This returns the management structure for rtp_fec pipes.
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_rtp_fec_mgr_alloc(void);

/** @This allocates and initializes a rtp fec pipe.
 *
 * @param mgr management structure for rtp fec type
 * @param uprobe structure used to raise events for the super pipe
 * @param uprobe_pic structure used to raise events for the pic subpipe
 * @param uprobe_sound structure used to raise events for the sound subpipe
 * @param uprobe_subpic structure used to raise events for the subpic subpipe
 * @return pointer to allocated pipe, or NULL in case of failure
 */
static inline struct upipe *upipe_rtp_fec_alloc(struct upipe_mgr *mgr,
                                                struct uprobe *uprobe,
                                                struct uprobe *uprobe_main,
                                                struct uprobe *uprobe_col,
                                                struct uprobe *uprobe_row)
{
    return upipe_alloc(mgr, uprobe, UPIPE_RTP_FEC_SIGNATURE,
                        uprobe_main, uprobe_col, uprobe_row);
}

#ifdef __cplusplus
}
#endif
#endif
