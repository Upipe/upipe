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

#define UURI_GEN_DELIMS ':', '/', '?', '#', '[', ']', '@'
#define UURI_SUB_DELIMS '!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '='
#define UURI_RESERVED UURI_GEN_DELIMS, UURI_SUB_DELIMS
#define UURI_ALPHA_LOWER 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', \
                         'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', \
                         'u', 'v', 'w', 'x', 'y', 'z'
#define UURI_ALPHA_UPPER 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', \
                         'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', \
                         'U', 'V', 'W', 'X', 'Y', 'Z'
#define UURI_ALPHA UURI_ALPHA_LOWER, UURI_ALPHA_UPPER
#define UURI_DIGIT '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
#define UURI_HEXDIGIT UURI_DIGIT, 'a', 'b', 'c', 'd', 'e', 'f', \
                                  'A', 'B', 'C', 'D', 'E', 'F'
#define UURI_UNRESERVED UURI_ALPHA, UURI_DIGIT, '-', '.', '_', '~'

struct ustring uuri_parse_scheme(struct ustring *str)
{
    static const char set[] = { UURI_ALPHA, UURI_DIGIT, '+', '-', '.', 0 };
    if (str->len >= 1 || isalpha(str->at[0]))
        return ustring_split_while(str, set);
    return ustring_null();
}

struct ustring uuri_parse_userinfo(struct ustring *str)
{
    static const char set[] = { UURI_UNRESERVED, UURI_SUB_DELIMS, ':', '%', 0 };
    return ustring_split_while(str, set);
}

struct ustring uuri_parse_host(struct ustring *str)
{
    static const char reg_set[] = { UURI_UNRESERVED, UURI_SUB_DELIMS, '%', 0 };
    static const char ipv6_set[] = { UURI_HEXDIGIT, ':', 0 };
    const char *host_set = reg_set;

    struct ustring tmp = *str;
    if (!ustring_is_null(ustring_split_match_str(&tmp, "[")))
        host_set = ipv6_set;

    struct ustring host = ustring_split_while(&tmp, host_set);

    if (host_set == ipv6_set) {
        if (ustring_is_null(ustring_split_match_str(&tmp, "]")))
            return ustring_null();
        host.at--;
        host.len += 2;
    }
    *str = ustring_shift(*str, host.len);
    return host;
}

struct ustring uuri_parse_port(struct ustring *str)
{
    static const char set[] = { UURI_DIGIT, 0 };
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

    if (!tmp.len || *tmp.at == '/') {
        *str = tmp;
        authority.userinfo = userinfo;
        authority.host = host;
        return authority;
    }

    if (ustring_is_null(ustring_split_match_str(&tmp, ":")))
        return uuri_authority_null();

    struct ustring port = uuri_parse_port(&tmp);
    if (ustring_is_null(port))
        return authority;
    *str = tmp;
    authority.userinfo = userinfo;
    authority.host = host;
    authority.port = port;
    return authority;
}

struct ustring uuri_parse_path(struct ustring *str)
{
    static const char set[] = { '?', '#', '\0' };
    return ustring_split_until(str, set);
}

struct ustring uuri_parse_query(struct ustring *str)
{
    static const char set[] = { '#', '\0' };
    return ustring_split_until(str, set);
}

struct ustring uuri_parse_fragment(struct ustring *str)
{
    return ustring_split_until(str, "");
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
        buffer += ret;
        len -= ret;
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
        buffer += ret;
        len -= ret;
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
    if (ret)
        return ret;

    *str_p = str;
    return UBASE_ERR_NONE;
}
