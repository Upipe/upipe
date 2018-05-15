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

struct sdi_ioctl_reg {
    uint32_t addr;
    uint32_t val;
    uint8_t is_write;
};

struct sdi_ioctl_flash {
    int tx_len; /* 8 to 40 */
    __u64 tx_data; /* 8 to 40 bits */
    __u64 rx_data; /* 40 bits */
};

struct sdi_ioctl_icap {
    uint8_t prog;
};

struct sdi_ioctl_refclk {
    uint8_t refclk_sel;
    uint32_t refclk0_freq;
    uint32_t refclk1_freq;
    uint32_t refclk0_counter;
    uint32_t refclk1_counter;
};

struct sdi_ioctl_vcxo {
    uint8_t pwm_enable;
    uint32_t pwm_period;
    uint32_t pwm_width;
};

struct sdi_ioctl_si5324_vcxo {
    uint8_t pwm_enable;
    uint32_t pwm_period;
    uint32_t pwm_width;
};

struct sdi_ioctl_si5324_spi {
    uint32_t tx_data;
    uint32_t rx_data;
};

struct sdi_ioctl_fan {
    uint8_t pwm_enable;
    uint32_t pwm_period;
    uint32_t pwm_width;
};

struct sdi_ioctl_dma {
    uint8_t loopback_enable;
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
    uint8_t enable;
    uint8_t format;
};

struct sdi_ioctl_rx_spi_cs {
    uint8_t cs_n;
};

struct sdi_ioctl_rx_spi {
    uint8_t cs_n;
    uint32_t tx_data;
    uint32_t rx_data;
};

struct sdi_ioctl_rx {
    uint8_t crc_enable;
    uint8_t locked;
    uint8_t mode;
    uint8_t family;
    uint8_t rate;
};

struct sdi_ioctl_tx_spi_cs {
    uint8_t cs_n;
};

struct sdi_ioctl_tx_spi {
    uint32_t tx_data;
    uint32_t rx_data;
};

struct sdi_ioctl_tx {
    uint8_t crc_enable;
    uint8_t txen;
    uint8_t slew;
};

#define SDI_IOCTL 'S'

#define SDI_IOCTL_REG         _IOWR(SDI_IOCTL,  0, struct sdi_ioctl_reg)
#define SDI_IOCTL_FLASH       _IOWR(SDI_IOCTL,  1, struct sdi_ioctl_flash)
#define SDI_IOCTL_ICAP        _IOWR(SDI_IOCTL,  2, struct sdi_ioctl_flash)
#define SDI_IOCTL_REFCLK      _IOWR(SDI_IOCTL,  4, struct sdi_ioctl_refclk)
#define SDI_IOCTL_VCXO        _IOW(SDI_IOCTL,   5, struct sdi_ioctl_vcxo)
#define SDI_IOCTL_SI5324_VCXO _IOW(SDI_IOCTL,   6, struct sdi_ioctl_si5324_vcxo)
#define SDI_IOCTL_SI5324_SPI  _IOWR(SDI_IOCTL,  6, struct sdi_ioctl_si5324_spi)
#define SDI_IOCTL_FAN         _IOW(SDI_IOCTL,   7, struct sdi_ioctl_fan)
#define SDI_IOCTL_DMA         _IOW(SDI_IOCTL,   8, struct sdi_ioctl_dma)
#define SDI_IOCTL_DMA_WRITER  _IOWR(SDI_IOCTL,  9, struct sdi_ioctl_dma_writer)
#define SDI_IOCTL_DMA_READER  _IOWR(SDI_IOCTL, 10, struct sdi_ioctl_dma_reader)

#define SDI_IOCTL_PATTERN     _IOW(SDI_IOCTL,  11, struct sdi_ioctl_pattern)
#define SDI_IOCTL_RX_SPI_CS   _IOW(SDI_IOCTL,  12, struct sdi_ioctl_rx_spi_cs)
#define SDI_IOCTL_RX_SPI      _IOWR(SDI_IOCTL, 13, struct sdi_ioctl_rx_spi)
#define SDI_IOCTL_RX          _IOWR(SDI_IOCTL, 14, struct sdi_ioctl_rx)
#define SDI_IOCTL_TX_SPI_CS   _IOW(SDI_IOCTL,  15, struct sdi_ioctl_rx_spi_cs)
#define SDI_IOCTL_TX_SPI      _IOWR(SDI_IOCTL, 16, struct sdi_ioctl_tx_spi)
#define SDI_IOCTL_TX          _IOWR(SDI_IOCTL, 17, struct sdi_ioctl_tx)


#endif /* _LINUX_SDI_H */
