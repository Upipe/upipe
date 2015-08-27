#include <upipe/uuri.h>

#include <assert.h>

static void test_ipv4(void)
{
    const char *valid[] = {
        "0.0.0.0",
        "255.255.255.255",
        "192.168.27.1",
        "127.0.0.1",
    };

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(valid); i++) {
        printf("valid ipv4 %s\n", valid[i]);
        struct ustring ip = ustring_from_str(valid[i]);
        struct ustring ipv4 = uuri_parse_ipv4(&ip);
        assert(!ustring_is_null(ipv4) && !ip.len);
        printf("%.*s\n", (int)ipv4.len, ipv4.at);
    }

    const char *invalid[] = {
        "127.0.0.1.27",
        "256.0.0.0",
        "00.0.0.0",
        "127.0.0.1.",
        ".0.0.0.0",
        "0.0.0",
    };

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(invalid); i++) {
        printf("invalid ipv4 %s\n", invalid[i]);
        struct ustring ip = ustring_from_str(invalid[i]);
        struct ustring ipv4 = uuri_parse_ipv4(&ip);
        assert(ustring_is_null(ipv4) || ip.len);
    }
}

static void test_ipv6(void)
{
    {
        const char *valid[] = {
            "::",
            "::1",
            "1fff:0:a88:85a::ac1f",
            "0:0:0:0:0:FFFF:129.144.52.38",
            "0:0:0:0:0:0:192.168.27.1",
            "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
            "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255",
            "::ffff:ffff:ffff:ffff:ffff:ffff:ffff",
            "::ffff:ffff:ffff:ffff:ffff:255.255.255.255",
            "::ffff:ffff:ffff:ffff:ffff:ffff",
            "::ffff:ffff:ffff:ffff:255.255.255.255",
            "::ffff:ffff:ffff:ffff:ffff",
            "::ffff:ffff:ffff:255.255.255.255",
            "::ffff:ffff:ffff:ffff",
            "::ffff:ffff:255.255.255.255",
            "::ffff:ffff:ffff",
            "::ffff:255.255.255.255",
            "::ffff:ffff",
            "::255.255.255.255",
            "::ffff",
            "ffff::ffff:ffff:ffff:ffff:ffff:ffff",
            "ffff::ffff:ffff:ffff:ffff:255.255.255.255",
            "ffff::ffff:ffff:ffff:ffff:ffff",
            "ffff::ffff:ffff:ffff:255.255.255.255",
            "ffff::ffff:ffff:ffff:ffff",
            "ffff::ffff:ffff:255.255.255.255",
            "ffff::ffff:ffff:ffff",
            "ffff::ffff:255.255.255.255",
            "ffff::ffff:ffff",
            "ffff::255.255.255.255",
            "ffff::ffff",
            "ffff:ffff::ffff:ffff:ffff:ffff:ffff",
            "ffff:ffff::ffff:ffff:ffff:255.255.255.255",
            "ffff:ffff::ffff:ffff:ffff:ffff",
            "ffff:ffff::ffff:ffff:255.255.255.255",
            "ffff:ffff::ffff:ffff:ffff",
            "ffff:ffff::ffff:255.255.255.255",
            "ffff:ffff::ffff:ffff",
            "ffff:ffff::255.255.255.255",
            "ffff:ffff::ffff",
            "ffff:ffff:ffff::ffff:ffff:ffff:ffff",
            "ffff:ffff:ffff::ffff:ffff:255.255.255.255",
            "ffff:ffff:ffff::ffff:ffff:ffff",
            "ffff:ffff:ffff::ffff:255.255.255.255",
            "ffff:ffff:ffff::ffff:ffff",
            "ffff:ffff:ffff::255.255.255.255",
            "ffff:ffff:ffff::ffff",
            "ffff:ffff:ffff:ffff::ffff:ffff:ffff",
            "ffff:ffff:ffff:ffff::ffff:255.255.255.255",
            "ffff:ffff:ffff:ffff::ffff:ffff",
            "ffff:ffff:ffff:ffff::255.255.255.255",
            "ffff:ffff:ffff:ffff::ffff",
            "ffff:ffff:ffff:ffff:ffff::ffff:ffff",
            "ffff:ffff:ffff:ffff:ffff::255.255.255.255",
            "ffff:ffff:ffff:ffff:ffff::ffff",
            "ffff:ffff:ffff:ffff:ffff:ffff::ffff",
            "ffff:ffff:ffff:ffff:ffff:ffff:ffff::",
            "::%25eth0",
            "::%25%25",
        };

        for (size_t i = 0; i < UBASE_ARRAY_SIZE(valid); i++) {
            printf("valid ipv6 %s\n", valid[i]);
            struct ustring ip = ustring_from_str(valid[i]);
            struct ustring ipv6 = uuri_parse_ipv6_scoped(&ip);
            assert(ipv6.len == strlen(valid[i]) && !ip.len);
        }
    }

    {
        const char *invalid[] = {
            "fffff::1", "1::ffffa",
            "0:0:0:0:0:0:192.168.27",
            "0:0:0:0:0:0:192.168.27.",
            "0:0:0:0:0:0:192.168.27.1.0",
            "::ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
            "::ffff:ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255",
            "::ffff:ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255",
            "::%eth0",
            "::%25%5",
            "ffff::7%eth0",
        };

        for (size_t i = 0; i < UBASE_ARRAY_SIZE(invalid); i++) {
            printf("invalid ipv6 %s\n", invalid[i]);
            struct ustring ip = ustring_from_str(invalid[i]);
            struct ustring ipv6 = uuri_parse_ipv6_scoped(&ip);
            assert(ustring_is_null(ipv6) || ip.len);
        }
    }
}

