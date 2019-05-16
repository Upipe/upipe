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

#define SDI_DEVICE_IS_BITPACKED 1


int64_t get_time_ms(void);

/* ioctl */

uint32_t sdi_readl(int fd, uint32_t addr);
void sdi_writel(int fd, uint32_t addr, uint32_t val);
void sdi_reload(int fd);

void sdi_refclk(int fd, uint8_t refclk_sel, uint32_t *refclk_freq, uint64_t *refclk_counter);

void sdi_capabilities(int fd, uint8_t *channels, uint8_t *has_vcxos,
        uint8_t *has_gs12241, uint8_t *has_gs12281, uint8_t *has_si5324,
        uint8_t *has_genlock, uint8_t *has_lmh0387, uint8_t *has_si596);

void sdi_vcxo(int fd, uint32_t width, uint32_t period);

void sdi_si5324_vcxo(int fd, uint32_t width, uint32_t period);
void sdi_si5324_spi(int fd, uint32_t tx_data, uint32_t *rx_data);

void sdi_genlock_hsync(int fd, uint8_t *active, uint64_t *period, uint64_t *seen);
void sdi_genlock_vsync(int fd, uint8_t *active, uint64_t *period, uint64_t *seen);
void sdi_genlock_field(int fd, uint8_t *field);

void sdi_dma(int fd, uint8_t loopback_enable);
void sdi_dma_reader(int fd, uint8_t enable, int64_t *hw_count, int64_t *sw_count);
void sdi_dma_writer(int fd, uint8_t enable, int64_t *hw_count, int64_t *sw_count);
void sdi_set_pattern(int fd, uint8_t mode, uint8_t enable, uint8_t format);

void sdi_gs12241_spi_cs(int fd, uint8_t cs_n);
void sdi_gs12241_spi(int fd, uint32_t tx_data, uint32_t *rx_data);

void sdi_gs12281_spi_cs(int fd, uint8_t cs_n);
void sdi_gs12281_spi(int fd, uint32_t tx_data, uint32_t *rx_data);

void sdi_lmh0387_direction(int fd, uint8_t tx_enable);
void sdi_lmh0387_spi_cs(int fd, uint8_t cs_n);
void sdi_lmh0387_spi(int fd, uint32_t tx_data, uint32_t *rx_data);

void sdi_rx(int fd, uint8_t *locked, uint8_t *mode, uint8_t *family, uint8_t *scan, uint8_t *rate);
void sdi_tx(int fd, uint8_t mode, uint8_t *txen, uint8_t *slew);
void sdi_tx_rx_loopback(int fd, uint8_t config);

uint8_t sdi_request_dma_reader(int fd);
uint8_t sdi_request_dma_writer(int fd);
void sdi_release_dma_reader(int fd);
void sdi_release_dma_writer(int fd);

#define countof(x) (sizeof(x) / sizeof(x[0]))

/* si5324 */

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

/* flash */

#define FALCON9_FLASH_READ_ID_REG 0x9E
#define MINI_4K_FLASH_READ_ID_REG 0x9F
#define DUO2_FLASH_READ_ID_REG 0x9F

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

void si5324_spi_write(int fd, uint8_t adr, uint8_t data);
uint8_t si5324_spi_read(int fd, uint16_t adr);

void gs12241_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data);
uint16_t gs12241_spi_read(int fd, uint8_t channel, uint16_t adr);
void gs12241_spi_init(int fd);
void gs12241_reset(int fd, int n);
void gs12241_config_for_sd(int fd, int n);

void gs12281_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data);
uint16_t gs12281_spi_read(int fd, uint8_t channel, uint16_t adr);
void gs12281_spi_init(int fd);

void sdi_lmh0387_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data);
uint16_t sdi_lmh0387_spi_read(int fd, uint8_t channel, uint16_t adr);

/* genlock */

/* genlock margins (in ns) */

#define GENLOCK_HSYNC_MARGIN 20
#define GENLOCK_VSYNC_MARGIN 10000

/* genlock hsync/vsync periods (in ns) */

/* SMPTE259M */
#define SMPTE259M_PAL_HSYNC_PERIOD 64000
#define SMPTE259M_PAL_VSYNC_PERIOD 40000000

#define SMPTE259M_NTSC_HSYNC_PERIOD 63555
#define SMPTE259M_NTSC_VSYNC_PERIOD 33366700

/* SMPTE296M */
#define SMPTE296M_720P60_HSYNC_PERIOD 22222
#define SMPTE296M_720P60_VSYNC_PERIOD 16666666

#define SMPTE296M_720P50_HSYNC_PERIOD 26666
#define SMPTE296M_720P50_VSYNC_PERIOD 20000000

#define SMPTE296M_720P30_HSYNC_PERIOD 44444
#define SMPTE296M_720P30_VSYNC_PERIOD 33333333

#define SMPTE296M_720P25_HSYNC_PERIOD 53333
#define SMPTE296M_720P25_VSYNC_PERIOD 40000000

#define SMPTE296M_720P24_HSYNC_PERIOD 55555
#define SMPTE296M_720P24_VSYNC_PERIOD 41666666

#define SMPTE296M_720P59_94_HSYNC_PERIOD 22244
#define SMPTE296M_720P59_94_VSYNC_PERIOD 16683350

