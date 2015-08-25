#undef NDEBUG

#include <upipe/umem.h>
#include <upipe/umem_alloc.h>
#include <upipe/udict.h>
#include <upipe/udict_inline.h>
#include <upipe/uref.h>
#include <upipe/uref_std.h>
#include <upipe/uref_uri.h>
#include <upipe/uref_dump.h>
#include <upipe/uprobe_stdio.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>

#define UDICT_POOL_DEPTH 1
#define UREF_POOL_DEPTH 1

static int catch(struct uprobe *uprobe,
                 struct upipe *upipe,
                 int event, va_list args)
{
    if (event != UPROBE_LOG)
        return uprobe_throw_next(uprobe, upipe, event, args);

    struct ulog *ulog = va_arg(args, struct ulog *);
    if (!strncmp(ulog->msg, "dumping ", strlen("dumping ")) ||
        !strncmp(ulog->msg, "end of attributes for udict ",
                 strlen("end of attributes for udict ")))
        return UBASE_ERR_NONE;

    return uprobe_throw(uprobe->next, upipe, UPROBE_LOG, ulog);
}

int main(int argc, char **argv)
{
    const char *uri[] = {
        "http://upipe.org",
        "http://upipe.org/",
        "http://upipe.org/index.html",
        "http://upipe.org:8080/index.html",
        "http://Meuuh@upipe.org:8080/index.html",
        "http://Meuuh@upipe.org:8080/index.html?query=toto#fragment",
        "http://127.0.0.1/index.html",
        "file:///home/user/file.ext",
        "file:/home/",
        "test:?query=test#fragment",
        /* from rfc 3986 */
        "ftp://ftp.is.co.za/rfc/rfc1808.txt",
        "http://www.ietf.org/rfc/rfc2396.txt",
        "ldap://[2001:db8::7]/c=GB?objectClass?one",
        "mailto:John.Doe@example.com",
        "news:comp.infosystems.www.servers.unix",
        "tel:+1-816-555-1212",
        "telnet://192.0.2.16:80/",
        "urn:oasis:names:specification:docbook:dtd:xml:4.1.2",
    };
    struct umem_mgr *umem_mgr = umem_alloc_mgr_alloc();
    assert(umem_mgr != NULL);
    struct udict_mgr *udict_mgr = udict_inline_mgr_alloc(UDICT_POOL_DEPTH,
                                                         umem_mgr, -1, -1);
    assert(udict_mgr != NULL);
    struct uref_mgr *mgr = uref_std_mgr_alloc(UREF_POOL_DEPTH, udict_mgr, 0);
    assert(mgr != NULL);

    struct uprobe *uprobe = uprobe_stdio_alloc(NULL, stdout, UPROBE_LOG_DEBUG);
    assert(uprobe);
    struct uprobe uprobe_log_filter;
    uprobe_init(&uprobe_log_filter, catch, uprobe);
    uprobe = &uprobe_log_filter;

    for (unsigned i = 0; i < UBASE_ARRAY_SIZE(uri); i++) {
        struct uref *uref = uref_alloc(mgr);
        assert(uref);
        uprobe_dbg_va(uprobe, NULL, "uri: %s", uri[i]);
        ubase_assert(uref_uri_set_from_str(uref, uri[i]));
        uref_dump(uref, uprobe);
        char *str;
        ubase_assert(uref_uri_get_to_str(uref, &str));
        uprobe_dbg_va(uprobe, NULL, "uri: %s", str);
        assert(strcmp(str, uri[i]) == 0);
        free(str);
        uref_free(uref);
    }

    uprobe_clean(&uprobe_log_filter);
    uprobe_release(uprobe);
    uref_mgr_release(mgr);
    udict_mgr_release(udict_mgr);
    umem_mgr_release(umem_mgr);
    return 0;
}
