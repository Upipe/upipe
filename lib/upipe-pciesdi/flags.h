#ifndef __HW_FLAGS_H
#define __HW_FLAGS_H

/* refclk */
#define REFCLK0_SEL = (1<<0)
#define REFCLK1_SEL = (1<<1)

/* spi */
#define SPI_CTRL_START 0x1
#define SPI_CTRL_LENGTH (1<<8)
#define SPI_STATUS_DONE 0x1

/* pcie */
#define DMA_TABLE_LOOP_INDEX (1 << 0)
#define DMA_TABLE_LOOP_COUNT (1 << 16)

#endif /* __HW_FLAGS_H */
