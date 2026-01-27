/*
 * Copyright (c) 2015 Arnaud de Turckheim <quarium@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _UPIPE_HLS_UREF_HLS_H_
# define _UPIPE_HLS_UREF_HLS_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "upipe/uref_attr.h"

UREF_ATTR_SMALL_UNSIGNED(hls, renditions, "hls.renditions", number of rendition);
UREF_ATTR_STRING_VA(hls_rendition, type, "hls.rendition[%" PRIu8"].type",
                    rendition type, uint8_t id, id);
UREF_ATTR_STRING_VA(hls_rendition, name, "hls.rendition[%" PRIu8"].name",
                    rendition name, uint8_t id, id);
UREF_ATTR_STRING_VA(hls_rendition, uri, "hls.rendition[%" PRIu8"].uri",
                    rendition uri, uint8_t id, id);
UREF_ATTR_VOID_VA(hls_rendition, default, "hls.rendition[%" PRIu8"].default",
                  rendition default, uint8_t id, id);
UREF_ATTR_VOID_VA(hls_rendition, autoselect,
                  "hls.rendition[%" PRIu8"].autoselect",
                  rendition autoselect, uint8_t id, id);

UREF_ATTR_STRING(hls, type, "hls.type", type);
UREF_ATTR_STRING(hls, name, "hls.name", name);
UREF_ATTR_STRING(hls, uri, "hls.uri", uri);
UREF_ATTR_VOID(hls, default, "hls.default", default rendition);
UREF_ATTR_VOID(hls, autoselect, "hls.autoselect", auto select rendition);

#ifdef __cplusplus
}
#endif
#endif /* !_UPIPE_HLS_UREF_HLS_H_ */
