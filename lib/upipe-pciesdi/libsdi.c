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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <time.h>

#include "sdi.h"
#include "sdi_config.h"
#include "csr.h"
#include "flags.h"

#include "libsdi.h"

int64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000U);
}

/* ioctl */

uint32_t sdi_readl(int fd, uint32_t addr) {
    struct sdi_ioctl_reg m;
    m.is_write = 0;
    m.addr = addr;
    ioctl(fd, SDI_IOCTL_REG, &m);
    return m.val;
}

void sdi_writel(int fd, uint32_t addr, uint32_t val) {
    struct sdi_ioctl_reg m;
    m.is_write = 1;
    m.addr = addr;
    m.val = val;
    ioctl(fd, SDI_IOCTL_REG, &m);
}

void sdi_refclk(int fd, uint8_t refclk_sel, uint32_t *refclk_freq, uint64_t *refclk_counter) {
    struct sdi_ioctl_refclk m;
    m.refclk_sel = refclk_sel;
    ioctl(fd, SDI_IOCTL_REFCLK, &m);
    *refclk_freq = m.refclk_freq;
    *refclk_counter = m.refclk_counter;
}

void sdi_capabilities(int fd, uint8_t *channels, uint8_t *has_vcxos,
        uint8_t *has_gs12241, uint8_t *has_gs12281, uint8_t *has_si5324,
        uint8_t *has_genlock, uint8_t *has_lmh0387, uint8_t *has_si596) {
    struct sdi_ioctl_capabilities m;
    ioctl(fd, SDI_IOCTL_CAPABILITIES, &m);
    *channels    = m.channels;
    *has_vcxos   = m.has_vcxos;
    *has_gs12241 = m.has_gs12241;
    *has_gs12281 = m.has_gs12281;
    *has_si5324  = m.has_si5324;
    *has_genlock = m.has_genlock;
    *has_lmh0387 = m.has_lmh0387;
    *has_si596   = m.has_si596;
}

void sdi_reload(int fd) {
    struct sdi_ioctl_icap m;
	m.addr = 0x4;
	m.data = 0xf;
    ioctl(fd, SDI_IOCTL_ICAP, &m);
}

void sdi_vcxo(int fd, uint32_t width, uint32_t period) {
    struct sdi_ioctl_vcxo m;
    m.pwm_enable = 1;
    m.pwm_width = width;
    m.pwm_period = period;
    ioctl(fd, SDI_IOCTL_VCXO, &m);
}

void sdi_si5324_vcxo(int fd, uint32_t width, uint32_t period) {
    struct sdi_ioctl_vcxo m;
    m.pwm_enable = 1;
    m.pwm_width = width;
    m.pwm_period = period;
    ioctl(fd, SDI_IOCTL_SI5324_VCXO, &m);
}

void sdi_si5324_spi(int fd, uint32_t tx_data, uint32_t *rx_data) {
    struct sdi_ioctl_si5324_spi m;
    m.tx_data = tx_data;
    ioctl(fd, SDI_IOCTL_SI5324_SPI, &m);
    *rx_data = m.rx_data;
}

void sdi_genlock_hsync(int fd, uint8_t *active, uint64_t *period, uint64_t *seen) {
    struct sdi_ioctl_genlock m;
    ioctl(fd, SDI_IOCTL_GENLOCK_HSYNC, &m);
    *active = m.active;
    *period = m.period;
    *seen = m.seen;
}

void sdi_genlock_vsync(int fd, uint8_t *active, uint64_t *period, uint64_t *seen) {
    struct sdi_ioctl_genlock m;
    ioctl(fd, SDI_IOCTL_GENLOCK_VSYNC, &m);
    *active = m.active;
    *period = m.period;
    *seen = m.seen;
}

void sdi_genlock_field(int fd, uint8_t *field) {
    struct sdi_ioctl_genlock m;
    ioctl(fd, SDI_IOCTL_GENLOCK_HSYNC, &m);
    *field = m.field;
}

void sdi_dma(int fd, uint8_t loopback_enable) {
    struct sdi_ioctl_dma m;
    m.loopback_enable = loopback_enable;
    ioctl(fd, SDI_IOCTL_DMA, &m);
}

