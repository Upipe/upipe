/*
 * 10 bit packing
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

void upipe_uyvy_to_sdi_c    (uint8_t *dst, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_ssse3(uint8_t *dst, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_avx  (uint8_t *dst, const uint8_t *y, uintptr_t pixels);
void upipe_uyvy_to_sdi_avx2 (uint8_t *dst, const uint8_t *y, uintptr_t pixels);
