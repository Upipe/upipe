/*
 * 10 bit unpacking
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

void upipe_sdi_to_uyvy_c    (const uint8_t *src, uint16_t *y, uintptr_t pixels);
void upipe_sdi_to_uyvy_ssse3(const uint8_t *src, uint16_t *y, uintptr_t pixels);
void upipe_sdi_to_uyvy_avx2 (const uint8_t *src, uint16_t *y, uintptr_t pixels);
