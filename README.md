# MIA (Multifunction Interface Adapter)

A Raspberry Pi Pico 2 W-based system that provides multiple critical functions for the Clementina 6502 computer:

## Registers

The MIA has 32 internal registers:

| Register | Clementina  | Description            |
|----------|-------------|------------------------|
| 00       | FFE0        | IDX A Data port        |
| 01       | FFE1        | IDX A Selection        |
| 02       | FFE2        | CFG Data port          |
| 03       | FFE3        | CFG Load               |
| 04       | FFE4        | IDX B Data Port        |
| 05       | FFE5        | IDX B Selection        |
| 06       | FFE6        | CMD parameter 1        |
| 07       | FFE7        | CMD parameter 2        |
| 08       | FFE8        | CMD parameter 3        |
| 09       | FFE9        | Trigger Specified CMD  |
| 0A       | FFEA        | Error LSB              |
| 0B       | FFEB        | Error MSB              |
| 0C       | FFEC        | Status LSB             |
| 0D       | FFED        | Status MSB             |
| 0E       | FFEE        | IRQ Mask LSB           |
| 0F       | FFEF        | IRQ Mask MSB           |
| 10       | FFF0        | IRQ Status LSB         |
| 11       | FFF1        | IRQ Status MSB         |
| 12 - 19  | FFF2 - FFF9 | Reserved               |
| 1A       | FFFA        | NMI vector LSB         |
| 1B       | FFFB        | NMI vector MSB         |
| 1C       | FFFC        | RESET vector LSB       |
| 1D       | FFFD        | RESET vector MSB       |
| 1E       | FFFE        | IRQ / BRK vector LSB   |
| 1F       | FFFF        | IRQ / BRK vector MSB   |

## Configuration registers table

| #       | CFG Index   | Description                                                         |
|---------|-------------|---------------------------------------------------------------------|
| 00      | IDXA_ADDR_L | Low byte to where the IDX A is pointing to in the MIA memory        |
| 01      | IDXA_ADDR_M | Middle byte to where the IDX A is pointing to in the MIA memory     |
| 02      | IDXA_ADDR_H | High byte to where the IDX A is pointing to in the MIA memory       |
| 03      | IDXA_DEF_L  | Low byte of the default address for IDX A                           |
| 04      | IDXA_DEF_M  | Middle byte of the default address for IDX A                        |
| 05      | IDXA_DEF_H  | High byte of the default address for IDX A                          |
| 06      | IDXA_LIM_L  | Low byte of the limit address for IDX A                             |
| 07      | IDXA_LIM_M  | Middle byte of the limit address for IDX A                          |
| 08      | IDXA_LIM_H  | High byte of the limit address for IDX A                            |
| 09      | IDXA_STP_L  | LSB of the signed 2 byte step for IDX A                             |
| 0A      | IDXA_STP_M  | MSB of the signed 2 byte step for IDX A                             |
| 0B      | IDXA_FLAGS  | IDX A flags                                                         |
| 0C - 0F | Reserved    | Reserved for future IDX A capabilities                              |
| 10      | IDXB_ADDR_L | Low byte to where the IDX B is pointing to in the MIA memory        |
| 11      | IDXB_ADDR_M | Middle byte to where the IDX B is pointing to in the MIA memory     |
| 12      | IDXB_ADDR_H | High byte to where the IDX B is pointing to in the MIA memory       |
| 13      | IDXB_DEF_L  | Low byte of the default address for IDX B                           |
| 14      | IDXB_DEF_M  | Middle byte of the default address for IDX B                        |
| 15      | IDXB_DEF_H  | High byte of the default address for IDX B                          |
| 16      | IDXB_LIM_L  | Low byte of the limit address for IDX B                             |
| 17      | IDXB_LIM_M  | Middle byte of the limit address for IDX B                          |
| 18      | IDXB_LIM_H  | High byte of the limit address for IDX B                            |
| 19      | IDXB_STP_L  | LSB of the signed 2 byte step for IDX B                             |
| 1A      | IDXB_STP_M  | MSB of the signed 2 byte step for IDX B                             |
| 1B      | IDXB_FLAGS  | IDX B flags                                                         |
| 1C - 1F | Reserved    | Reserved for future IDX B capabilities                              |
| 20      | SPEED_L     | Low byte of the clock speed in Mhz                                  |
| 21      | SPEED_M     | Mid byte of the clock speed in Mhz                                  |
| 22      | SPEED_H     | High byte of the clock speed in Mhz                                 |

## Index Flags

| Bit | Name         | Description                                                                                          |
|-----|--------------|------------------------------------------------------------------------------------------------------|
| 0   | R_STP_ENA    | When set IDX_ADDR will be change by IDX_STP when the IDX data port is READ                           |
| 1   | W_STP_ENA    | When set IDX_ADDR will be change by IDX_STP when the IDX data port is WRITTEN                        |
| 2   | W_STP_DIR    | Sets the index step direction (0 = forward, 1 = backward)                                            |
| 3   | WRAP_DISABLE | By default IDX_ADDR will jump to IDX_DEF after reaching IDX_LIM, set this bit to disable             |
| 4   | WRAP_IRQ     | When set wrapping of the active index will trigger interrupt (provided global mask is enabled)       |

## IRQ Status

| Bit | Name             | Description                               |
|-----|------------------|-------------------------------------------|
| 0   | IRQ_IDXA_WRAPPED | IDX A has wrapped                         |
| 1   | IRQ_IDXB_WRAPPED | IDX B has wrapped                         |
| 2   | IRQ_COMMAND      | COMMAND execution triggered interrupt     |
