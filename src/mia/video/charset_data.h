/**
 * Charset Data Interface
 * External declarations for the charset image generated from a
 * charsets/<name>.bin file at build time (selected via the MIA_CHARSET option).
 *
 * Layout: a sequence of 2048-byte blocks (256 glyphs each) in MIA pixel order.
 * The loader split-loads it -- block i goes into plane 0 of CHR bank i -- so a
 * 512-glyph charset fills plane 0 of banks 0 and 1, selectable per cell via the
 * CHR_ALT attribute. Block 0 (bank 0 plane 0) is the ASCII text set; block 1
 * (bank 1 plane 0) is the alternate graphics set. mia_charset_size holds the
 * actual length.
 */

#ifndef CHARSET_DATA_H
#define CHARSET_DATA_H

#include <stdint.h>
#include <stddef.h>

extern const uint8_t mia_charset[];
extern const size_t mia_charset_size;

#endif // CHARSET_DATA_H
