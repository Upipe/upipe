#ifndef _UPIPE_UREF_HTTP_H_
# define _UPIPE_UREF_HTTP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <upipe/uref_attr.h>

UREF_ATTR_SMALL_UNSIGNED(http, cookies, "http.cookies",
                         number of http cookies)

UREF_ATTR_STRING_VA(http, cookie, "http.cookie[%"PRIu8"]",
                    http cookie, uint8_t nb, nb);

UREF_ATTR_STRING_VA(http, cookie_value, "http.cookie.%s",
                    http cookie value, const char *name, name);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_UREF_HTTP_H_ */
