/*
 * Copyright (C) 2012-2017 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Arnaud de Turckheim
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
 * @short probe outputting all log events to stdio
 */

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/** maximum level name length */
#define LEVEL_NAME_LEN  7

#define ANSI_ESC(Value)         "\e[" Value "m"
#define ANSI_RESET              ANSI_ESC()

#define ANSI_ATTR_BOLD          "1"
#define ANSI_ATTR_FAINT         "2"
#define ANSI_ATTR(Attr)         ";" ANSI_ATTR_##Attr
#define ANSI_BOLD               ANSI_ATTR(BOLD)
#define ANSI_FAINT              ANSI_ATTR(FAINT)

#define ANSI_COLOR_BLACK        "0"
#define ANSI_COLOR_RED          "1"
#define ANSI_COLOR_GREEN        "2"
#define ANSI_COLOR_YELLOW       "3"
#define ANSI_COLOR_BLUE         "4"
#define ANSI_COLOR_MAGENTA      "5"
#define ANSI_COLOR_CYAN         "6"
#define ANSI_COLOR_WHITE        "7"
#define ANSI_COLOR_FG(Color)    ";3" ANSI_COLOR_##Color
#define ANSI_FG_READ            ANSI_COLOR_FG(RED)
#define ANSI_FG_BLACK           ANSI_COLOR_FG(BLACK)
#define ANSI_FG_CYAN            ANSI_COLOR_FG(CYAN)
#define ANSI_FG_BLACK           ANSI_COLOR_FG(BLACK)
#define ANSI_FG_RED             ANSI_COLOR_FG(RED)
#define ANSI_FG_GREEN           ANSI_COLOR_FG(GREEN)
#define ANSI_FG_YELLOW          ANSI_COLOR_FG(YELLOW)
#define ANSI_FG_BLUE            ANSI_COLOR_FG(BLUE)
#define ANSI_FG_MAGENTA         ANSI_COLOR_FG(MAGENTA)
#define ANSI_FG_CYAN            ANSI_COLOR_FG(CYAN)
#define ANSI_FG_WHITE           ANSI_COLOR_FG(WHITE)
#define ANSI_COLOR_BG(Color)    ";4" ANSI_COLOR_##Color


#define LEVEL_COLOR(Level, Name)      ANSI_ESC(Level) Name ANSI_RESET
#define LEVEL_NOCOLOR(Name)           Name

#define TAG_COLOR(Tag) \
    ANSI_ESC(ANSI_BOLD ANSI_FG_BLACK) "[" \
    ANSI_ESC(ANSI_FG_CYAN) Tag \
    ANSI_ESC(ANSI_BOLD ANSI_FG_BLACK) "]" ANSI_RESET
#define TAG_NOCOLOR(Tag) \
    "[" Tag "]"

struct level {
    enum uprobe_log_level log_level;
    const char *name;
    const char *color;
};

static const struct level levels[] = {
    {
        .log_level = UPROBE_LOG_VERBOSE,
        .name = "verbose",
        .color = ANSI_FG_WHITE,
    },
    {
        .log_level = UPROBE_LOG_DEBUG,
        .name = "debug",
        .color = ANSI_FG_GREEN,
    },
    {
        .log_level = UPROBE_LOG_NOTICE,
        .name = "notice",
        .color = ANSI_FG_BLUE,
    },
    {
        .log_level = UPROBE_LOG_WARNING,
        .name = "warning",
        .color = ANSI_FG_YELLOW,
    },
    {
        .log_level = UPROBE_LOG_ERROR,
        .name = "error",
        .color = ANSI_FG_RED,
    },
};

static const struct level level_unknown = {
    .log_level = UPROBE_LOG_ERROR,
    .name = "unknown",
    .color = ANSI_FG_RED,
};

/** @internal @This returns the corresponding level description of an ulog.
 *
 * @param ulog ulog structure
 * @return a pointer to the corresponding level description
 */
static const struct level *uprobe_stdio_get_level(struct ulog *ulog)
{
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(levels); i++)
        if (levels[i].log_level == ulog->level)
            return &levels[i];
    return &level_unknown;
}

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_stdio_throw(struct uprobe *uprobe, struct upipe *upipe,
                              int event, va_list args)
{
    struct uprobe_stdio *uprobe_stdio = uprobe_stdio_from_uprobe(uprobe);
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct ulog *ulog = va_arg(args, struct ulog *);
    if (uprobe_stdio->min_level > ulog->level)
        return UBASE_ERR_NONE;

    bool colored = uprobe_stdio->colored;
    size_t len = 0;
    struct uchain *uchain;
    ulist_foreach_reverse(&ulog->prefixes, uchain) {
        struct ulog_pfx *ulog_pfx = ulog_pfx_from_uchain(uchain);

        len += colored ? strlen(TAG_COLOR() " ") : strlen(TAG_NOCOLOR() " ");
        len += strlen(ulog_pfx->tag);
    }

    char buffer[len + 1];
    buffer[0] = '\0';
    char *tmp = buffer;
    ulist_foreach_reverse(&ulog->prefixes, uchain) {
        struct ulog_pfx *ulog_pfx = ulog_pfx_from_uchain(uchain);
        if (colored)
            tmp += sprintf(tmp, TAG_COLOR("%s") " ", ulog_pfx->tag);
        else
            tmp += sprintf(tmp, TAG_NOCOLOR("%s") " ", ulog_pfx->tag);
    }

    const struct level *level = uprobe_stdio_get_level(ulog);

    if (colored)
        fprintf(uprobe_stdio->stream,
                LEVEL_COLOR("%s", "%*s") ": %s%s\n",
                level->color, LEVEL_NAME_LEN, level->name, buffer, ulog->msg);
    else
        fprintf(uprobe_stdio->stream,
                LEVEL_NOCOLOR("%s") ": %s%s\n",
                level->name, buffer, ulog->msg);

    return UBASE_ERR_NONE;
}

/** @This initializes an already allocated uprobe_stdio structure.
 *
 * @param uprobe_stdio pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @param stream stdio stream to which to log the messages
 * @param level level at which to log the messages
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_stdio_init(struct uprobe_stdio *uprobe_stdio,
                                 struct uprobe *next, FILE *stream,
                                 enum uprobe_log_level min_level)
{
    assert(uprobe_stdio != NULL);
    struct uprobe *uprobe = uprobe_stdio_to_uprobe(uprobe_stdio);
    uprobe_stdio->stream = stream;
    uprobe_stdio->min_level = min_level;
    uprobe_stdio->colored = isatty(fileno(stream));
    uprobe_init(uprobe, uprobe_stdio_throw, next);
    return uprobe;
}

/** @This cleans a uprobe_stdio structure.
 *
 * @param uprobe_stdio structure to clean
 */
void uprobe_stdio_clean(struct uprobe_stdio *uprobe_stdio)
{
    assert(uprobe_stdio != NULL);
    struct uprobe *uprobe = uprobe_stdio_to_uprobe(uprobe_stdio);
    uprobe_clean(uprobe);
}

/** @This enables or disables colored output.
 *
 * @param uprobe pointer to probe
 * @param enabled enable (or disable) colored output
 */
void uprobe_stdio_set_color(struct uprobe *uprobe, bool enabled)
{
    struct uprobe_stdio *uprobe_stdio = uprobe_stdio_from_uprobe(uprobe);
    uprobe_stdio->colored = enabled;
}

#define ARGS_DECL struct uprobe *next, FILE *stream, enum uprobe_log_level min_level
#define ARGS next, stream, min_level
UPROBE_HELPER_ALLOC(uprobe_stdio)
#undef ARGS
#undef ARGS_DECL