void sdi_dma_writer(int fd, uint8_t enable, int64_t *hw_count, int64_t *sw_count) {
    struct sdi_ioctl_dma_writer m;
    m.enable = enable;
    ioctl(fd, SDI_IOCTL_DMA_WRITER, &m);
    *hw_count = m.hw_count;
    *sw_count = m.sw_count;
}

void sdi_dma_reader(int fd, uint8_t enable, int64_t *hw_count, int64_t *sw_count) {
    struct sdi_ioctl_dma_reader m;
    m.enable = enable;
    ioctl(fd, SDI_IOCTL_DMA_READER, &m);
    *hw_count = m.hw_count;
    *sw_count = m.sw_count;
}

void sdi_set_pattern(int fd, uint8_t mode, uint8_t enable, uint8_t format) {
    struct sdi_ioctl_pattern m;
    m.mode = mode;
    m.enable = enable;
    m.format = format;
    ioctl(fd, SDI_IOCTL_PATTERN, &m);
}

void sdi_gs12241_spi_cs(int fd, uint8_t cs_n) {
    struct sdi_ioctl_gs12241_spi_cs m;
    m.cs_n = cs_n;
    ioctl(fd, SDI_IOCTL_RX_SPI_CS, &m);
}

void sdi_gs12241_spi(int fd, uint32_t tx_data, uint32_t *rx_data) {
    struct sdi_ioctl_gs12241_spi m;
    m.tx_data = tx_data;
    ioctl(fd, SDI_IOCTL_RX_SPI, &m);
    *rx_data = m.rx_data;
}

void sdi_gs12281_spi_cs(int fd, uint8_t cs_n) {
    struct sdi_ioctl_gs12281_spi_cs m;
    m.cs_n = cs_n;
    ioctl(fd, SDI_IOCTL_TX_SPI_CS, &m);
}

void sdi_gs12281_spi(int fd, uint32_t tx_data, uint32_t *rx_data) {
    struct sdi_ioctl_gs12281_spi m;
    m.tx_data = tx_data;
    ioctl(fd, SDI_IOCTL_TX_SPI, &m);
    *rx_data = m.rx_data;
}

void sdi_lmh0387_direction(int fd, uint8_t tx_enable) {
    struct sdi_ioctl_lmh0387_direction m;
    m.tx_enable = tx_enable;
    ioctl(fd, SDI_IOCTL_LMH0387_DIRECTION, &m);
}

void sdi_lmh0387_spi_cs(int fd, uint8_t cs_n) {
    struct sdi_ioctl_lmh0387_spi_cs m;
    m.cs_n = cs_n;
    ioctl(fd, SDI_IOCTL_LMH0387_SPI_CS, &m);
}

void sdi_lmh0387_spi(int fd, uint32_t tx_data, uint32_t *rx_data) {
    struct sdi_ioctl_lmh0387_spi m;
    m.tx_data = tx_data;
    ioctl(fd, SDI_IOCTL_LMH0387_SPI, &m);
    *rx_data = m.rx_data;
}

void sdi_rx(int fd, uint8_t *locked, uint8_t *mode, uint8_t *family, uint8_t *scan, uint8_t *rate) {
    struct sdi_ioctl_rx m;
    m.crc_enable = 0;
    m.packed = SDI_DEVICE_IS_BITPACKED;
    ioctl(fd, SDI_IOCTL_RX, &m);
    *locked = m.locked;
    *mode = m.mode;
    *family = m.family;
    *scan = m.scan;
    *rate = m.rate;
}

void sdi_tx(int fd, uint8_t mode, uint8_t *txen, uint8_t *slew) {
    struct sdi_ioctl_tx m;
    m.crc_enable = 1;
    m.packed = SDI_DEVICE_IS_BITPACKED;
    m.mode = mode;
    ioctl(fd, SDI_IOCTL_TX, &m);
    *txen = m.txen;
    *slew = m.slew;
}

void sdi_tx_rx_loopback(int fd, uint8_t config) {
    struct sdi_ioctl_tx_rx_loopback m;
    m.config = config;
    ioctl(fd, SDI_IOCTL_TX_RX_LOOPBACK, &m);
}

/* lock */

