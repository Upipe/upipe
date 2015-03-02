/*
 * Copyright (C) 2012-2014 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe utility functions to work on PSI sections and tables
 * This is actually the equivalent of <bitstream/mpeg/psi.h>, using uref's and
 * zerocopy instead of octets arrays.
 */

#include <upipe/ubase.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>

#include <stdbool.h>
#include <assert.h>

#include <bitstream/mpeg/psi.h>

/** @This checks the CRC of a PSI section.
 *
 * @param section PSI section
 * @return false if the CRC is invalid
 */
static inline bool upipe_ts_psid_check_crc(struct uref *section)
{
    uint8_t buffer[PSI_HEADER_SIZE];
    const uint8_t *section_header = uref_block_peek(section, 0, PSI_HEADER_SIZE,
                                                    buffer);
    assert(section_header != NULL);
    uint16_t size = psi_get_length(section_header) +
                    PSI_HEADER_SIZE - PSI_CRC_SIZE;
    int err = uref_block_peek_unmap(section, 0, buffer, section_header);
    ubase_assert(err);

    uint32_t crc = 0xffffffff;
    int offset = 0;
    while (size > 0) {
        int read_size = size;
        const uint8_t *read_buffer;
        if (unlikely(!ubase_check(uref_block_read(section, offset, &read_size,
                                                  &read_buffer))))
            return false;
        for (int i = 0; i < read_size; i++)
            crc = (crc << 8) ^ p_psi_crc_table[(crc >> 24) ^ (read_buffer[i])];
        if (unlikely(!ubase_check(uref_block_unmap(section, offset))))
            return false;
        size -= read_size;
        offset += read_size;
    }

    uint8_t buffer2[4];
    const uint8_t *section_crc = uref_block_peek(section, offset, 4, buffer2);
    assert(section_crc != NULL);
    bool checked = section_crc[0] == (crc >> 24) &&
                   section_crc[1] == ((crc >> 16) & 0xff) &&
                   section_crc[2] == ((crc >> 8) & 0xff) &&
                   section_crc[3] == (crc & 0xff);
    err = uref_block_peek_unmap(section, offset, buffer2, section_crc);
    ubase_assert(err);
    return checked;
}

/** @This validates a PSI section.
 *
 * @param section PSI section
 * @return false if the section is invalid
 */
static inline bool upipe_ts_psid_validate(struct uref *section)
{
    uint8_t buffer[PSI_HEADER_SIZE];
    const uint8_t *section_header = uref_block_peek(section, 0, PSI_HEADER_SIZE,
                                                    buffer);
    assert(section_header != NULL);
    bool checked = !psi_get_syntax(section_header) ||
                   psi_get_length(section_header) >= PSI_HEADER_SIZE_SYNTAX1 -
                                                PSI_HEADER_SIZE + PSI_CRC_SIZE;
    int err = uref_block_peek_unmap(section, 0, buffer, section_header);
    ubase_assert(err);

    return checked;
}

/** @This compares two PSI sections
 *
 * @param section1 PSI section 1
 * @param section2 PSI section 2
 * @return false if the tables are different
 */
static inline bool upipe_ts_psid_equal(struct uref *section1,
                                       struct uref *section2)
{
    return ubase_check(uref_block_equal(section1, section2));
}

/** @This declares a PSI table in a structure.
 *
 * @param table name of the member
 */
#define UPIPE_TS_PSID_TABLE_DECLARE(table) \
    struct uref *table[PSI_TABLE_MAX_SECTIONS]

/** @This initializes a PSI table.
 *
 * @param sections PSI table
 */
static inline void upipe_ts_psid_table_init(struct uref **sections)
{
    for (int i = 0; i < PSI_TABLE_MAX_SECTIONS; i++)
        sections[i] = NULL;
}

/** @This cleans up a PSI table.
 *
 * @param sections PSI table
 */
static inline void upipe_ts_psid_table_clean(struct uref **sections)
{
    for (int i = 0; i < PSI_TABLE_MAX_SECTIONS; i++)
        if (sections[i] != NULL)
            uref_free(sections[i]);
}

/** @This validates a PSI table.
 *
 * @param sections PSI table
 * @return false if the table is invalid
 */
static inline bool upipe_ts_psid_table_validate(struct uref **sections)
{
    return sections[0] != NULL;
}

/** @This (temporarily) copies a PSI table. Reference counts are not
 * incremented.
 *
 * @param dest destination PSI table
 * @param src source PSI table
 */
