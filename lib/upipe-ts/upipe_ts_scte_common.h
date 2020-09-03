/*
 * Copyright (C) 2020 EasyTools
 *
 * Authors: Arnaud de Turckheim
 *
 * This event is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This event is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this event; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

/** @file
 * @short Upipe module decoding the operation tables of SCTE 104 streams
 * Normative references:
 *  - SCTE 104 2012 (Automation to Compression Communications API)
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

/** @file
 * @short Common function for SCTE decoders
 * Normative references:
 *  - SCTE 104 2012 (Automation to Compression Communications API)
 *  - SCTE 35 2013 (Digital Program Insertion Cueing Message for Cable)
 */

#ifndef _UPIPE_TS_SCTE_COMMON_H_
#define _UPIPE_TS_SCTE_COMMON_H_

/** @hidden */
struct upipe;

/** @hidden */
struct uref;

/** @This allocates an uref describing a SCTE35 descriptor.
 *
 * @param upipe description structure of the caller
 * @param uref input buffer
 * @param desc pointer to the SCTE35 descriptor
 * @return an allocated uref of NULL
 */
struct uref *upipe_ts_scte_extract_desc(struct upipe *upipe,
                                        struct uref *uref,
                                        const uint8_t *desc);

/** @This export an uref describing a SCTE35 descriptor.
 *
 * @param upipe description structure of the caller
 * @param uref uref to export
 * @param desc pointer to the SCTE35 descriptor destination
 * @return an error code
 */
int upipe_ts_scte_export_desc(struct upipe *upipe,
                              struct uref *uref,
                              uint8_t *desc);

#endif