uint8_t sdi_request_dma_reader(int fd) {
    struct sdi_ioctl_lock m;
    m.dma_reader_request = 1;
    m.dma_writer_request = 0;
    m.dma_reader_release = 0;
    m.dma_writer_release = 0;
    ioctl(fd, SDI_IOCTL_LOCK, &m);
    return m.dma_reader_status;
}

uint8_t sdi_request_dma_writer(int fd) {
    struct sdi_ioctl_lock m;
    m.dma_reader_request = 0;
    m.dma_writer_request = 1;
    m.dma_reader_release = 0;
    m.dma_writer_release = 0;
    ioctl(fd, SDI_IOCTL_LOCK, &m);
    return m.dma_writer_status;
}

void sdi_release_dma_reader(int fd) {
    struct sdi_ioctl_lock m;
    m.dma_reader_request = 0;
    m.dma_writer_request = 0;
    m.dma_reader_release = 1;
    m.dma_writer_release = 0;
    ioctl(fd, SDI_IOCTL_LOCK, &m);
}

void sdi_release_dma_writer(int fd) {
    struct sdi_ioctl_lock m;
    m.dma_reader_request = 0;
    m.dma_writer_request = 0;
    m.dma_reader_release = 0;
    m.dma_writer_release = 1;
    ioctl(fd, SDI_IOCTL_LOCK, &m);
}

/* flash */

static uint64_t flash_spi(int fd, int tx_len, uint8_t cmd,
                          uint32_t tx_data)
{
    struct sdi_ioctl_flash m;
    m.tx_len = tx_len;
    m.tx_data = tx_data | ((uint64_t)cmd << 32);
    if (ioctl(fd, SDI_IOCTL_FLASH, &m) < 0) {
        perror("SDI_IOCTL_FLASH");
        exit(1);
    }
    return m.rx_data;
}

uint32_t flash_read_id(int fd, int reg)
{
    return flash_spi(fd, 32, reg, 0) & 0xffffff;
}

static void flash_write_enable(int fd)
{
    flash_spi(fd, 8, FLASH_WREN, 0);
}

static void flash_write_disable(int fd)
{
    flash_spi(fd, 8, FLASH_WRDI, 0);
}

static uint8_t flash_read_status(int fd)
{
    return flash_spi(fd, 16, FLASH_RDSR, 0) & 0xff;
}

static __attribute__((unused)) void flash_write_status(int fd, uint8_t value)
{
    flash_spi(fd, 16, FLASH_WRSR, value << 24);
}

static __attribute__((unused)) void flash_erase_sector(int fd, uint32_t addr)
{
    flash_spi(fd, 32, FLASH_SE, addr << 8);
}

static __attribute__((unused)) uint8_t flash_read_sector_lock(int fd, uint32_t addr)
{
    return flash_spi(fd, 40, FLASH_WRSR, addr << 8) & 0xff;
}

static __attribute__((unused)) void flash_write_sector_lock(int fd, uint32_t addr, uint8_t byte)
{
    flash_spi(fd, 40, FLASH_WRSR, (addr << 8) | byte);
}

static void flash_write(int fd, uint32_t addr, uint8_t byte)
{
    flash_spi(fd, 40, FLASH_PP, (addr << 8) | byte);
}

uint8_t sdi_flash_read(int fd, uint32_t addr)
{
    return flash_spi(fd, 40, FLASH_READ, addr << 8) & 0xff;
}

int sdi_flash_get_erase_block_size(int fd)
{
    return FLASH_SECTOR_SIZE;
}

