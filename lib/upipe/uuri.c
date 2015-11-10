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
#include <upipe/uuri.h>

#include <ctype.h>
#include <stdlib.h>
#include <ctype.h>

#define UURI_GEN_DELIMS ':', '/', '?', '#', '[', ']', '@'
#define UURI_SUB_DELIMS '!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '='
#define UURI_RESERVED UURI_GEN_DELIMS, UURI_SUB_DELIMS
#define UURI_UNRESERVED USTRING_ALPHA, USTRING_DIGIT, '-', '.', '_', '~'

static const char uuri_escape_set[] = { UURI_RESERVED, 0 };

static const char uuri_path_set[] = {
    UURI_UNRESERVED, UURI_SUB_DELIMS, ':', '@', '/', 0
};

static const char uuri_query_set[] = {
    UURI_UNRESERVED, UURI_SUB_DELIMS, ':', '@', '/', '?', 0
};

static const char uuri_fragment_set[] = {
    UURI_UNRESERVED, UURI_SUB_DELIMS, ':', '@', '/', '?', 0
};

static inline struct ustring ustring_split_pct_encoded(struct ustring *ustring)
{
    if (ustring->len >= 3 &&
        ustring->at[0] == '%' &&
        isxdigit(ustring->at[1]) &&
        isxdigit(ustring->at[2])) {
        struct ustring tmp = ustring_truncate(*ustring, 3);
        *ustring = ustring_shift(*ustring, 3);
        return tmp;
    }
    return ustring_null();
}

ssize_t uuri_escape(const char *path, char *buffer, size_t size)
{
    struct ustring str = ustring_from_str(path);
    size_t s = 0;

    while (!ustring_is_empty(str)) {
        struct ustring tmp = ustring_split_while(&str, uuri_escape_set);
        memcpy(buffer, tmp.at, tmp.len > size ? size : tmp.len);
        s += tmp.len;
        buffer += tmp.len;
        size -= size > tmp.len ? tmp.len : size;

        tmp = ustring_split_until(&str, uuri_escape_set);
        for (size_t i = 0; i < tmp.len; i++) {
            int ret = snprintf(buffer, size, "%%%02X", tmp.at[i]);
            if (ret != 3)
                return -1;
            s += 3;
            buffer += 3;
            size -= size > 3 ? 3 : size;
        }
    }
    if (size)
        *buffer = '\0';

    return s;
}

static char uuri_xdigit_value(char c)
{
    if (isdigit(c))
        return c - '0';
    else if (isxdigit(c))
        return (islower(c) ? c - 'a' : c - 'A') + 10;
    else
        return -1;
}

static char uuri_pct_decode(struct ustring pct)
{
    if (pct.len >= 3 && isxdigit(pct.at[1]) && isxdigit(pct.at[2]))
        return uuri_xdigit_value(pct.at[1]) * 16 +
            uuri_xdigit_value(pct.at[2]);
    return -1;
}

ssize_t uuri_unescape(const char *path, char *buffer, size_t size)
{
    struct ustring str = ustring_from_str(path);
    size_t s = 0;

    while (!ustring_is_empty(str)) {
        struct ustring tmp = ustring_split_until(&str, "%");
        memcpy(buffer, tmp.at, tmp.len > size ? size : tmp.len);
        s += tmp.len;
        buffer += tmp.len;
        size -= size > tmp.len ? tmp.len : size;

        if (ustring_is_empty(str))
            break;

        struct ustring pct = ustring_split_pct_encoded(&str);
        if (ustring_is_empty(pct))
            return -1;
        if (size) {
            buffer[0] = uuri_pct_decode(pct);
            size--;
        }
        buffer++;
        s++;
    }
    if (size)
        *buffer = '\0';

    return s;
}

struct ustring uuri_parse_ipv4(struct ustring *str)
{
    struct ustring ipv4 = *str;
    struct ustring tmp = *str;

    for (unsigned i = 0; i < 4; i++) {
        uint16_t value = 0;
        bool digit = false;

        while (tmp.len && isdigit(*tmp.at) && value <= 255) {
            if (digit && !value)
                return ustring_null();

            value *= 10;
            value += *tmp.at - '0';
            tmp = ustring_shift(tmp, 1);
            digit = true;
        }

        if (!digit || value > 255)
            return ustring_null();

        if (i < 3 && ustring_is_null(ustring_split_match_str(&tmp, ".")))
            return ustring_null();
    }
    ipv4 = ustring_truncate(ipv4, ipv4.len - tmp.len);
    *str = tmp;
    return ipv4;
}

static struct ustring uuri_parse_ipv6_hex(struct ustring *str)
{
    if (!str->len || !isxdigit(str->at[0]))
        return ustring_null();

