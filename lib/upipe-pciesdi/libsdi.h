/*
 * PCIeSDI library
 *
 * Copyright (C) 2018 / EnjoyDigital  / florent@enjoy-digital.fr
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#ifndef SDI_LIB_H
#define SDI_LIB_H

#include "genlock.h"

int64_t get_time_ms(void);

/* ioctl */

uint32_t sdi_readl(int fd, uint32_t addr);
void sdi_writel(int fd, uint32_t addr, uint32_t val);
void sdi_reload(int fd);

void sdi_refclk(int fd, uint8_t refclk_sel, uint32_t *refclk_freq, uint32_t *refclk_counter);

#ifdef HAS_VCXOS
void sdi_vcxo(int fd, uint32_t width, uint32_t period);
#endif

#ifdef HAS_SI5324
void sdi_si5324_vcxo(int fd, uint32_t width, uint32_t period);
void sdi_si5324_spi(int fd, uint32_t tx_data, uint32_t *rx_data);
#endif

#ifdef HAS_GENLOCK
void sdi_genlock_hsync(int fd, uint8_t *active, uint64_t *period, uint64_t *seen);
void sdi_genlock_vsync(int fd, uint8_t *active, uint64_t *period, uint64_t *seen);
#endif

void sdi_dma(int fd, uint8_t fill, uint8_t rx_tx_loopback_enable, uint8_t tx_rx_loopback_enable);
void sdi_dma_reader(int fd, uint8_t enable, int64_t *hw_count, int64_t *sw_count);
void sdi_dma_writer(int fd, uint8_t enable, int64_t *hw_count, int64_t *sw_count);
void sdi_set_pattern(int fd, uint8_t mode, uint8_t enable, uint8_t format);

#ifdef HAS_GS12241
void sdi_gs12241_spi_cs(int fd, uint8_t cs_n);
void sdi_gs12241_spi(int fd, uint32_t tx_data, uint32_t *rx_data);
#endif

#ifdef HAS_GS12281
void sdi_gs12281_spi_cs(int fd, uint8_t cs_n);
void sdi_gs12281_spi(int fd, uint32_t tx_data, uint32_t *rx_data);
#endif

#ifdef HAS_LMH0387
void sdi_set_direction(int fd, uint8_t tx_enable);
void sdi_spi_cs(int fd, uint8_t cs_n);
void sdi_spi(int fd, uint32_t tx_data, uint32_t *rx_data);
#endif

void sdi_rx(int fd, uint8_t *locked, uint8_t *mode, uint8_t *family, uint8_t *scan, uint8_t *rate);
void sdi_tx(int fd, uint8_t mode, uint8_t *txen, uint8_t *slew);
void sdi_tx_rx_loopback(int fd, uint8_t config);

uint8_t sdi_request_dma_reader(int fd);
uint8_t sdi_request_dma_writer(int fd);
void sdi_release_dma_reader(int fd);
void sdi_release_dma_writer(int fd);

/* si5324 */

#ifdef HAS_SI5324

#define countof(x) (sizeof(x) / sizeof(x[0]))

static const uint16_t si5324_148_5_mhz_regs[][2] = {
    {   0, 0x54 },
    {   1, 0xe4 },
    {   2, 0x42 },
    {   3, 0x15 },
    {   4, 0x92 },
    {   5, 0xed },
    {   6, 0x2d },
    {   7, 0x2a },
    {   8, 0x00 },
    {   9, 0xc0 },
    {  10, 0x00 },
    {  11, 0x40 },
    {  19, 0x29 },
    {  20, 0x3e },
    {  21, 0xff },
    {  22, 0xdf },
    {  23, 0x1f },
    {  24, 0x3f },
    {  25, 0xa0 },
    {  31, 0x00 },
    {  32, 0x00 },
    {  33, 0x03 },
    {  34, 0x00 },
    {  35, 0x00 },
    {  36, 0x03 },
    {  40, 0xe0 },
    {  41, 0x4f },
    {  42, 0x7d },
    {  43, 0x00 },
    {  44, 0x06 },
    {  45, 0x5b },
    {  46, 0x00 },
    {  47, 0x06 },
    {  48, 0x5b },
    {  55, 0x00 },
    { 131, 0x1f },
    { 132, 0x02 },
    { 137, 0x01 },
    { 138, 0x0f },
    { 139, 0xff },
    { 142, 0x00 },
    { 143, 0x00 },
    { 136, 0x40 },
};

static const uint16_t si5324_148_35_mhz_regs[][2] = {
    {  0, 0x54 },
    {  1, 0xe4 },
    {  2, 0x42 },
    {  3, 0x15 },
    {  4, 0x92 },
    {  5, 0xed },
    {  6, 0x2d },
    {  7, 0x2a },
    {  8, 0x00 },
    {  9, 0xc0 },
    { 10, 0x00 },
    { 11, 0x40 },
    { 19, 0x29 },
    { 20, 0x3e },
    { 21, 0xff },
    { 22, 0xdf },
    { 23, 0x1f },
    { 24, 0x3f },
    { 25, 0x40 },
    { 31, 0x00 },
    { 32, 0x00 },
    { 33, 0x05 },
    { 34, 0x00 },
    { 35, 0x00 },
    { 36, 0x05 },
    { 40, 0xe0 },
    { 41, 0x46 },
    { 42, 0x3d },
    { 43, 0x00 },
    { 44, 0x05 },
    { 45, 0x9f },
    { 46, 0x00 },
    { 47, 0x05 },
    { 48, 0x9f },
    { 55, 0x00 },
    {131, 0x1f },
    {132, 0x02 },
    {137, 0x01 },
    {138, 0x0f },
    {139, 0xff },
    {142, 0x00 },
    {143, 0x00 },
    {136, 0x40 },
};

#endif

/* flash */

#if defined(PCIE_SDI_HW)
#define FLASH_READ_ID 0x9E
#elif defined(DUO2_HW)
#define FLASH_READ_ID 0x9F
#elif defined(MINI_4K_HW)
#define FLASH_READ_ID 0x9F
#endif

#define FLASH_READ    0x03
#define FLASH_WREN    0x06
#define FLASH_WRDI    0x04
#define FLASH_PP      0x02
#define FLASH_SE      0xD8
#define FLASH_BE      0xC7
#define FLASH_RDSR    0x05
#define FLASH_WRSR    0x01
/* status */
#define FLASH_WIP     0x01

#define FLASH_SECTOR_SIZE (1 << 16)

uint8_t sdi_flash_read(int fd, uint32_t addr);
int sdi_flash_get_erase_block_size(int fd);
int sdi_flash_write(int fd,
                     const uint8_t *buf, uint32_t base, uint32_t size,
                     void (*progress_cb)(void *opaque, const char *fmt, ...),
                     void *opaque);

/* spi */

#ifdef HAS_SI5324
void si5324_spi_write(int fd, uint8_t adr, uint8_t data);
uint8_t si5324_spi_read(int fd, uint16_t adr);
#endif

#ifdef HAS_GS12241
void rx_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data);
uint16_t rx_spi_read(int fd, uint8_t channel, uint16_t adr);
#endif

#ifdef HAS_GS12281
void tx_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data);
uint16_t tx_spi_read(int fd, uint8_t channel, uint16_t adr);
#endif

#ifdef HAS_LMH0387
void sdi_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data);
uint16_t sdi_spi_read(int fd, uint8_t channel, uint16_t adr);
#endif

/* genlock */

#ifdef HAS_GENLOCK
void si5324_genlock(int fd);
#endif

#endif /* SDI_LIB_H */
