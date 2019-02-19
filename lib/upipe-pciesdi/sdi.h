/*
 * PCIeSDI driver
 *
 * Copyright (C) 2018 / LambdaConcept / ramtin@lambdaconcept.com
 * Copyright (C) 2018 / LambdaConcept / po@lambdaconcept.com
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

#ifndef _LINUX_SDI_H
#define _LINUX_SDI_H

#include <linux/types.h>

#include "csr.h"
#include "sdi_config.h"

struct sdi_ioctl_reg {
    uint32_t addr;
    uint32_t val;
    uint8_t is_write;
};

struct sdi_ioctl_fan {
    uint8_t pwm_enable;
    uint32_t pwm_period;
    uint32_t pwm_width;
};

struct sdi_ioctl_flash {
    int tx_len; /* 8 to 40 */
    __u64 tx_data; /* 8 to 40 bits */
    __u64 rx_data; /* 40 bits */
};

struct sdi_ioctl_icap {
    uint8_t addr;
    uint32_t data;
};

struct sdi_ioctl_refclk {
    uint8_t refclk_sel;
    uint32_t refclk_freq;
    uint32_t refclk_counter;
};

#ifdef HAS_VCXOS
struct sdi_ioctl_vcxo {
    uint8_t pwm_enable;
    uint32_t pwm_period;
    uint32_t pwm_width;
};
#endif

#ifdef HAS_SI5324
struct sdi_ioctl_si5324_vcxo {
    uint8_t pwm_enable;
    uint32_t pwm_period;
    uint32_t pwm_width;
};

struct sdi_ioctl_si5324_spi {
    uint32_t tx_data;
    uint32_t rx_data;
};
#endif

#ifdef HAS_GENLOCK
struct sdi_ioctl_genlock {
    uint8_t active;
    uint64_t period;
    uint64_t seen;
    uint8_t field;
};
#endif

struct sdi_ioctl_dma {
    uint8_t fill;
    uint8_t tx_rx_loopback_enable;
    uint8_t rx_tx_loopback_enable;
};

struct sdi_ioctl_dma_writer {
    uint8_t enable;
    int64_t hw_count;
    int64_t sw_count;
};

struct sdi_ioctl_dma_reader {
    uint8_t enable;
    int64_t hw_count;
    int64_t sw_count;
};

struct sdi_ioctl_pattern {
    uint8_t mode;
    uint8_t enable;
    uint8_t format;
};

#ifdef HAS_GS12241
struct sdi_ioctl_gs12241_spi_cs {
    uint8_t cs_n;
};

struct sdi_ioctl_gs12241_spi {
    uint8_t cs_n;
    uint32_t tx_data;
    uint32_t rx_data;
};
#endif

#ifdef HAS_GS12281
struct sdi_ioctl_gs12281_spi_cs {
    uint8_t cs_n;
};

struct sdi_ioctl_gs12281_spi {
    uint32_t tx_data;
    uint32_t rx_data;
};
#endif

#ifdef HAS_LMH0387
struct sdi_ioctl_lmh0387_direction {
    uint8_t tx_enable;
};

struct sdi_ioctl_lmh0387_spi_cs {
    uint8_t cs_n;
};

struct sdi_ioctl_lmh0387_spi {
    uint8_t cs_n;
    uint32_t tx_data;
    uint32_t rx_data;
};
#endif

struct sdi_ioctl_rx {
    uint8_t crc_enable;
    uint8_t packed;
    uint8_t locked;
    uint8_t mode;
    uint8_t family;
    uint8_t scan;
    uint8_t rate;
};

struct sdi_ioctl_tx {
    uint8_t crc_enable;
    uint8_t packed;
    uint8_t mode;
    uint8_t txen;
    uint8_t slew;
};

struct sdi_ioctl_tx_rx_loopback {
    uint8_t config;
};

struct sdi_ioctl_lock {
    uint8_t dma_reader_request;
    uint8_t dma_writer_request;
    uint8_t dma_reader_release;
    uint8_t dma_writer_release;
    uint8_t dma_reader_status;
    uint8_t dma_writer_status;
};

struct sdi_ioctl_mmap_dma_info {
    uint64_t dma_tx_buf_offset;
    uint64_t dma_tx_buf_size;
    uint64_t dma_tx_buf_count;

    uint64_t dma_rx_buf_offset;
    uint64_t dma_rx_buf_size;
    uint64_t dma_rx_buf_count;
};