static void test_host(void)
{
    {
        static const char *valid[] = {
            "[1fff:0:a88:85a::ac1f%25eth0]",
            "hostname",
            "192.168.27.0.1",
            "192.168.27.1",
        };

        for (size_t i = 0; i < UBASE_ARRAY_SIZE(valid); i++) {
            printf("valid host %s\n", valid[i]);
            struct ustring str = ustring_from_str(valid[i]);
            struct ustring host = uuri_parse_host(&str);
            assert(host.len == strlen(valid[i]) && !str.len);
        }
    }
}

static void test_scheme(void)
{
    const char *valid[] = {
        "http", "https", "file", "tel",
        "tel-0", "scheme.1.0-3", "tel+fax",
        "HTTP",
    };

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(valid); i++) {
        printf("valid scheme %s\n", valid[i]);
        struct ustring tmp = ustring_from_str(valid[i]);
        struct ustring scheme = uuri_parse_scheme(&tmp);
        assert(scheme.len == strlen(valid[i]) && !tmp.len);
    }

    const char *invalid[] = {
        "0tel", "", "file:", "http@"
    };

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(invalid); i++) {
        printf("invalid scheme %s\n", invalid[i]);
        struct ustring tmp = ustring_from_str(invalid[i]);
        struct ustring scheme = uuri_parse_scheme(&tmp);
        assert(ustring_is_null(scheme) || tmp.len);
    }
}

static void test_authority(void)
{
    {
        static const char *valid[] = {
            "",
            "host",
            "host:5004",
            "user@",
            "user@host:5004",
            "user:password@host:5004",
            "user@host",
        };

        for (size_t i = 0; i < UBASE_ARRAY_SIZE(valid); i++) {
            struct ustring str = ustring_from_str(valid[i]);
            printf("valid authority %s\n", valid[i]);
            struct uuri_authority authority = uuri_parse_authority(&str);
            assert(!uuri_authority_is_null(authority) && str.len == 0);
        }
    }
    {
        static const char *invalid[] = {
            "host:port",
            "host:port0",
            "host:0:",
            "user@host@",
        };

        for (size_t i = 0; i < UBASE_ARRAY_SIZE(invalid); i++) {
            struct ustring str = ustring_from_str(invalid[i]);
            printf("invalid authority %s\n", invalid[i]);
            struct uuri_authority authority = uuri_parse_authority(&str);
            assert(uuri_authority_is_null(authority) || str.len);
        }
    }
}

static void test_uri(void)
{
    {
        static const char *valid[] = {
            "scheme:",
            "scheme://",
            "scheme:///",
            "scheme:///?#",
            "scheme:?#",
            "scheme:#",
            "scheme:?",
            "scheme://user:password@host:5004?#",
            "scheme:/path/to/file",
            "scheme://192.168.27.1.1",
            "scheme://[ffff::7%25eth0]/",
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
            /* ipvfuture */
            "test://[v1.0:name:0]",
        };

        for (size_t i = 0; i < UBASE_ARRAY_SIZE(valid); i++) {
            struct ustring str = ustring_from_str(valid[i]);
            printf("valid uri %s\n", valid[i]);
            struct uuri uuri = uuri_parse(&str);
            assert(!uuri_is_null(uuri) && str.len == 0);
        }
    }
    {
        static const char *invalid[] = {
            "",
            "scheme",
            "scheme//:",
            "scheme///",
            "scheme://[ffff::7%eth0]/",
            "scheme://[v.0:name:0]",
            "scheme://[1.0:name:0]",
            "scheme://[v1.0:name?:0]",
        };

        for (size_t i = 0; i < UBASE_ARRAY_SIZE(invalid); i++) {
            struct ustring str = ustring_from_str(invalid[i]);
            printf("invalid uri %s\n", invalid[i]);
            struct uuri uuri = uuri_parse(&str);
            assert(uuri_is_null(uuri) || str.len);
        }
    }
}

static void test_escape(void)
{
    const char *paths[] = {
        "",
        "/path",
        "/path#",
        "/path###",
        "/path /",
        "/path#/to /?file",
    };

    for (size_t i = 0; i < UBASE_ARRAY_SIZE(paths); i++) {
        ssize_t len = uuri_escape_len(paths[i]);
        assert(len >= 0);
        char escape[len + 1];
        assert(uuri_escape(paths[i], escape, sizeof (escape)) >= 0);
        printf("escaped path %s -> %s\n", paths[i], escape);

        assert(uuri_unescape_len(escape) <= strlen(paths[i]));
        char unescape[strlen(paths[i]) + 1];
        assert(uuri_unescape(escape, unescape, sizeof (unescape)) >= 0);
        assert(!strcmp(paths[i], unescape));
    }
}

int main(int argc, char *argv[])
{
    test_ipv4();
    test_ipv6();
    test_scheme();
    test_host();
    test_authority();
    test_uri();
    test_escape();
    return 0;
}
