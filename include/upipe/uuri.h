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

/** @file
 * @short Upipe functions to parse or generate URIs according to RFC3986
 */

#ifndef _UPIPE_UURI_H_
/** @hidden */
# define _UPIPE_UURI_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/ustring.h>

#include <stddef.h>
#include <string.h>

/** @This parses an IPv4 and shifts ustring str.
 * (ex: 192.168.0.1)
 *
 * @param str the ustring to parse
 * @return the parsed IPv4 or a null ustring
 */
struct ustring uuri_parse_ipv4(struct ustring *str);

/** @This parses an IPv6 and shifts ustring str.
 * (ex: FE80:0000:0000:0000:0202:B3FF:FE1E:8329)
 *
 * @param str the ustring to parse
 * @return the parsed IPv6 or a null ustring
 */
struct ustring uuri_parse_ipv6(struct ustring *str);

/** @This parses a scoped IPv6 and shifts ustring str.
 * (ex: FE80:0000:0000:0000:0202:B3FF:FE1E:8329%25eth0)
 *
 * @param str the ustring to parse
 * @return the parsed IPv6 or a null ustring
 */
struct ustring uuri_parse_ipv6_scoped(struct ustring *str);

/** @This parses a future ip and shifts ustring str.
 *
 * @param str the ustring to parse
 * @return the parsed IPv6 or a null ustring
 */
struct ustring uuri_parse_ipvfuture(struct ustring *str);

/** @This parses and shifts an authority user info.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an ustring with the user info portion of str
 */
struct ustring uuri_parse_userinfo(struct ustring *str);

/** @This parses and shifts an authority host.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an ustring with the host portion of str
 */
struct ustring uuri_parse_host(struct ustring *str);

/** @This parses and shifts an authority port.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an ustring with the port portion of str
 */
struct ustring uuri_parse_port(struct ustring *str);

/** @This parses and shifts a scheme.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an ustring with the scheme portion of str
 */
struct ustring uuri_parse_scheme(struct ustring *str);

/** @This parses and shifts a path.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an ustring with the path portion of str
 */
struct ustring uuri_parse_path(struct ustring *str);

/** @This parses and shifts a query.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an ustring with the query portion of str
 */
struct ustring uuri_parse_query(struct ustring *str);

/** @This parses and shifts a fragment.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an ustring with the fragment portion of str
 */
struct ustring uuri_parse_fragment(struct ustring *str);

/** @This escapes a string into a buffer.
 *
 * @param str the string to escape
 * @param buffer the destination buffer
 * @param size the size of the destination buffer
 * @return the size needed to escape or a negative value on error
 */
ssize_t uuri_escape(const char *str, char *buffer, size_t size);

/** @This returns the size needed to escape.
 *
 * @param str the string to escape
 * @return the size needed to escape or a negative value on error
 */
static inline ssize_t uuri_escape_len(const char *str)
{
    return uuri_escape(str, NULL, 0);
}

/** @This unescapes a string into buffer.
 *
 * @param str the string to unescape
 * @param buffer the destination buffer
 * @param size the size of the destination buffer
 * @return the size needed to unescape or a negative value on error
 */
ssize_t uuri_unescape(const char *str, char *buffer, size_t size);

/** @This returns the size needed to unescape string.
 *
 * @param str the string to unescape
 * @return the size needed to unescape or a negative value on error
 */
static inline ssize_t uuri_unescape_len(const char *str)
{
    return uuri_unescape(str, NULL, 0);
}

/** @This stores the authority part of an URI.
 *
 * [ userinfo '@@' ] host [ ':' port ]
 */
struct uuri_authority {
    /** userinfo part */
    struct ustring userinfo;
    /** host part */
    struct ustring host;
    /** port part */
    struct ustring port;
};

/** @This returns an initialized uuri_authority.
 *
 * @return an initialized uuri_authority structure
 */
static inline struct uuri_authority uuri_authority_null(void)
{
    struct uuri_authority authority;
    authority.userinfo = ustring_null();
    authority.host = ustring_null();
    authority.port = ustring_null();
    return authority;
}

/** @This checks if an authority is null.
 *
 * @param authority an uuri_authority structure
 * @return true if authority is null
 */
static inline bool uuri_authority_is_null(struct uuri_authority authority)
{
    return ustring_is_null(authority.host);
}


/** @This gets the length required to print authority.
 *
 * @param authority pointer to an uuri_authority structure
 * @param len_p pointer to the length
 * @return an error code
 */
int uuri_authority_len(const struct uuri_authority *authority, size_t *len_p);

/** @This prints the authority into a buffer.
 *
 * @param authority pointer to an uuri_authority structure
 * @param buffer pointer to buffer
 * @param len size of the buffer
 * @return an error code
 */
int uuri_authority_to_buffer(const struct uuri_authority *authority,
                             char *buffer, size_t len);

/** @This parses and shifts an authority.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an authority structure with the authority portion of str
 */
struct uuri_authority uuri_parse_authority(struct ustring *str);

/** @This stores the different parts of an URI.
 *
 * scheme ':' [ '//' authority ] path [ '?' query ] [ '#' fragment ]
 */
struct uuri {
    /** scheme part */
    struct ustring scheme;
    /** authority */
    struct uuri_authority authority;
    /** path part */
    struct ustring path;
    /** query part */
    struct ustring query;
    /** fragment part */
    struct ustring fragment;
};

/** @This returns an initialized uuri.
 *
 * @return an initialized uuri structure
 */
static inline struct uuri uuri_null(void)
{
    struct uuri uuri;
    uuri.scheme = ustring_null();
    uuri.authority = uuri_authority_null();
    uuri.path = ustring_null();
    uuri.query = ustring_null();
    uuri.fragment = ustring_null();
    return uuri;
}

/** @This checks if an uri is null.
 *
 * @param uuri an uuri structure
 * @return true if uri is null
 */
static inline bool uuri_is_null(struct uuri uuri)
{
    return ustring_is_null(uuri.scheme);
}

/** @This gets the length required to print uri.
 *
 * @param uuri pointer to an uuri structure
 * @param len_p pointer to the length
 * @return an error code
 */
int uuri_len(const struct uuri *uuri, size_t *len_p);

/** @This prints the uri into a buffer.
 *
 * @param uuri pointer to an uuri structure
 * @param buffer pointer to buffer
 * @param len size of the buffer
 * @return an error code
 */
int uuri_to_buffer(struct uuri *uuri, char *buffer, size_t len);

/** @This allocates a string from an uri.
 *
 * @param uuri pointer to an uuri structure
 * @param str_p a pointer to the allocated string
 * @return an error code
 */
int uuri_to_str(struct uuri *uuri, char **str_p);

/** @This parses and shifts an uri.
 *
 * @param str pointer to an ustring to parse and shift
 * @return an uuri structure with the uri portion of str
 */
struct uuri uuri_parse(struct ustring *str);

/** @This makes an uuri structure from a string.
 *
 * @param uuri pointer to an uuri structure
 * @param str the string to parse
 * @return an error code
 */
static inline int uuri_from_str(struct uuri *uuri, const char *str)
{
    struct ustring ustring = ustring_from_str(str);
    *uuri = uuri_parse(&ustring);
    return ustring.len ? UBASE_ERR_INVALID : UBASE_ERR_NONE;
}

#ifdef __cplusplus
}
#endif
#endif