int sdi_flash_write(int fd,
                     const uint8_t *buf, uint32_t base, uint32_t size,
                     void (*progress_cb)(void *opaque, const char *fmt, ...),
                     void *opaque)
{
    int i, errors, retry;

    /* dummy command because in some case the first erase does not
       work. */
    flash_read_id(fd, 0);

#if 0
    /* erase */
    for(i = 0; i < size; i += FLASH_SECTOR_SIZE) {
        if (progress_cb) {
            progress_cb(opaque, "Erasing %08x\r", base + i);
        }
        flash_write_enable(fd);
        flash_erase_sector(fd, base + i);
        while (flash_read_status(fd) & FLASH_WIP) {
            usleep(10 * 1000);
        }
    }
    if (progress_cb) {
        progress_cb(opaque, "\n");
    }
#else
    /* erase full flash */
    printf("Erasing...\n");
    flash_write_enable(fd);
    flash_spi(fd, 8, 0xC7, 0);
    while (flash_read_status(fd) & FLASH_WIP) {
        usleep(10 * 1000);
    }
#endif
    flash_write_disable(fd);

    i = errors = retry = 0;
    while (i < size) {
        if (progress_cb && (i % FLASH_SECTOR_SIZE) == 0) {
            progress_cb(opaque, "Writing %08x\r", base + i);
        }

        /* program */
        while (flash_read_status(fd) & FLASH_WIP) {
            usleep(10 * 1000);
        }
        flash_write_enable(fd);
        flash_write(fd, base + i, buf[i]);
        flash_write_disable(fd);

        /* verify */
        while (flash_read_status(fd) & FLASH_WIP)
            usleep(10 * 1000);
        if (sdi_flash_read(fd, base + i) != buf[i]) {
            retry += 1;
        } else {
            if (retry && progress_cb) {
                progress_cb(opaque, "Retried %d times at 0x%08x\n",
                        retry, base+i);
            }
            i += 1;
            retry = 0;
        }

        if (retry > 10) {
            if (retry && progress_cb) {
                progress_cb(opaque, "Max retry reached at 0x%08x, continuing\n",
                        base+i);
            }
            retry = 0;
            i += 1;
            errors += 1;
        }
    }

    if (progress_cb) {
        progress_cb(opaque, "\n");
    }

    return errors;
}

/* spi */

void si5324_spi_write(int fd, uint8_t adr, uint8_t data)
{
    uint32_t tx_data, rx_data;
    /* set address */
    tx_data = (0b00000000 << 8) | adr;
    sdi_si5324_spi(fd, tx_data, &rx_data);
    /* write */
    tx_data = (0b01000000 << 8) | data;
    sdi_si5324_spi(fd, tx_data, &rx_data);
}

uint8_t si5324_spi_read(int fd, uint16_t adr)
{
    uint32_t tx_data, rx_data;
    /* set address */
    tx_data = (0b00000000 << 8) | adr;
    sdi_si5324_spi(fd, tx_data, &rx_data);
    /* read */
    tx_data = (0b10000000 << 8);
    sdi_si5324_spi(fd, tx_data, &rx_data);
    return rx_data & 0xff;
}

void gs12241_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data)
{
    uint32_t cmd;
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_gs12241_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd */
    cmd = (0 << 31) | (0 << 30) | (1 << 29) | adr;
    tx_data = (cmd >> 16) & 0xffff;
    sdi_gs12241_spi(fd, tx_data, &rx_data);
    tx_data = cmd & 0xffff;
    sdi_gs12241_spi(fd, tx_data, &rx_data);

    /* send data */
    tx_data = data;
    sdi_gs12241_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_gs12241_spi_cs(fd, 0b1111);
}

uint16_t gs12241_spi_read(int fd, uint8_t channel, uint16_t adr)
{
    uint32_t cmd;
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_gs12241_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd */
    cmd = (1 << 31) | (0 << 30) | (1 << 29) | adr;
    tx_data = (cmd >> 16) & 0xffff;
    sdi_gs12241_spi(fd, tx_data, &rx_data);
    tx_data = cmd & 0xffff;
    sdi_gs12241_spi(fd, tx_data, &rx_data);

    /* receive data */
    tx_data = 0;
    sdi_gs12241_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_gs12241_spi_cs(fd, 0b1111);

    return rx_data & 0xffff;
}

void gs12241_spi_init(int fd)
{
    int i;
    /* sdo sharing */
    for (i=0; i<4; i++)
        gs12241_spi_write(fd, i, 0, 1 << 13); /* gspi_bus_through_enable */
}

void gs12241_reset(int fd, int n)
{
    gs12241_spi_write(fd, n, 0x7f, 0xad00); /* chip reset (pulse/release) */
}

void gs12241_config_for_sd(int fd, int n)
{
    int i;
    /* FIXME: loop since not taken into account if too early after reset */
    for (i=0; i<128; i++) {
        gs12241_spi_write(fd, n, 0x2b, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x29, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x2d, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x2f, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x31, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x33, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x35, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x37, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x39, (35 << 8) | 0x70);
        gs12241_spi_write(fd, n, 0x3b, (35 << 8) | 0x70);
    }
}

