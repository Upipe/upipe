#ifndef _UPIPE_UPROBE_STDIO_COLOR_H_
/** @hidden */
#define _UPIPE_UPROBE_STDIO_COLOR_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_uprobe.h>

/** @This is a super-set of the uprobe structure with additional local
 * members. */
struct uprobe_stdio_color {
    /** file stream to write to */
    FILE *stream;
    /** minimum level of printed messages */
    enum uprobe_log_level min_level;

    /** structure exported to modules */
    struct uprobe uprobe;
};

UPROBE_HELPER_UPROBE(uprobe_stdio_color, uprobe)

/** @This initializes an already allocated uprobe_stdio_color structure.
 *
 * @param uprobe_stdio color  pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param stream stdio stream to which to log the messages
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_stdio_color_init(
    struct uprobe_stdio_color *uprobe_stdio_color,
    struct uprobe *next,
    FILE *stream, enum uprobe_log_level min_level);

/** @This cleans a uprobe_stdio_color structure.
 *
 * @param uprobe_stdio_color structure to clean
 */
void uprobe_stdio_color_clean(struct uprobe_stdio_color *uprobe_stdio_color);

/** @This allocates a new uprobe stdio color structure.
 *
 * @param next next probe to test if this one doesn't catch the event
 * @param stream stdio stream to which to log the messages
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_stdio_color_alloc(struct uprobe *next, FILE *stream,
                                  enum uprobe_log_level min_level);

#ifdef __cplusplus
}
#endif
#endif
