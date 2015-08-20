#include <upipe/uprobe.h>
#include <upipe/uprobe_helper_alloc.h>
#include <upipe-modules/upipe_http_source.h>
#include <upipe-modules/uprobe_http_redirect.h>

static int uprobe_http_redir_throw(struct uprobe *uprobe,
                                   struct upipe *upipe,
                                   int event, va_list args)
{
    if (event != UPROBE_HTTP_SRC_REDIRECT)
        return uprobe_throw_next(uprobe, upipe, event, args);

    UBASE_SIGNATURE_CHECK(args, UPIPE_HTTP_SRC_SIGNATURE)
    const char *uri = va_arg(args, const char *);
    return upipe_set_uri(upipe, uri);
}

struct uprobe *uprobe_http_redir_init(
    struct uprobe_http_redir *uprobe_http_redir,
    struct uprobe *next)
{
    assert(uprobe_http_redir != NULL);
    struct uprobe *uprobe = uprobe_http_redir_to_uprobe(uprobe_http_redir);
    uprobe_init(uprobe, uprobe_http_redir_throw, next);
    return uprobe;
}

void uprobe_http_redir_clean(struct uprobe_http_redir *uprobe_http_redir)
{
    assert(uprobe_http_redir != NULL);
    struct uprobe *uprobe = uprobe_http_redir_to_uprobe(uprobe_http_redir);
    uprobe_clean(uprobe);
}

#define ARGS_DECL struct uprobe *next
#define ARGS next
UPROBE_HELPER_ALLOC(uprobe_http_redir)
#undef ARGS
#undef ARGS_DECL
