/*
 * Copyright (C) 2015 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * SPDX-License-Identifier: MIT
 */

/** @file
 * @short unit tests for bit-oriented writer and reader
 */

#undef NDEBUG

#include "upipe/ubits.h"

#include <stdio.h>
#include <assert.h>

int main(int argc, char **argv)
{
    struct ubits bw;
    size_t buffer_size = 5;
    uint8_t buffer[8];
    uint8_t *buffer_end = NULL;

    ubits_init(&bw, buffer, buffer_size, UBITS_WRITE);
    ubits_put(&bw, 8, 1);
    ubits_put(&bw, 8, 2);
    ubits_put(&bw, 8, 3);
    ubits_put(&bw, 8, 4);
    ubits_put(&bw, 4, 0);
    ubits_put(&bw, 1, 0);
    ubits_put(&bw, 1, 1);
    ubits_put(&bw, 1, 0);
    ubits_put(&bw, 1, 1);
    ubase_assert(ubits_clean(&bw, &buffer_end));
    assert(buffer_end == buffer + buffer_size);
    for (int i = 0; i < 5; i++)
        assert(buffer[i] == i + 1);

    ubits_init(&bw, buffer, buffer_size, UBITS_READ);
    assert(ubits_get(&bw, 8) == 1);
    assert(ubits_get(&bw, 8) == 2);
    assert(ubits_get(&bw, 8) == 3);
    assert(ubits_get(&bw, 8) == 4);
    assert(ubits_get(&bw, 4) == 0);
    assert(ubits_get(&bw, 1) == 0);
    assert(ubits_get(&bw, 1) == 1);
    assert(ubits_get(&bw, 1) == 0);
    assert(ubits_get(&bw, 1) == 1);
    assert(!bw.overflow);

    buffer_size = 1;
    ubits_init(&bw, buffer, buffer_size, UBITS_WRITE);
    ubits_put(&bw, 4, 0);
    ubits_put(&bw, 1, 0);
    ubits_put(&bw, 1, 1);
    ubits_put(&bw, 1, 0);
    ubits_put(&bw, 1, 1);
    ubase_assert(ubits_clean(&bw, &buffer_end));
    assert(buffer_end == buffer + buffer_size);
    assert(buffer[0] == 5);

    ubits_init(&bw, buffer, buffer_size, UBITS_READ);
    assert(ubits_get(&bw, 4) == 0);
    assert(ubits_get(&bw, 1) == 0);
    assert(ubits_get(&bw, 1) == 1);
    assert(ubits_get(&bw, 1) == 0);
    assert(ubits_get(&bw, 1) == 1);
    assert(!bw.overflow);

    buffer_size = 1;
    ubits_init(&bw, buffer, buffer_size, UBITS_WRITE);
    ubits_put(&bw, 4, 0);
    ubits_put(&bw, 1, 0);
    ubits_put(&bw, 1, 1);
    ubits_put(&bw, 1, 0);
    ubits_put(&bw, 1, 1);
    ubits_put(&bw, 1, 0);
    ubits_put(&bw, 1, 0);
    ubase_nassert(ubits_clean(&bw, &buffer_end));

    ubits_init(&bw, buffer, buffer_size, UBITS_READ);
    assert(ubits_get(&bw, 4) == 0);
    assert(ubits_get(&bw, 1) == 0);
    assert(ubits_get(&bw, 1) == 1);
    assert(ubits_get(&bw, 1) == 0);
    assert(ubits_get(&bw, 1) == 1);
    assert(!bw.overflow);
    assert(ubits_get(&bw, 1) == 0);
    assert(bw.overflow);

    buffer[0] = 0x02;
    buffer[1] = 0x8f;
    buffer[2] = 0x80;
    buffer[3] = 0x0e;
    buffer[4] = 0x55;
    buffer[5] = 0x81;
    buffer[6] = 0x53;
    buffer[7] = 0x78;

    buffer_size = 8;
    ubits_init(&bw, buffer, buffer_size, UBITS_READ);
    assert(ubits_get(&bw, 6)  ==  0);
    assert(ubits_get(&bw, 1)  ==  1);
    assert(ubits_get(&bw, 11) ==  574);
    assert(ubits_get(&bw, 12) ==  3);
    assert(ubits_get(&bw, 10) ==  597);
    assert(ubits_get(&bw, 10) ==  517);
    assert(ubits_get(&bw, 10) ==  311);
    return 0;
}