struct sdi_ioctl_mmap_dma_update {
    int64_t sw_count;
};

#define SDI_IOCTL 'S'

#define SDI_IOCTL_REG               _IOWR(SDI_IOCTL,  0, struct sdi_ioctl_reg)
#define SDI_IOCTL_FAN               _IOW(SDI_IOCTL,   1, struct sdi_ioctl_fan)
#define SDI_IOCTL_FLASH             _IOWR(SDI_IOCTL,  2, struct sdi_ioctl_flash)
#define SDI_IOCTL_ICAP              _IOWR(SDI_IOCTL,  3, struct sdi_ioctl_icap)
#define SDI_IOCTL_REFCLK            _IOWR(SDI_IOCTL,  4, struct sdi_ioctl_refclk)

#ifdef HAS_VCXOS
#define SDI_IOCTL_VCXO              _IOW(SDI_IOCTL,  10, struct sdi_ioctl_vcxo)
#endif
#ifdef HAS_SI5324
#define SDI_IOCTL_SI5324_VCXO       _IOW(SDI_IOCTL,  20, struct sdi_ioctl_si5324_vcxo)
#define SDI_IOCTL_SI5324_SPI        _IOWR(SDI_IOCTL, 21, struct sdi_ioctl_si5324_spi)
#endif

#ifdef HAS_GENLOCK
#define SDI_IOCTL_GENLOCK_HSYNC     _IOWR(SDI_IOCTL, 30, struct sdi_ioctl_genlock)
#define SDI_IOCTL_GENLOCK_VSYNC     _IOWR(SDI_IOCTL, 31, struct sdi_ioctl_genlock)
#endif

#define SDI_IOCTL_DMA                       _IOW(SDI_IOCTL,  40, struct sdi_ioctl_dma)
#define SDI_IOCTL_DMA_WRITER                _IOWR(SDI_IOCTL, 41, struct sdi_ioctl_dma_writer)
#define SDI_IOCTL_DMA_READER                _IOWR(SDI_IOCTL, 42, struct sdi_ioctl_dma_reader)
#define SDI_IOCTL_PATTERN                   _IOW(SDI_IOCTL,  43, struct sdi_ioctl_pattern)
#define SDI_IOCTL_MMAP_DMA_INFO             _IOR(SDI_IOCTL,  44, struct sdi_ioctl_mmap_dma_info)
#define SDI_IOCTL_MMAP_DMA_WRITER_UPDATE    _IOW(SDI_IOCTL,  45, struct sdi_ioctl_mmap_dma_update)
#define SDI_IOCTL_MMAP_DMA_READER_UPDATE    _IOW(SDI_IOCTL,  46, struct sdi_ioctl_mmap_dma_update)

#ifdef HAS_GS12241
#define SDI_IOCTL_RX_SPI_CS         _IOW(SDI_IOCTL,  50, struct sdi_ioctl_gs12241_spi_cs)
#define SDI_IOCTL_RX_SPI            _IOWR(SDI_IOCTL, 51, struct sdi_ioctl_gs12241_spi)
#endif

#ifdef HAS_GS12281
#define SDI_IOCTL_TX_SPI_CS         _IOW(SDI_IOCTL,  60, struct sdi_ioctl_gs12241_spi_cs)
#define SDI_IOCTL_TX_SPI            _IOWR(SDI_IOCTL, 61, struct sdi_ioctl_gs12281_spi)
#endif

#ifdef HAS_LMH0387
#define SDI_IOCTL_DIRECTION         _IOW(SDI_IOCTL,  70, struct sdi_ioctl_lmh0387_direction)
#define SDI_IOCTL_SPI_CS            _IOW(SDI_IOCTL,  71, struct sdi_ioctl_lmh0387_spi_cs)
#define SDI_IOCTL_SPI               _IOWR(SDI_IOCTL, 72, struct sdi_ioctl_lmh0387_spi)
#endif

#define SDI_IOCTL_RX                _IOWR(SDI_IOCTL, 80, struct sdi_ioctl_rx)
#define SDI_IOCTL_TX                _IOWR(SDI_IOCTL, 81, struct sdi_ioctl_tx)
#define SDI_IOCTL_TX_RX_LOOPBACK    _IOW(SDI_IOCTL,  82, struct sdi_ioctl_tx_rx_loopback)
#define SDI_IOCTL_LOCK              _IOWR(SDI_IOCTL, 83, struct sdi_ioctl_lock)

#endif /* _LINUX_SDI_H */
