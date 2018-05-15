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

#include <bitstream/smpte/291.h>

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

void sdi_icap(int fd) {
    struct sdi_ioctl_icap m;
    m.prog = 1;
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

void sdi_refclk(int fd, uint8_t refclk_sel,
    uint32_t *refclk0_freq, uint32_t *refclk1_freq,
    uint32_t *refclk0_counter, uint32_t *refclk1_counter) {
    struct sdi_ioctl_refclk m;
    m.refclk_sel = refclk_sel;
    ioctl(fd, SDI_IOCTL_REFCLK, &m);
    *refclk0_freq = m.refclk0_freq*2;
    *refclk1_freq = m.refclk1_freq*2;
    *refclk0_counter = m.refclk0_counter*2;
    *refclk1_counter = m.refclk1_counter*2;
}

void sdi_dma(int fd, uint8_t fill, uint8_t rx_tx_loopback_enable, uint8_t tx_rx_loopback_enable) {
    struct sdi_ioctl_dma m;
    m.fill = fill;
    m.rx_tx_loopback_enable = rx_tx_loopback_enable;
    m.tx_rx_loopback_enable = tx_rx_loopback_enable;
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

void sdi_set_pattern(int fd, uint8_t enable, uint8_t format) {
    struct sdi_ioctl_pattern m;
    m.enable = enable;
    m.format = format;
    ioctl(fd, SDI_IOCTL_PATTERN, &m);
}

void sdi_rx_spi_cs(int fd, uint8_t cs_n) {
    struct sdi_ioctl_rx_spi_cs m;
    m.cs_n = cs_n;
    ioctl(fd, SDI_IOCTL_RX_SPI_CS, &m);
}

void sdi_rx_spi(int fd, uint32_t tx_data, uint32_t *rx_data) {
    struct sdi_ioctl_rx_spi m;
    m.tx_data = tx_data;
    ioctl(fd, SDI_IOCTL_RX_SPI, &m);
    *rx_data = m.rx_data;
}

void sdi_rx(int fd, uint8_t *locked, uint8_t *mode, uint8_t *family, uint8_t *scan, uint8_t *rate) {
    struct sdi_ioctl_rx m;
    m.crc_enable = 1;
    ioctl(fd, SDI_IOCTL_RX, &m);
    *locked = m.locked;
    *mode = m.mode;
    *family = m.family;
    *scan = m.scan;
    *rate = m.rate;
}

void sdi_tx_spi_cs(int fd, uint8_t cs_n) {
    struct sdi_ioctl_tx_spi_cs m;
    m.cs_n = cs_n;
    ioctl(fd, SDI_IOCTL_TX_SPI_CS, &m);
}

void sdi_tx_spi(int fd, uint32_t tx_data, uint32_t *rx_data) {
    struct sdi_ioctl_tx_spi m;
    m.tx_data = tx_data;
    ioctl(fd, SDI_IOCTL_TX_SPI, &m);
    *rx_data = m.rx_data;
}

void sdi_tx(int fd, uint8_t mode, uint8_t *txen, uint8_t *slew) {
    struct sdi_ioctl_tx m;
    m.crc_enable = 1;
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

static uint32_t flash_read_id(int fd)
{
    return flash_spi(fd, 32, FLASH_READ_ID, 0) & 0xffffff;
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

static void flash_erase_sector(int fd, uint32_t addr)
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

void sdi_flash_write(int fd,
                     const uint8_t *buf, uint32_t base, uint32_t size,
                     void (*progress_cb)(void *opaque, const char *fmt, ...),
                     void *opaque)
{
    int i;

    /* dummy command because in some case the first erase does not
       work. */
    flash_read_id(fd);

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
    flash_write_disable(fd);

    /* program */
    for(i = 0; i < size; i++) {
        if (progress_cb && (i % FLASH_SECTOR_SIZE) == 0) {
            progress_cb(opaque, "Writing %08x\r", base + i);
        }
        while (flash_read_status(fd) & FLASH_WIP) {
            usleep(10 * 1000);
        }
        flash_write_enable(fd);
        flash_write(fd, base + i, buf[i]);
        flash_write_disable(fd);
    }
    if (progress_cb) {
        progress_cb(opaque, "\n");
    }
}

/* spi */

void rx_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data)
{
    uint32_t cmd;
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_rx_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd */
    cmd = (0 << 31) | (0 << 30) | (1 << 29) | adr;
    tx_data = (cmd >> 16) & 0xffff;
    sdi_rx_spi(fd, tx_data, &rx_data);
    tx_data = cmd & 0xffff;
    sdi_rx_spi(fd, tx_data, &rx_data);

    /* send data */
    tx_data = data;
    sdi_rx_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_rx_spi_cs(fd, 0b1111);
}

uint16_t rx_spi_read(int fd, uint8_t channel, uint16_t adr)
{
    uint32_t cmd;
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_rx_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd */
    cmd = (1 << 31) | (0 << 30) | (1 << 29) | adr;
    tx_data = (cmd >> 16) & 0xffff;
    sdi_rx_spi(fd, tx_data, &rx_data);
    tx_data = cmd & 0xffff;
    sdi_rx_spi(fd, tx_data, &rx_data);

    /* receive data */
    tx_data = 0;
    sdi_rx_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_rx_spi_cs(fd, 0b1111);

    return rx_data & 0xffff;
}

void tx_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data)
{
    uint32_t cmd;
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_tx_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd */
    cmd = (0 << 31) | (0 << 30) | (1 << 29) | adr;
    tx_data = (cmd >> 16) & 0xffff;
    sdi_tx_spi(fd, tx_data, &rx_data);
    tx_data = cmd & 0xffff;
    sdi_tx_spi(fd, tx_data, &rx_data);

    /* send data */
    tx_data = data;
    sdi_tx_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_tx_spi_cs(fd, 0b1111);
}

uint16_t tx_spi_read(int fd, uint8_t channel, uint16_t adr)
{
    uint32_t cmd;
    uint32_t tx_data, rx_data;

    /* set chip_select */
    sdi_tx_spi_cs(fd, 0b1111 ^ (1 << channel));

    /* send cmd */
    cmd = (1 << 31) | (0 << 30) | (1 << 29) | adr;
    tx_data = (cmd >> 16) & 0xffff;
    sdi_tx_spi(fd, tx_data, &rx_data);
    tx_data = cmd & 0xffff;
    sdi_tx_spi(fd, tx_data, &rx_data);

    /* receive data */
    tx_data = 0;
    sdi_tx_spi(fd, tx_data, &rx_data);

    /* release chip_select */
    sdi_tx_spi_cs(fd, 0b1111);

    return rx_data & 0xffff;
}

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