static inline void upipe_ts_psid_table_copy(struct uref **dest,
                                            struct uref **src)
{
    memcpy(dest, src, PSI_TABLE_MAX_SECTIONS * sizeof(struct uref *));
}

/** @This returns the last section number from the given table. This function
 * may only be called if @ref upipe_ts_psid_table_validate is true.
 *
 * @param sections PSI table
 */
static inline uint8_t upipe_ts_psid_table_get_lastsection(struct uref **sections)
{
    uint8_t buffer[PSI_HEADER_SIZE_SYNTAX1];
    const uint8_t *section_header = uref_block_peek(sections[0], 0,
                                                    PSI_HEADER_SIZE_SYNTAX1,
                                                    buffer);
    assert(section_header != NULL);
    uint8_t last_section = psi_get_lastsection(section_header);
    int err = uref_block_peek_unmap(sections[0], 0, buffer,
                                               section_header);
    ubase_assert(err);
    return last_section;
}

/** @This inserts a new section that composes a table.
 *
 * @param sections PSI table
 * @param uref new section
 * @return true if the table is complete
 */
static inline bool upipe_ts_psid_table_section(struct uref **sections,
                                               struct uref *uref)
{
    uint8_t buffer[PSI_HEADER_SIZE_SYNTAX1];
    const uint8_t *section_header = uref_block_peek(uref, 0,
                                                    PSI_HEADER_SIZE_SYNTAX1,
                                                    buffer);
    if (unlikely(section_header == NULL)) {
        uref_free(uref);
        return false;
    }
    uint8_t section = psi_get_section(section_header);
    uint8_t last_section = psi_get_lastsection(section_header);
    uint8_t version = psi_get_version(section_header);
    uint16_t tableidext = psi_get_tableidext(section_header);
    int err = uref_block_peek_unmap(uref, 0, buffer, section_header);
    ubase_assert(err);

    if (unlikely(sections[section] != NULL))
        uref_free(sections[section]);
    sections[section] = uref;

    int i;
    for (i = 0; i <= last_section; i++) {
        if (sections[i] == NULL)
            return false;

        section_header = uref_block_peek(sections[i], 0,
                                         PSI_HEADER_SIZE_SYNTAX1, buffer);
        uint8_t sec_last_section = psi_get_lastsection(section_header);
        uint8_t sec_version = psi_get_version(section_header);
        uint16_t sec_tableidext = psi_get_tableidext(section_header);
        err = uref_block_peek_unmap(sections[i], 0, buffer, section_header);
        ubase_assert(err);

        if (sec_last_section != last_section || sec_version != version ||
            sec_tableidext != tableidext)
            return false;
    }

    /* free spurious, invalid sections */
    for ( ; i < PSI_TABLE_MAX_SECTIONS; i++) {
        if (sections[i] != NULL)
            uref_free(sections[i]);
        sections[i] = NULL;
    }

    /* a new, full table is available */
    return true;
}

/** @This returns a section from a PSI table.
 *
 * @param sections PSI table
 * @param n section number
 * @return uref containing the wanted section
 */
static inline struct uref *upipe_ts_psid_table_get_section(struct uref **sections, uint8_t n)
{
    return sections[n];
}

/** @This walks through the sections of a PSI table.
 *
 * @param sections PSI table
 * @param section iterator
 */
#define upipe_ts_psid_table_foreach(sections, section)                      \
    uint8_t upipe_ts_psid_table_foreach_last_section =                      \
        upipe_ts_psid_table_get_lastsection(sections);                      \
    int upipe_ts_psid_table_foreach_i = 0;                                  \
    for (struct uref *section = sections[0];                                \
         upipe_ts_psid_table_foreach_i <=                                   \
            upipe_ts_psid_table_foreach_last_section;                       \
         upipe_ts_psid_table_foreach_i++,                                   \
            section = sections[upipe_ts_psid_table_foreach_i])

/** @This compares two PSI tables.
 *
 * @param sections1 PSI table 1
 * @param sections2 PSI table 2
 * @return false if the tables are different
 */
static inline bool upipe_ts_psid_table_compare(struct uref **sections1,
                                               struct uref **sections2)
{
    uint8_t last_section = upipe_ts_psid_table_get_lastsection(sections1);
    if (last_section != upipe_ts_psid_table_get_lastsection(sections2))
        return false;

    for (int i = 0; i <= last_section; i++) {
        struct uref *section1 = upipe_ts_psid_table_get_section(sections1, i);
        struct uref *section2 = upipe_ts_psid_table_get_section(sections2, i);
        if (!upipe_ts_psid_equal(section1, section2))
            return false;
    }

    return true;
}
