/** @file
 * @short Upipe logging structure
 */

#ifndef _UPIPE_ULOG_H_
/** @hidden */
# define _UPIPE_ULOG_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ubase.h>
#include <upipe/ulist.h>
#include <upipe/uprobe.h>

/** @This defines the levels of log messages. */
enum uprobe_log_level {
    /** verbose messages, on a uref basis */
    UPROBE_LOG_VERBOSE,
    /** debug messages, not necessarily meaningful */
    UPROBE_LOG_DEBUG,
    /** notice messages, only informative */
    UPROBE_LOG_NOTICE,
    /** warning messages, the processing continues but may have unexpected
     * results */
    UPROBE_LOG_WARNING,
    /** error messages, the processing cannot continue */
    UPROBE_LOG_ERROR
};

/** @This describe a prefix tag for a log message. */
struct ulog_pfx {
    /** uchain to attach in @ref ulog prefixes field */
    struct uchain uchain;
    /** the prefix string */
    const char *tag;
};

UBASE_FROM_TO(ulog_pfx, uchain, uchain, uchain);

/** @This describe a log message. */
struct ulog {
    /** log level of the message */
    enum uprobe_log_level level;
    /** the message to be logged */
    const char *msg;
    /** list of prefix tags */
    struct uchain prefixes;
};

/** @This initializes an ulog structure.
 *
 * @param ulog pointer to the ulog structure to initialize
 * @param level the level of the log
 * @param msg the message to log
 */
static inline void ulog_init(struct ulog *ulog,
                             enum uprobe_log_level level,
                             const char *msg)
{
    ulog->level = level;
    ulog->msg = msg;
    ulist_init(&ulog->prefixes);
}

#ifdef __cplusplus
}
#endif
#endif
