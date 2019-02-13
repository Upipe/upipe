/*
 * Copyright (C) 2018-2019 Open Broadcast Systems Ltd
 *
 * Author: Rafaël Carré
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
 * @short Upipe NIC PTP implementation of uclock
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/uclock_ptp.h>

#include <stdbool.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <net/if.h>
#ifdef __linux__
#include <linux/sockios.h>
#include <linux/ethtool.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <time.h>

/** super-set of the uclock structure with additional local members */
struct uclock_ptp {
    /** refcount management structure */
    struct urefcount urefcount;

    /** clock device fd */
    int fd[2];

#ifdef __linux__
    /** interface device fd */
    int if_fd[2];

    /** interface struct */
    struct ifreq ifr[2];
#endif

    /** structure exported to modules */
    struct uclock uclock;
};

UBASE_FROM_TO(uclock_ptp, uclock, uclock, uclock)
UBASE_FROM_TO(uclock_ptp, urefcount, urefcount, urefcount)

/** @internal */
static bool uclock_ptp_intf_up(struct uclock *uclock, int i)
{
    struct uclock_ptp *ptp = uclock_ptp_from_uclock(uclock);

#ifdef __linux__
    if (ioctl(ptp->if_fd[i], SIOCGIFFLAGS, &ptp->ifr[i]) >= 0)
        return ptp->ifr[i].ifr_flags & IFF_UP;
#else
    (void)ptp;
    (void)i;
#endif

    return false;
}

/** @This returns the current time in the given clock.
 *
 * @param uclock utility structure passed to the module
 * @return current system time in 27 MHz ticks
 */
static uint64_t uclock_ptp_now(struct uclock *uclock)
{
    struct uclock_ptp *ptp = uclock_ptp_from_uclock(uclock);

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd) ((~(clockid_t) (fd) << 3) | CLOCKFD)

    int idx = uclock_ptp_intf_up(uclock, 0) ? 0 : 1;

    struct timespec ts;
    if (unlikely(clock_gettime(FD_TO_CLOCKID(ptp->fd[idx]), &ts) == -1))
        return UINT64_MAX;

    uint64_t now = ts.tv_sec * UCLOCK_FREQ +
                   ts.tv_nsec * UCLOCK_FREQ / UINT64_C(1000000000);
    return now;
}

/** @This frees a uclock.
 *
 * @param urefcount pointer to urefcount
 */
static void uclock_ptp_free(struct urefcount *urefcount)
{
    struct uclock_ptp *ptp = uclock_ptp_from_urefcount(urefcount);
    urefcount_clean(urefcount);
    for (int i = 0; i < 2; i++) {
        ubase_clean_fd(&ptp->fd[i]);
#ifdef __linux__
        ubase_clean_fd(&ptp->if_fd[i]);
#endif
    }
    free(ptp);
}

/** @internal */
static int uclock_ptp_nic_clock_idx(struct uclock_ptp *ptp,
        struct uprobe *uprobe, int i, const char *interface)
{
#ifdef __linux__
    struct ethtool_ts_info info;

    memset(&info, 0, sizeof(info));
    info.cmd = ETHTOOL_GET_TS_INFO;
    strncpy(ptp->ifr[i].ifr_name, interface, IFNAMSIZ - 1);
    ptp->ifr[i].ifr_data = (char *) &info;

    if (ioctl(ptp->if_fd[i], SIOCETHTOOL, &ptp->ifr[i]) < 0) {
        uprobe_err_va(uprobe, NULL, "Couldn't get ethtool ts information for %s: %m",
            interface);
        info.phc_index = -1;
    }

    return info.phc_index;
#else
    /* TODO */
    return -1;
#endif
}

/** @internal */
static int uclock_ptp_open_nic(struct uclock_ptp *ptp, struct uprobe *uprobe,
        int i, const char *interface)
{
    int idx = uclock_ptp_nic_clock_idx(ptp, uprobe, i, interface);

    if (idx < 0) {
        uprobe_err_va(uprobe, NULL, "No PTP device found for %s", interface);
        return UBASE_ERR_EXTERNAL;
    }

    char clkdev[32];
    snprintf(clkdev, sizeof(clkdev), "/dev/ptp%u", idx);

    ptp->fd[i] = open(clkdev, O_RDWR);
    if (ptp->fd[i] < 0) {
        uprobe_err_va(uprobe, NULL, "Could not open PTP device %s: %m", clkdev);
        return UBASE_ERR_EXTERNAL;
    }

    return UBASE_ERR_NONE;
}

/** @This allocates a new uclock structure.
 *
 * @param uprobe probe catching log events for error reporting
 * @param interface NIC names
 * @return pointer to uclock, or NULL in case of error
 */
struct uclock *uclock_ptp_alloc(struct uprobe *uprobe, const char *interface[2])
{
    struct uclock_ptp *ptp = malloc(sizeof(struct uclock_ptp));
    if (unlikely(ptp == NULL))
        return NULL;

    struct urefcount *urefcount = uclock_ptp_to_urefcount(ptp);

    urefcount_init(urefcount, uclock_ptp_free);
    ptp->uclock.refcount = urefcount;
    ptp->uclock.uclock_now = uclock_ptp_now;
    ptp->uclock.uclock_to_real = NULL;
    ptp->uclock.uclock_from_real = NULL;

    for (int i = 0; i < 2; i++) {
        ptp->fd[i] = -1;
#ifdef __linux__
        ptp->if_fd[i] = -1;
#endif
    }

    for (int i = 0; i < 2 && interface[i]; i++) {
#ifdef __linux__
        ptp->if_fd[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (ptp->if_fd[i] < 0) {
            uprobe_err_va(uprobe, NULL, "%s: can't open socket (%m)",
                    interface[i]);
            goto err;
        }
        memset(&ptp->ifr[i], 0, sizeof(ptp->ifr[i]));
#endif
        if (!ubase_check(uclock_ptp_open_nic(ptp, uprobe, 0, interface[i])))
            goto err;
    }

    return uclock_ptp_to_uclock(ptp);

err:
    uclock_ptp_free(urefcount);
    return NULL;
}
