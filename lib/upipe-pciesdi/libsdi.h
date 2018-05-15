/*
 * PCIeSDI library
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

#ifndef SDI_LIB_H
#define SDI_LIB_H

int64_t get_time_ms(void);

/* ioctl */

uint32_t sdi_readl(int fd, uint32_t addr);
void sdi_writel(int fd, uint32_t addr, uint32_t val);
void sdi_vcxo(int fd, uint32_t width, uint32_t period);
void sdi_si5324_vcxo(int fd, uint32_t width, uint32_t period);
void sdi_si5324_spi(int fd, uint32_t tx_data, uint32_t *rx_data);
void sdi_refclk(int fd, uint8_t refclk_sel, uint32_t *refclk0_freq, uint32_t *refclk1_freq);
void sdi_dma_loopback(int fd, uint8_t enable);
void sdi_dma_reader(int fd, uint8_t enable, int64_t *hw_count, int64_t *sw_count);
void sdi_dma_writer(int fd, uint8_t enable, int64_t *hw_count, int64_t *sw_count);
void sdi_set_pattern(int fd, uint8_t enable, uint8_t format);
void sdi_rx_spi_cs(int fd, uint8_t cs_n);
void sdi_rx_spi(int fd, uint32_t tx_data, uint32_t *rx_data);
void sdi_rx(int fd, uint8_t *locked, uint8_t *mode, uint8_t *family, uint8_t *rate);
void sdi_tx_spi_cs(int fd, uint8_t cs_n);
void sdi_tx_spi(int fd, uint32_t tx_data, uint32_t *rx_data);
void sdi_tx(int fd, uint8_t *txen, uint8_t *slew);

/* flash */

#define FLASH_READ_ID 0x9E
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
void sdi_flash_write(int fd,
                     const uint8_t *buf, uint32_t base, uint32_t size,
                     void (*progress_cb)(void *opaque, const char *fmt, ...),
                     void *opaque);

/* spi */

void rx_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data);
uint16_t rx_spi_read(int fd, uint8_t channel, uint16_t adr);
void tx_spi_write(int fd, uint8_t channel, uint16_t adr, uint16_t data);
uint16_t tx_spi_read(int fd, uint8_t channel, uint16_t adr);
void si5324_spi_write(int fd, uint8_t adr, uint8_t data);
uint8_t si5324_spi_read(int fd, uint16_t adr);

#endif /* SDI_LIB_H */
