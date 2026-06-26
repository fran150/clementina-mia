/**
 * Palette Data Interface
 * External declarations for the default video palette generated from a
 * palettes/<name>.palette.bin file at build time (selected via MIA_PALETTE).
 *
 * Layout: 16 palette banks * 8 little-endian RGB565 colors = 256 bytes, matching
 * the tile editor Palette (.bin) export. The video subsystem copies it verbatim
 * into MIA palette RAM at startup.
 */

#ifndef PALETTE_DATA_H
#define PALETTE_DATA_H

#include <stdint.h>
#include <stddef.h>

extern const uint8_t mia_palette[];
extern const size_t mia_palette_size;

#endif // PALETTE_DATA_H
