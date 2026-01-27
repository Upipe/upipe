/*
 * 10 bit packing
 *
 * Copyright (c) 2016 Open Broadcast Systems Ltd
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "upipe/ubits.h"

#include <arpa/inet.h>

#include "sdienc.h"

void upipe_uyvy_to_sdi_c(uint8_t *dst, const uint8_t *y, uintptr_t pixels)
{
    struct ubits s;
    uintptr_t size = pixels * 2; /* change to number of samples */
    ubits_init(&s, dst, size * 10 / 8, UBITS_WRITE);

    for (int i = 0; i < size; i ++)
        ubits_put(&s, 10, htons((y[2*i+0] << 8) | y[2*i+1]));
    uint8_t *temp;
    ubits_clean(&s, &temp);
}