    struct ustring hex = *str;
    struct ustring tmp = hex;
    for (unsigned i = 0; tmp.len && isxdigit(*tmp.at) && i < 4; i++)
        tmp = ustring_shift(tmp, 1);
    hex = ustring_truncate(hex, hex.len - tmp.len);
    *str = tmp;
    return hex;
}

struct ustring uuri_parse_ipv6(struct ustring *str)
{
    struct ustring ipv6 = *str;
    struct ustring tmp = *str;
    int left = -1, right = 0;

    for (struct ustring hex = tmp;
         !ustring_is_null(uuri_parse_ipv6_hex(&hex)) &&
         !ustring_match_str(hex, ".");
         hex = tmp) {
        right++;
        tmp = hex;
        if (ustring_match_str(tmp, "::") ||
            ustring_is_null(ustring_split_match_str(&tmp, ":")))
            break;
    }

    if (!ustring_is_null(ustring_split_match_str(&tmp, "::"))) {
        left = right;
        right = 0;

        for (struct ustring hex = tmp;
             !ustring_is_null(uuri_parse_ipv6_hex(&hex)) &&
             !ustring_match_str(hex, ".");
             hex = tmp) {
            right++;
            tmp = hex;
            if (ustring_is_null(ustring_split_match_str(&tmp, ":")))
                break;
        }
    }

    struct ustring ipv4 = uuri_parse_ipv4(&tmp);
    if (!ustring_is_null(ipv4))
        right += 2;

    if (left < 0) {
        if (right != 8)
            return ustring_null();
    }
    else if (left + right > 7)
        return ustring_null();

    ipv6 = ustring_truncate(ipv6, tmp.len - ipv6.len);
    *str = tmp;
    return ipv6;
}

struct ustring uuri_parse_ipv6_scoped(struct ustring *str)
{
    struct ustring tmp = *str;
    struct ustring ipv6 = uuri_parse_ipv6(&tmp);
    if (ustring_is_null(ipv6))
        return ustring_null();

    if (ustring_is_null(ustring_split_match_str(&tmp, "%25"))) {
        *str = tmp;
        return ipv6;
    }

    static const char set[] = { UURI_UNRESERVED, 0 };
    while (!ustring_is_empty(ustring_split_while(&tmp, set)) ||
           !ustring_is_empty(ustring_split_pct_encoded(&tmp)));

    struct ustring ipv6_scoped = ustring_truncate(*str, str->len - tmp.len);
    *str = tmp;
    return ipv6_scoped;
}

struct ustring uuri_parse_ipvfuture(struct ustring *str)
{
    struct ustring tmp = *str;
    if (ustring_is_empty(ustring_split_match_str(&tmp, "v")))
        return ustring_null();

    static const char hexdigit_set[] = { USTRING_HEXDIGIT, 0 };
    if (ustring_is_empty(ustring_split_while(&tmp, hexdigit_set)))
        return ustring_null();

    if (ustring_is_empty(ustring_split_match_str(&tmp, ".")))
        return ustring_null();

    static const char set[] = { UURI_UNRESERVED, UURI_SUB_DELIMS, ':', 0 };
    if (ustring_is_empty(ustring_split_while(&tmp, set)))
        return ustring_null();

    struct ustring ipvfuture = ustring_truncate(*str, str->len - tmp.len);
    *str = tmp;
    return ipvfuture;
}

static struct ustring uuri_parse_ip_literal(struct ustring *str)
{
    struct ustring tmp = *str;

    if (ustring_is_empty(ustring_split_match_str(&tmp, "[")))
        return ustring_null();

    if (ustring_is_empty(uuri_parse_ipv6_scoped(&tmp)) &&
        ustring_is_empty(uuri_parse_ipvfuture(&tmp)))
        return ustring_null();

    if (ustring_is_empty(ustring_split_match_str(&tmp, "]")))
        return ustring_null();

    struct ustring ip_literal = ustring_truncate(*str, str->len - tmp.len);
    *str = tmp;
    return ip_literal;
}

static struct ustring uuri_parse_hostname(struct ustring *str)
{
    static const char set[] = { UURI_UNRESERVED, UURI_SUB_DELIMS, 0 };
    struct ustring tmp = *str;
    while (!ustring_is_empty(ustring_split_while(&tmp, set)) ||
           !ustring_is_empty(ustring_split_pct_encoded(&tmp)));
    struct ustring hostname = ustring_truncate(*str, str->len - tmp.len);
    *str = tmp;
    return hostname;
}