void gs12281_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data)
{
    uint32_t cmd;
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_gs12281_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd */
    cmd = (0 << 31) | (0 << 30) | (1 << 29) | adr;
    tx_data = (cmd >> 16) & 0xffff;
    sdi_gs12281_spi(fd, tx_data, &rx_data);
    tx_data = cmd & 0xffff;
    sdi_gs12281_spi(fd, tx_data, &rx_data);

    /* send data */
    tx_data = data;
    sdi_gs12281_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_gs12281_spi_cs(fd, 0b1111);
}

uint16_t gs12281_spi_read(int fd, uint8_t channel, uint16_t adr)
{
    uint32_t cmd;
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_gs12281_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd */
    cmd = (1 << 31) | (0 << 30) | (1 << 29) | adr;
    tx_data = (cmd >> 16) & 0xffff;
    sdi_gs12281_spi(fd, tx_data, &rx_data);
    tx_data = cmd & 0xffff;
    sdi_gs12281_spi(fd, tx_data, &rx_data);

    /* receive data */
    tx_data = 0;
    sdi_gs12281_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_gs12281_spi_cs(fd, 0b1111);

    return rx_data & 0xffff;
}

void gs12281_spi_init(int fd)
{
    int i;
    /* sdo sharing */
    for (i=0; i<4; i++)
        gs12281_spi_write(fd, i, 0, 1 << 13); /* gspi_bus_through_enable */
}

void sdi_lmh0387_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data)
{
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_lmh0387_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd & data*/
    tx_data = (0 << 15) | ((adr & 0x3f) << 8) | (data & 0xff);
    sdi_lmh0387_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_lmh0387_spi_cs(fd, 0b1111);
}

uint16_t sdi_lmh0387_spi_read(int fd, uint8_t channel, uint16_t adr)
{
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_lmh0387_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd  & data*/
    tx_data = (1 << 15) | ((adr & 0x3f) << 8);
    sdi_lmh0387_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_lmh0387_spi_cs(fd, 0b1111);

    return rx_data & 0xff;
}

/* genlock */

static int hsync_check(uint64_t reference, uint64_t value) {
    if (value < (reference - GENLOCK_HSYNC_MARGIN))
        return 0;
    if (value > reference + GENLOCK_HSYNC_MARGIN)
        return 0;
    return 1;
}

static int vsync_check(uint64_t reference, uint64_t value) {
    if (value < (reference - GENLOCK_VSYNC_MARGIN))
        return 0;
    if (value > reference + GENLOCK_VSYNC_MARGIN)
        return 0;
    return 1;
}

