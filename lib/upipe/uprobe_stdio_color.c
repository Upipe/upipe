/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * Authors: Arnaud de Turckheim
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

#include <upipe/ubase.h>
#include <upipe/uprobe.h>
#include <upipe/uprobe_stdio_color.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe/upipe.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

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


#define LEVEL(Level, Name)      ANSI_ESC(Level) Name ANSI_RESET
#define TAG(Tag) \
    ANSI_ESC(ANSI_BOLD ANSI_FG_BLACK) "[" \
    ANSI_ESC(ANSI_FG_CYAN) Tag \
    ANSI_ESC(ANSI_BOLD ANSI_FG_BLACK) "]" ANSI_RESET

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

const struct level level_unknown = {
    .log_level = UPROBE_LOG_ERROR,
    .name = "unknown",
    .color = ANSI_FG_RED,
};

/** @internal @This catches events thrown by pipes.
 *
 * @param uprobe pointer to probe
 * @param upipe pointer to pipe throwing the event
 * @param event event thrown
 * @param args optional event-specific parameters
 * @return an error code
 */
static int uprobe_stdio_color_throw(struct uprobe *uprobe,
                                    struct upipe *upipe,
                                    int event, va_list args)
{
    struct uprobe_stdio_color *uprobe_stdio_color =
        uprobe_stdio_color_from_uprobe(uprobe);
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct ulog *ulog = va_arg(args, struct ulog *);
    if (uprobe_stdio_color->min_level > ulog->level)
        return UBASE_ERR_NONE;

    size_t len = 0;
    struct uchain *uchain;
    ulist_foreach_reverse(&ulog->prefixes, uchain) {
        struct ulog_pfx *ulog_pfx = ulog_pfx_from_uchain(uchain);
        len += strlen(TAG() " ") + strlen(ulog_pfx->tag);
    }

    char buffer[len + 1];
    memset(buffer, 0, sizeof (buffer));
    char *tmp = buffer;
    ulist_foreach_reverse(&ulog->prefixes, uchain) {
        struct ulog_pfx *ulog_pfx = ulog_pfx_from_uchain(uchain);
        tmp += sprintf(tmp, TAG("%s") " ", ulog_pfx->tag);
    }

    struct level level = level_unknown;
    for (size_t i = 0; i < UBASE_ARRAY_SIZE(levels); i++)
        if (levels[i].log_level == ulog->level) {
            level = levels[i];
            break;
        }

    fprintf(uprobe_stdio_color->stream, LEVEL("%s", "%*s") ": %s %s\n",
            level.color, LEVEL_NAME_LEN, level.name, buffer, ulog->msg);

    return UBASE_ERR_NONE;
}

/** @This initializes an already allocated uprobe_stdio_color structure.
 *
 * @param uprobe_stdio_color pointer to the already allocated structure
 * @param next next probe to test if this one doesn't catch the event
 * @return pointer to uprobe, or NULL in case of error
 */
struct uprobe *uprobe_stdio_color_init(
    struct uprobe_stdio_color *uprobe_stdio_color,
    struct uprobe *next,
    FILE *stream, enum uprobe_log_level min_level)
{
    assert(uprobe_stdio_color != NULL);
    struct uprobe *uprobe = uprobe_stdio_color_to_uprobe(uprobe_stdio_color);
    uprobe_init(uprobe, uprobe_stdio_color_throw, next);
    uprobe_stdio_color->stream = stream;
    uprobe_stdio_color->min_level = min_level;
    return uprobe;
}

/** @This cleans a uprobe_stdio_color structure.
 *
 * @param uprobe_stdio_color structure to clean
 */
void uprobe_stdio_color_clean(struct uprobe_stdio_color *uprobe_stdio_color)
{
    assert(uprobe_stdio_color != NULL);
    struct uprobe *uprobe = uprobe_stdio_color_to_uprobe(uprobe_stdio_color);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next, FILE *stream, enum uprobe_log_level min_level
#define ARGS next, stream, min_level
UPROBE_HELPER_ALLOC(uprobe_stdio_color)
#undef ARGS
#undef ARGS_DECL