struct ustring uuri_parse_scheme(struct ustring *str)
{
    static const char set[] =
        { USTRING_ALPHA, USTRING_DIGIT, '+', '-', '.', 0 };
    if (str->len >= 1 && isalpha(str->at[0]))
        return ustring_split_while(str, set);
    return ustring_null();
}

struct ustring uuri_parse_userinfo(struct ustring *str)
{
    static const char set[] = { UURI_UNRESERVED, UURI_SUB_DELIMS, ':', 0 };
    struct ustring tmp = *str;
    while (!ustring_is_empty(ustring_split_while(&tmp, set)) ||
           !ustring_is_empty(ustring_split_pct_encoded(&tmp)));

    struct ustring userinfo = ustring_truncate(*str, str->len - tmp.len);
    *str = tmp;
    return userinfo;
}

struct ustring uuri_parse_host(struct ustring *str)
{
    struct ustring ip_literal = uuri_parse_ip_literal(str);
    if (!ustring_is_empty(ip_literal))
        return ip_literal;

    struct ustring tmp = *str;
    struct ustring ipv4 = uuri_parse_ipv4(&tmp);
    tmp = *str;
    struct ustring hostname = uuri_parse_hostname(&tmp);
    *str = tmp;
    if (ustring_is_empty(ipv4) || hostname.len > ipv4.len)
        return hostname;
    return ipv4;
}

struct ustring uuri_parse_port(struct ustring *str)
{
    static const char set[] = { USTRING_DIGIT, 0 };
    struct ustring port = ustring_while(*str, set);
    if (!port.len)
        return ustring_null();
    *str = ustring_shift(*str, port.len);
    return port;
}

struct uuri_authority uuri_parse_authority(struct ustring *str)
{
    struct uuri_authority authority = uuri_authority_null();

    struct ustring tmp = *str;
    struct ustring userinfo = uuri_parse_userinfo(&tmp);
    if (ustring_is_null(ustring_split_match_str(&tmp, "@"))) {
        userinfo = ustring_null();
        tmp = *str;
    }

    struct ustring host = uuri_parse_host(&tmp);
    if (ustring_is_null(host))
        return authority;

    if (ustring_is_null(ustring_split_match_str(&tmp, ":"))) {
        *str = tmp;
        authority.userinfo = userinfo;
        authority.host = host;
        return authority;
    }

    struct ustring port = uuri_parse_port(&tmp);
    if (ustring_is_null(port))
        return uuri_authority_null();
    *str = tmp;
    authority.userinfo = userinfo;
    authority.host = host;
    authority.port = port;
    return authority;
}

struct ustring uuri_parse_path(struct ustring *str)
{
    struct ustring tmp = *str;
    while (!ustring_is_empty(ustring_split_while(&tmp, uuri_path_set)) ||
           !ustring_is_empty(ustring_split_pct_encoded(&tmp)));
    struct ustring path = ustring_truncate(*str, str->len - tmp.len);
    *str = tmp;
    return path;
}

struct ustring uuri_parse_query(struct ustring *str)
{
    struct ustring tmp = *str;
    while (!ustring_is_empty(ustring_split_while(&tmp, uuri_query_set)) ||
           !ustring_is_empty(ustring_split_pct_encoded(&tmp)));
    struct ustring query = ustring_truncate(*str, str->len - tmp.len);
    *str = tmp;
    return query;
}

struct ustring uuri_parse_fragment(struct ustring *str)
{
    struct ustring tmp = *str;
    while (!ustring_is_empty(ustring_split_while(&tmp, uuri_fragment_set)) ||
           !ustring_is_empty(ustring_split_pct_encoded(&tmp)));
    struct ustring fragment = ustring_truncate(*str, str->len - tmp.len);
    *str = tmp;
    return fragment;
}

struct uuri uuri_parse(struct ustring *str)
{
    struct ustring tmp = *str;

    struct uuri uuri = uuri_null();
    uuri.scheme = uuri_parse_scheme(&tmp);
    if (ustring_is_null(uuri.scheme) || !uuri.scheme.len)
        return uuri_null();

    if (ustring_is_null(ustring_split_match_str(&tmp, ":")))
        return uuri_null();

    if (!ustring_is_null(ustring_split_match_str(&tmp, "//"))) {
        uuri.authority = uuri_parse_authority(&tmp);
        if (uuri_authority_is_null(uuri.authority))
            return uuri_null();
    }

    if (uuri_authority_is_null(uuri.authority) || ustring_match_str(tmp, "/"))
        uuri.path = uuri_parse_path(&tmp);

    if (!ustring_is_null(ustring_split_match_str(&tmp, "?")))
        uuri.query = uuri_parse_query(&tmp);

    if (!ustring_is_null(ustring_split_match_str(&tmp, "#")))
        uuri.fragment = uuri_parse_fragment(&tmp);