#define SMPTE296M_720P29_97_HSYNC_PERIOD 44488
#define SMPTE296M_720P29_97_VSYNC_PERIOD 33366700

#define SMPTE296M_720P23_98_HSYNC_PERIOD 55601
#define SMPTE296M_720P23_98_VSYNC_PERIOD 41701417

/* SMPTE274M */
#define SMPTE274M_1080P60_HSYNC_PERIOD 14814
#define SMPTE274M_1080P60_VSYNC_PERIOD 16666666

#define SMPTE274M_1080P50_HSYNC_PERIOD 17777
#define SMPTE274M_1080P50_VSYNC_PERIOD 20000000

#define SMPTE274M_1080I60_HSYNC_PERIOD 29629
#define SMPTE274M_1080I60_VSYNC_PERIOD 33333333

#define SMPTE274M_1080I50_HSYNC_PERIOD 35555
#define SMPTE274M_1080I50_VSYNC_PERIOD 40000000

#define SMPTE274M_1080P30_HSYNC_PERIOD 29629
#define SMPTE274M_1080P30_VSYNC_PERIOD 33333333

#define SMPTE274M_1080P25_HSYNC_PERIOD 35555
#define SMPTE274M_1080P25_VSYNC_PERIOD 40000000

#define SMPTE274M_1080P24_HSYNC_PERIOD 37037
#define SMPTE274M_1080P24_VSYNC_PERIOD 41666666

#define SMPTE274M_1080P59_94_HSYNC_PERIOD 14829
#define SMPTE274M_1080P59_94_VSYNC_PERIOD 16683350

#define SMPTE274M_1080I59_94_HSYNC_PERIOD 29659
#define SMPTE274M_1080I59_94_VSYNC_PERIOD 33366700

#define SMPTE274M_1080P29_97_HSYNC_PERIOD 29659
#define SMPTE274M_1080P29_97_VSYNC_PERIOD 33366700

#define SMPTE274M_1080P23_98_HSYNC_PERIOD 37067
#define SMPTE274M_1080P23_98_VSYNC_PERIOD 41701417

/* genlock si5324 configurations */

#define SI5324_BASE_CONFIG_N2_OFFSET 25

/* SMPTE259M */

static const uint16_t smpte259m_pal_regs[][2] = {
    { 40, 0x01 },
    { 41, 0x4e },
    { 42, 0x1f },
};

static const uint16_t smpte259m_ntsc_regs[][2] = {
    { 40, 0x01 },
    { 41, 0x4b },
    { 42, 0xc4 },
};

/* SMPTE296M */

static const uint16_t smpte296m_720p60_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x74 },
    { 42, 0x03 },
};

static const uint16_t smpte296m_720p50_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x8b },
    { 42, 0x37 },
};

static const uint16_t smpte296m_720p30_regs[][2] = {
    { 40, 0x00 },
    { 41, 0xe8 },
    { 42, 0x07 },
};

static const uint16_t smpte296m_720p25_regs[][2] = {
    { 40, 0x01 },
    { 41, 0x16 },
    { 42, 0x6f },
};

static const uint16_t smpte296m_720p24_regs[][2] = {
    { 40, 0x01 },
    { 41, 0x22 },
    { 42, 0x09 },
};

static const uint16_t smpte296m_720p59_94_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x74 },
    { 42, 0x03 },
};

static const uint16_t smpte296m_720p29_97_regs[][2] = {
    { 40, 0x00 },
    { 41, 0xe8 },
    { 42, 0x07 },
};

static const uint16_t smpte296m_720p23_98_regs[][2] = {
    { 40, 0x01 },
    { 41, 0x22 },
    { 42, 0x09 },
};

/* SMPTE274M */

static const uint16_t smpte274m_1080p60_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x4d },
    { 42, 0x57 },
};

static const uint16_t smpte274m_1080p50_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x5c },
    { 42, 0xcf },
};

static const uint16_t smpte274m_1080i60_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x9a },
    { 42, 0xaf },
};

static const uint16_t smpte274m_1080i50_regs[][2] = {
    { 40, 0x00 },
    { 41, 0xb9 },
    { 42, 0x9f },
};

static const uint16_t smpte274m_1080p30_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x9a },
    { 42, 0xaf },
};

static const uint16_t smpte274m_1080p25_regs[][2] = {
    { 40, 0x00 },
    { 41, 0xb9 },
    { 42, 0x9f },
};

static const uint16_t smpte274m_1080p24_regs[][2] = {
    { 40, 0x00 },
    { 41, 0xc1 },
    { 42, 0x5b },
};

static const uint16_t smpte274m_1080p59_94_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x4d },
    { 42, 0x57 },
};

static const uint16_t smpte274m_1080i59_94_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x9a },
    { 42, 0xaf },
};

static const uint16_t smpte274m_1080p29_97_regs[][2] = {
    { 40, 0x00 },
    { 41, 0x9a },
    { 42, 0xaf },
};

static const uint16_t smpte274m_1080p23_98_regs[][2] = {
    { 40, 0x00 },
    { 41, 0xc1 },
    { 42, 0x52 },
};

void si5324_genlock(int fd);

#endif /* SDI_LIB_H */