static uint16_t si5324_base_config_regs[][2] = {
    {   0, 0x14 },
    {   1, 0xe4 },
    {   2, 0x32 },
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
    {  25, 0x40 },
    {  31, 0x00 },
    {  32, 0x00 },
    {  33, 0x05 },
    {  34, 0x00 },
    {  35, 0x00 },
    {  36, 0x05 },
    {  40, 0x01 },
    {  41, 0x4e },
    {  42, 0x1f },
    {  43, 0x00 },
    {  44, 0x00 },
    {  45, 0x00 },
    {  46, 0x00 },
    {  47, 0x00 },
    {  48, 0x00 },
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

void si5324_genlock(int fd)
{
    int i;

    uint8_t hsync_active;
    uint64_t hsync_period;
    uint64_t hsync_seen;

    uint8_t vsync_active;
    uint64_t vsync_period;
    uint64_t vsync_seen;

    /* get hsync */
    sdi_genlock_hsync(fd, &hsync_active, &hsync_period, &hsync_seen);
    printf("HSYNC_ACTIVE: %d, HSYNC_PERIOD: %" PRIu64 " ns\n", hsync_active, hsync_period);

    /* get vsync */
    sdi_genlock_vsync(fd, &vsync_active, &vsync_period, &vsync_seen);
    printf("VSYNC_ACTIVE: %d, VSYNC_PERIOD: %" PRIu64 " ns\n", vsync_active, vsync_period);

    /* configure vcxo to 50% */
    sdi_si5324_vcxo(fd, 512<<10, 1024<<10);

    /* detect video format */
    /* SMPTE259M */
    if (hsync_check(SMPTE259M_PAL_HSYNC_PERIOD, hsync_period) &
        vsync_check(SMPTE259M_PAL_VSYNC_PERIOD, vsync_period*2)) {
        printf("SMPTE259M_PAL detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte259m_pal_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte259m_pal_regs[i][1];
    } else if (hsync_check(SMPTE259M_NTSC_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE259M_NTSC_VSYNC_PERIOD, vsync_period*2)) {
        printf("SMPTE259M_NTSC detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte259m_pal_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte259m_ntsc_regs[i][1];
    /* SMPTE296M */
    } else if (hsync_check(SMPTE296M_720P60_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE296M_720P60_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE296M_720P60 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte296m_720p60_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte296m_720p60_regs[i][1];
    } else if (hsync_check(SMPTE296M_720P50_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE296M_720P50_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE296M_720P50 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte296m_720p50_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte296m_720p50_regs[i][1];
    } else if (hsync_check(SMPTE296M_720P30_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE296M_720P30_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE296M_720P30 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte296m_720p30_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte296m_720p30_regs[i][1];
    } else if (hsync_check(SMPTE296M_720P25_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE296M_720P25_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE296M_720P25 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte296m_720p25_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte296m_720p25_regs[i][1];
    } else if (hsync_check(SMPTE296M_720P24_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE296M_720P24_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE296M_720P24 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte296m_720p24_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte296m_720p24_regs[i][1];
    } else if (hsync_check(SMPTE296M_720P59_94_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE296M_720P59_94_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE296M_720P59_94 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte296m_720p59_94_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte296m_720p59_94_regs[i][1];
    } else if (hsync_check(SMPTE296M_720P29_97_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE296M_720P29_97_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE296M_720P29_97 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte296m_720p29_97_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte296m_720p29_97_regs[i][1];
    } else if (hsync_check(SMPTE296M_720P23_98_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE296M_720P23_98_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE296M_720P23_98 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte296m_720p23_98_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte296m_720p23_98_regs[i][1];
    /* SMPTE274M */
    } else if (hsync_check(SMPTE274M_1080P60_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080P60_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE274M_1080P60 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080p60_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080p60_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080P50_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080P50_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE274M_1080P50 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080p50_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080p50_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080I60_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080I60_VSYNC_PERIOD, vsync_period*2)) {
        printf("SMPTE274M_1080I60 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080i60_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080i60_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080I50_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080I50_VSYNC_PERIOD, vsync_period*2)) {
        printf("SMPTE274M_1080I50 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080i50_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080i50_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080P30_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080P30_VSYNC_PERIOD, vsync_period*2)) {
        printf("SMPTE274M_1080P30 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080p30_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080p30_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080P25_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080P25_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE274M_1080P25 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080p25_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080p25_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080P24_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080P24_VSYNC_PERIOD, vsync_period*2)) {
        printf("SMPTE274M_1080P24 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080p24_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080p24_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080P59_94_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080P59_94_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE274M_1080P59_94 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080p59_94_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080p59_94_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080I59_94_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080I59_94_VSYNC_PERIOD, vsync_period*2)) {
        printf("SMPTE274M_1080I59_94 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080i59_94_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080i59_94_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080P29_97_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080P29_97_VSYNC_PERIOD, vsync_period)) {
        printf("SMPTE274M_1080P29_97 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080p29_97_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080p29_97_regs[i][1];
    } else if (hsync_check(SMPTE274M_1080P23_98_HSYNC_PERIOD, hsync_period) &
               vsync_check(SMPTE274M_1080P23_98_VSYNC_PERIOD, vsync_period*2)) {
        printf("SMPTE274M_1080P23_98 detected, configuring SI5324...\n");
        for(i = 0; i < countof(smpte274m_1080p23_98_regs); i++)
            si5324_base_config_regs[SI5324_BASE_CONFIG_N2_OFFSET + i][1] = smpte274m_1080p23_98_regs[i][1];
    } else {
        printf("No valid video format detected\n");
    }

    /* configure si5324 */
    for(i = 0; i < countof(si5324_base_config_regs); i++) {
        si5324_spi_write(fd, si5324_base_config_regs[i][0], si5324_base_config_regs[i][1]);
    }
}
