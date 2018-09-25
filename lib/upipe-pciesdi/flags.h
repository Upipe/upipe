#ifndef __HW_FLAGS_H
#define __HW_FLAGS_H

/* xo */
#define XO_148P50MHZ_SEL 0
#define XO_148P35MHZ_SEL 1

/* refclk */
#define REFCLK0_SEL (1<<0)
#define REFCLK1_SEL (1<<1)

/* spi */
#define SPI_CTRL_START 0x1
#define SPI_CTRL_LENGTH (1<<8)
#define SPI_STATUS_DONE 0x1

/* pcie */
#define DMA_TABLE_LOOP_INDEX (1 << 0)
#define DMA_TABLE_LOOP_COUNT (1 << 16)

/* sdi */

#define SDI_TX_MODE_HD 0b00
#define SDI_TX_MODE_SD 0b01
#define SDI_TX_MODE_3G 0b10

#define SDI_LOOPBACK_NEAR_END_PCS 0b001
#define SDI_LOOPBACK_NEAR_END_PMA 0b010
#define SDI_LOOPBACK_FAR_END_PMA  0b100
#define SDI_LOOPBACK_FAR_END_PCS  0b110

#endif /* __HW_FLAGS_H */