    *str = tmp;
    return uuri;
}

int uuri_authority_len(const struct uuri_authority *authority, size_t *len_p)
{
    size_t len = 0;
    if (authority->userinfo.len)
        len += authority->userinfo.len + 1;
    len += authority->host.len;
    if (authority->port.len)
        len += 1 + authority->port.len;
    *len_p = len;
    return UBASE_ERR_NONE;
}

int uuri_len(const struct uuri *uuri, size_t *len_p)
{
    size_t len = 0;

    if (uuri->scheme.len)
        len += uuri->scheme.len + 1;

    if (!uuri_authority_is_null(uuri->authority)) {
        size_t authority_len;
        int ret = uuri_authority_len(&uuri->authority, &authority_len);
        if (!ubase_check(ret))
            return ret;

        len += 2 + authority_len;
    }

    if (uuri->path.at)
        len += uuri->path.len;
    if (uuri->query.len)
        len += 1 + uuri->query.len;
    if (uuri->fragment.len)
        len += 1 + uuri->fragment.len;
    *len_p = len;
    return UBASE_ERR_NONE;
}

int uuri_authority_to_buffer(const struct uuri_authority *authority,
                             char *buffer, size_t len)
{
    int ret;

    if (uuri_authority_is_null(*authority))
        return UBASE_ERR_INVALID;

    if (authority->userinfo.len) {
        ret = snprintf(buffer, len, "%.*s@",
                       (int)authority->userinfo.len,
                       authority->userinfo.at);
        if (ret < 0 || ret >= len)
            return UBASE_ERR_INVALID;
        buffer += ret;
        len -= ret;
    }

    ret = snprintf(buffer, len, "%.*s",
                   (int)authority->host.len,
                   authority->host.at);
    if (ret < 0 || ret >= len)
        return UBASE_ERR_INVALID;
    buffer += ret;
    len -= ret;

    if (authority->port.len) {
        ret = snprintf(buffer, len, ":%.*s",
                       (int)authority->port.len,
                       authority->port.at);
        if (ret < 0 || ret >= len)
            return UBASE_ERR_INVALID;
    }

    return UBASE_ERR_NONE;
}

int uuri_to_buffer(struct uuri *uuri, char *buffer, size_t len)
{
    int ret;

    if (uuri_is_null(*uuri))
        return UBASE_ERR_INVALID;

    ret = snprintf(buffer, len, "%.*s:", (int)uuri->scheme.len,
                   uuri->scheme.at);
    if (ret < 0 || ret >= len)
        return UBASE_ERR_INVALID;
    buffer += ret;
    len -= ret;

    if (!uuri_authority_is_null(uuri->authority)) {
        ret = snprintf(buffer, len, "//");
        if (ret < 0 || ret >= len)
            return UBASE_ERR_INVALID;
        buffer += ret;
        len -= ret;

        size_t authority_len;
        ret = uuri_authority_len(&uuri->authority, &authority_len);
        if (!ubase_check(ret))
            return ret;

        if (authority_len >= len)
            return UBASE_ERR_NOSPC;

        ret = uuri_authority_to_buffer(&uuri->authority, buffer,
                                       authority_len + 1);
        if (!ubase_check(ret))
            return ret;

        buffer += authority_len;
        len -= authority_len;
    }

    ret = snprintf(buffer, len, "%.*s",
                   (int)uuri->path.len,
                   uuri->path.at);
    if (ret < 0 || ret >= len)
        return UBASE_ERR_INVALID;
    buffer += ret;
    len -= ret;

    if (uuri->query.len) {
        ret = snprintf(buffer, len, "?%.*s", (int)uuri->query.len,
                       uuri->query.at);
        if (ret < 0 || ret >= len)
            return UBASE_ERR_INVALID;
        buffer += ret;
        len -= ret;
    }
    if (uuri->fragment.len) {
        ret = snprintf(buffer, len, "#%.*s", (int)uuri->fragment.len,
                       uuri->fragment.at);
        if (ret < 0 || ret >= len)
            return UBASE_ERR_INVALID;
    }

    return UBASE_ERR_NONE;
}

int uuri_to_str(struct uuri *uuri, char **str_p)
{
    size_t size = 0;
    int ret = uuri_len(uuri, &size);
    if (ret)
        return ret;

    char *str = malloc((size + 1) * sizeof (char));
    if (!str)
        return UBASE_ERR_ALLOC;

    ret = uuri_to_buffer(uuri, str, (size + 1) * sizeof (char));
    if (ret) {
        free(str);
        return ret;
    }

    *str_p = str;
    return UBASE_ERR_NONE;
}
