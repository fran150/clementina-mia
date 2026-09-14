# MIA SD and FAT Filesystem Subsystem

This document defines MIA's SD-card and FAT filesystem interface. The
programmer-facing workflow and examples are in
[sd-programmer-guide.md](sd-programmer-guide.md).

## Overview

MIA exposes an SD card as an intelligent storage device. The 6502 does not
implement SPI, SD card initialization, FAT directory traversal, or cluster-chain
I/O. Instead, a program writes request fields into MIA RAM, issues a command,
waits for completion, and streams result bytes through MIA's indexed windows.

The first implementation provides:

| Feature | Value |
| --- | --- |
| Physical transport | SPI mode SD card |
| Sector size | 512 bytes |
| Filesystem | FAT via FatFs |
| File API | read and write |
| File management | stat, mkdir, delete, rename, free-space query |
| Whole-file jobs | chunked load to MIA RAM, chunked save from MIA RAM |
| Raw block API | read and write single 512-byte sectors |
| Open files | up to 16 concurrent file handles (`SD_HANDLE_SELECT`) |
| Open directories | one implicit directory cursor |
| Path encoding | ASCII/CP437-safe paths recommended |

The filesystem API is intentionally small. It is meant for loading programs,
assets, data files, saved games, and configuration files from a normal
FAT-formatted SD card. Raw sector access is available for diagnostics, custom
disk images, and future native filesystems.

## Hardware Pins

The default SD pin mapping uses `SPI0`:

| Signal | Default GPIO | Notes |
| --- | ---: | --- |
| MISO | 0 | SD DO |
| CS | 1 | SD CS, active low |
| SCK | 2 | SD CLK |
| MOSI | 3 | SD DI |

These defaults can be overridden at build time:

| Define | Description |
| --- | --- |
| `MIA_SD_SPI_INSTANCE` | `0` for `spi0`, `1` for `spi1`. |
| `MIA_SD_MISO_PIN` | MISO GPIO. |
| `MIA_SD_CS_PIN` | chip-select GPIO. |
| `MIA_SD_SCK_PIN` | SCK GPIO. |
| `MIA_SD_MOSI_PIN` | MOSI GPIO. |
| `MIA_SD_SPI_FAST_BAUD` | post-initialization SPI clock. Default `12000000`. |
| `MIA_SD_SERVICE_BUDGET_US` | Core 0 SD job service budget per call. Default `1000`. |

Example:

```sh
MIA_SD_MISO_PIN=16 MIA_SD_CS_PIN=17 MIA_SD_SCK_PIN=18 MIA_SD_MOSI_PIN=19 make build
```

The SD card socket or module must be 3.3 V compatible. Do not connect a 5 V SD
module directly to Pico GPIOs unless it has appropriate level shifting.

MIA does not currently use dedicated card-detect or write-protect GPIOs. The
`SD_STATUS_PRESENT` and `MIA_STAT_SD_PRESENT` bits mean that a card answered SD
initialization successfully; they are not live socket-switch state.

## Memory Map

SD and filesystem state lives in MIA RAM at `$13000-$13BFF`. It is outside the
syncable video region, so normal SD/FS control writes do not dirty video pages.

| Range | Size | Description |
| ---: | ---: | --- |
| `$13000-$1303F` | 64 | SD/FS control block |
| `$13040-$1323F` | 512 | raw sector buffer |
| `$13240-$1333F` | 256 | path buffer |
| `$13340-$1343F` | 256 | directory entry/result buffer |
| `$13440-$13BFF` | 1984 | file transfer buffer |

The secondary path buffer used by `FS_RENAME` overlays `$13440-$1353F`, the
first 256 bytes of the file transfer buffer.

`FS_LOAD_TO_MIA_RAM` can write into any MIA RAM destination. If the destination
overlaps the syncable video region, MIA marks the affected video pages dirty.
Bulk loads into the audio register block update RAM but do not queue live audio
register changes; stop and re-enable audio to resynchronize the audio engine.

## Control Block

Offsets in this table are relative to `$13000`.

| Offset | Name | Access | Description |
| ---: | --- | --- | --- |
| `$00` | `SD_VERSION` | read | SD/FS memory layout version. Current value is `6`. |
| `$01` | `SD_STATUS` | read | SD/FS status flags. |
| `$02` | `SD_LAST_ERROR` | read | Last MIA SD/FS error code, or zero. |
| `$03` | `SD_CARD_TYPE` | read | Card type code. |
| `$04-$07` | `SD_LBA` | read/write | Little-endian sector address for raw sector commands. |
| `$08-$09` | `SD_REQUEST_LEN` | read/write | Requested byte count for `FS_READ`/`FS_WRITE`, or max load length for `FS_LOAD_TO_MIA_RAM`. |
| `$0A-$0B` | `SD_RESULT_LEN` | read | Bytes produced by the last transfer. |
| `$0C-$0E` | `SD_DEST_ADDR` | read/write | 24-bit MIA RAM destination for `FS_LOAD_TO_MIA_RAM`. |
| `$0F` | `SD_FILE_HANDLE` | read | `1` when the slot named by `SD_HANDLE_SELECT` is open, otherwise `0`. |
| `$10` | `SD_OPEN_MODE` | read/write | File open policy for `FS_OPEN`, applied to the selected slot. |
| `$11` | `SD_EOF` | read | Nonzero when the selected slot's open file is at EOF; falls back to the last directory-read/load-save-job EOF when the selected slot has no file open. See [File Handles](#file-handles). |
| `$12` | `SD_FATFS_RESULT` | read | Raw FatFs `FRESULT` code from the last filesystem operation. |
| `$13` | `SD_FLAGS` | reserved | Write zero. |
| `$14-$17` | `SD_CARD_SECTORS` | read | Little-endian card capacity in 512-byte sectors, when known. |
| `$18-$1B` | `SD_FILE_SIZE` | read | Size of the selected slot's open file. Refreshed by `FS_OPEN`/`FS_READ`/`FS_WRITE`/`FS_SYNC`/`FS_SEEK`/`FS_CLOSE`/`FS_FILE_INFO` on that slot, not by other commands. |
| `$1C-$1F` | `SD_FILE_POS` | read/write | Selected slot's file position. Write before `FS_SEEK` to choose the target offset. After `FS_LOAD_TO_MIA_RAM`, this contains the full 32-bit loaded byte count (the load/save jobs use their own file, independent of any slot - see [File Handles](#file-handles)). |
| `$20-$23` | `SD_FREE_CLUSTERS` | read | Free FAT clusters after `FS_GET_FREE`. |
| `$24-$27` | `SD_TOTAL_CLUSTERS` | read | Total usable FAT clusters after `FS_GET_FREE`. |
| `$28-$29` | `SD_CLUSTER_SECTORS` | read | Sectors per FAT cluster after `FS_GET_FREE`. |
| `$2A-$2D` | `SD_TRANSFER_LEN` | read/write | 32-bit byte count for `FS_SAVE_FROM_MIA_RAM`. |
| `$2E` | `SD_HANDLE_SELECT` | read/write | File-handle slot (`0`-`15`) that `FS_OPEN`/`FS_READ`/`FS_WRITE`/`FS_SEEK`/`FS_SYNC`/`FS_CLOSE` act on. Defaults to `0` on reset. See [File Handles](#file-handles). |
| `$2F-$3F` | reserved | reserved | Write zero. |

`SD_OPEN_MODE` values:

| Value | Name | FatFs policy | Use |
| ---: | --- | --- | --- |
| `$00` | `FS_OPEN_READ` | `FA_READ` | Open an existing file for reading. |
| `$01` | `FS_OPEN_WRITE_CREATE` | `FA_WRITE | FA_CREATE_ALWAYS` | Create or truncate a file for saving. |
| `$02` | `FS_OPEN_WRITE_APPEND` | `FA_WRITE | FA_OPEN_APPEND` | Create if missing and append at EOF. |
| `$03` | `FS_OPEN_READ_WRITE` | `FA_READ | FA_WRITE | FA_OPEN_ALWAYS` | Open or create a file for in-place updates. |

`SD_STATUS` flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | `SD_STATUS_PRESENT` | A card initialized successfully. |
| 1 | `SD_STATUS_INITIALIZED` | The SD block driver is initialized. |
| 2 | `SD_STATUS_MOUNTED` | The FAT filesystem is mounted. |
| 3 | `SD_STATUS_BUSY` | An SD/FS command is in progress. |
| 4 | `SD_STATUS_FILE_OPEN` | The slot named by `SD_HANDLE_SELECT` is open. |
| 5 | `SD_STATUS_DIR_OPEN` | The implicit directory cursor is open. |
| 6 | `SD_STATUS_EOF` | The selected slot's open file is at EOF (or, with no file open on that slot, the last directory-read/load-save-job EOF). |
| 7 | `SD_STATUS_ERROR` | `SD_LAST_ERROR` is nonzero. |

`SD_CARD_TYPE` values:

| Value | Name | Meaning |
| ---: | --- | --- |
| `$00` | none | No initialized card. |
| `$01` | SD v1 | Standard-capacity SD v1 card. |
| `$02` | SD v2 | Standard-capacity SD v2 card. |
| `$03` | SDHC/SDXC | High-capacity card using block addressing. |

## File Handles

MIA holds up to **16 file-handle slots** (`SD_HANDLE_SELECT` values `0`-`15`),
each with its own open/mode/size/position/EOF state - independent SD/FS
commands can have several files open at once. There is no allocator: a program
picks which slot to use for a given file, the same way it already picks a
BASIC/application-level file number. `FS_OPEN` on a slot that is already open
fails with `ERROR_FS_HANDLE_ALREADY_OPEN` rather than silently replacing it;
`SD_HANDLE_SELECT` outside `0`-`15` fails with `ERROR_FS_INVALID_HANDLE`.

Select a slot before any of `FS_OPEN`/`FS_READ`/`FS_WRITE`/`FS_SEEK`/`FS_SYNC`/
`FS_CLOSE`:

```asm
lda #IIDX_SD_CONTROL
sta IDXA_SELECT
ldx #SD_HANDLE_SELECT
@skip: lda IDXA_PORT : dex : bne @skip
lda #2          ; use slot 2
sta IDXA_PORT
```

`SD_HANDLE_SELECT` defaults to `0` on reset, so code that never touches it sees
exactly the single-handle behavior of protocol versions before `5`.

`SD_FILE_HANDLE`, `SD_EOF`, and the `SD_STATUS_FILE_OPEN`/`SD_STATUS_EOF` bits
always describe whichever slot is currently selected, refreshed whenever any
SD/FS command completes (even one against a different slot) - so switching
`SD_HANDLE_SELECT` and then issuing any command is enough to see the newly
selected slot's own open/EOF state, not a stale value left over from whichever
slot a previous command touched. `SD_FILE_SIZE`/`SD_FILE_POS`, by contrast,
only update when a command actually runs *against* the selected slot
(`FS_OPEN`/`FS_READ`/`FS_WRITE`/`FS_SYNC`/`FS_SEEK`/`FS_CLOSE`/`FS_FILE_INFO`) - the same rule
that applied to the single implicit handle before protocol version `5`.

`FS_CLOSE` closes only the selected slot. It no longer also closes the
directory cursor (a side effect of the old single-handle design): closing one
file never disturbs an in-progress `FS_OPENDIR`/`FS_READDIR` listing.

The directory cursor and the whole-file `FS_LOAD_TO_MIA_RAM`/
`FS_SAVE_FROM_MIA_RAM` jobs are unaffected by `SD_HANDLE_SELECT` - both stay
singular/self-contained, exactly as before. A load/save job never collides
with an explicitly open handle: it opens, transfers, and closes its own file
internally.

## Directory Entry Buffer

`FS_READDIR` writes one directory entry to `$13340-$1343F`. A zero name length
and `SD_EOF != 0` means the directory is exhausted.

Offsets in this table are relative to `$13340`.

| Offset | Name | Description |
| ---: | --- | --- |
| `$00` | `DIR_ATTR` | FAT attribute byte. |
| `$01` | `DIR_NAME_LEN` | Length of the returned name. |
| `$02-$03` | reserved | Reserved. |
| `$04-$07` | `DIR_SIZE` | Little-endian file size. Zero for directories. |
| `$08-$09` | `DIR_DATE` | FAT packed modification date. |
| `$0A-$0B` | `DIR_TIME` | FAT packed modification time. |
| `$0C-$FF` | `DIR_NAME` | Null-terminated name bytes. |

Common FAT attribute bits:

| Bit | Mask | Meaning |
| ---: | ---: | --- |
| 0 | `$01` | read-only |
| 1 | `$02` | hidden |
| 2 | `$04` | system |
| 3 | `$08` | volume label |
| 4 | `$10` | directory |
| 5 | `$20` | archive |

## Indexes

MIA configures fixed indexes for SD/FS during runtime reset:

| Index | Range | Description |
| ---: | ---: | --- |
| `$E0` | `$13000-$1303F` | SD/FS control block. |
| `$E1` | `$13040-$1323F` | Raw 512-byte sector buffer. |
| `$E2` | `$13240-$1333F` | Path buffer. |
| `$E3` | `$13340-$1343F` | Directory entry/result buffer. |
| `$E4` | `$13440-$13BFF` | File transfer buffer. |
| `$E5` | `$13440-$1353F` | Secondary path buffer, overlaid on the first 256 bytes of the transfer buffer. |

All SD/FS indexes step on reads and writes and wrap within their configured
range.

## Commands

SD and filesystem commands use the normal MIA command registers. Commands are
asynchronous from the 6502 perspective:

1. The command write is accepted through `CMD_TRIGGER`.
2. MIA sets `MIA_STAT_SD_BUSY`.
3. Core 0 performs the SD/FAT work from `mia_sd_service()`.
4. MIA clears `MIA_STAT_SD_BUSY`.
5. MIA raises `IRQ_SD_DONE` on success or `IRQ_SD_ERROR` on failure.
6. Filesystem commands also raise `IRQ_FS_EVENT`.

The existing `IRQ_COMMAND` still reports that the command request was accepted
by the command dispatcher; it is not the SD/FS completion event.

| Command | Id | Parameters | Description |
| --- | ---: | --- | --- |
| `SD_INIT` | `$70` | none | Initialize the physical SD card. Updates card type and capacity. |
| `SD_READ_SECTOR` | `$71` | `SD_LBA` | Read one 512-byte sector into the sector buffer. |
| `SD_WRITE_SECTOR` | `$72` | `SD_LBA`, sector buffer | Write one 512-byte sector from the sector buffer. Advanced use only. |
| `SD_GET_INFO` | `$73` | none | Refresh/read current SD status fields. |
| `FS_MOUNT` | `$78` | none | Mount the FAT filesystem. Also initializes the SD card if needed. |
| `FS_OPENDIR` | `$79` | path buffer | Open a directory cursor. |
| `FS_READDIR` | `$7A` | none | Read the next directory entry into the directory buffer. |
| `FS_OPEN` | `$7B` | `SD_HANDLE_SELECT`, path buffer, `SD_OPEN_MODE` | Open one file on the selected slot for reading and/or writing. Fails with `ERROR_FS_HANDLE_ALREADY_OPEN` if that slot is already open. |
| `FS_READ` | `$7C` | `SD_HANDLE_SELECT`, `SD_REQUEST_LEN` | Read up to `SD_REQUEST_LEN` bytes from the selected slot into the transfer buffer. |
| `FS_CLOSE` | `$7D` | `SD_HANDLE_SELECT` | Close the selected slot. Does not touch the directory cursor. |
| `FS_LOAD_TO_MIA_RAM` | `$7E` | path buffer, `SD_DEST_ADDR`, `SD_REQUEST_LEN` | Open a file, load it into MIA RAM, then close it. Uses its own file, independent of any `SD_HANDLE_SELECT` slot. |
| `FS_WRITE` | `$7F` | `SD_HANDLE_SELECT`, `SD_REQUEST_LEN`, transfer buffer | Write up to `SD_REQUEST_LEN` bytes from the transfer buffer to the selected slot's open file. |
| `FS_SYNC` | `$80` | `SD_HANDLE_SELECT` | Flush the selected slot's dirty FAT/data sectors to the card. |
| `FS_SEEK` | `$81` | `SD_HANDLE_SELECT`, `SD_FILE_POS` | Seek the selected slot's open file to `SD_FILE_POS`. |
| `FS_STAT` | `$82` | path buffer | Fill the directory entry buffer with metadata for one file or directory. |
| `FS_MKDIR` | `$83` | path buffer | Create one directory. Parent directories must already exist. |
| `FS_DELETE` | `$84` | path buffer | Delete one file or empty directory. |
| `FS_RENAME` | `$85` | path buffer, secondary path buffer | Rename or move one file or directory. |
| `FS_GET_FREE` | `$86` | none | Update free-space fields in the control block. |
| `FS_SAVE_FROM_MIA_RAM` | `$87` | path buffer, `SD_DEST_ADDR`, `SD_TRANSFER_LEN`, `SD_OPEN_MODE` | Open a file, save bytes from MIA RAM, then close it. |
| `FS_CHDIR` | `$88` | path buffer | Change the current directory (`f_chdir`). Persists, per mounted volume, until the next `FS_MOUNT` - see the path buffer note below. |

For `FS_READ` and `FS_WRITE`, a `SD_REQUEST_LEN` of zero means the full transfer
buffer size (`1984` bytes). `SD_RESULT_LEN` contains the actual byte count read
or written. For `FS_WRITE`, a successful command means the requested byte count
was accepted by FatFs; call `FS_SYNC` or `FS_CLOSE` when the data must be forced
to the card before continuing.

For `FS_LOAD_TO_MIA_RAM`, a `SD_REQUEST_LEN` of zero means load until EOF or
until the destination reaches the end of MIA RAM. `SD_RESULT_LEN` is 16-bit and
contains the low 16 bits of the loaded byte count; `SD_FILE_POS` contains the
full 32-bit loaded byte count. The load runs as an internal 512-byte chunked job,
so core 0 returns to the normal service loop between chunks.

For `FS_SAVE_FROM_MIA_RAM`, `SD_DEST_ADDR` is the MIA RAM source address,
`SD_TRANSFER_LEN` is the full 32-bit byte count to save, and `SD_OPEN_MODE`
selects the write policy. `FS_OPEN_READ` is rejected. `SD_RESULT_LEN` contains
the low 16 bits of the saved byte count; `SD_FILE_POS` contains the full 32-bit
saved byte count. The save runs as an internal 512-byte chunked job and closes
the file when complete.

For `FS_SEEK`, write the 32-bit target offset to `SD_FILE_POS`, trigger the
command, then read `SD_FILE_POS` again. FatFs may clamp or adjust the final
position depending on the open mode and filesystem result.

`FS_STAT` uses the same directory entry/result buffer as `FS_READDIR`.
`FS_RENAME` reads the old path from `$E2`/`IIDX_FS_PATH` and the new path from
`$E5`/`IIDX_FS_PATH2`. Because path2 overlays the start of the transfer buffer,
do not expect the first 256 transfer bytes to survive a rename request.

After `FS_GET_FREE`, free space in bytes is:

```text
SD_FREE_CLUSTERS * SD_CLUSTER_SECTORS * 512
```

The total usable filesystem size is:

```text
SD_TOTAL_CLUSTERS * SD_CLUSTER_SECTORS * 512
```

## Status and IRQ Bits

Global `MIA_STATUS` adds:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 9 | `MIA_STAT_SD_PRESENT` | SD card initialized successfully. This is not physical socket-detect state. |
| 10 | `MIA_STAT_SD_BUSY` | SD/FS command is in progress. |
| 11 | `MIA_STAT_FS_MOUNTED` | FAT filesystem is mounted. |

Global `IRQ_STATUS` adds:

| Bit | Name | Meaning |
| ---: | --- | --- |
| 11 | `IRQ_SD_DONE` | An SD/FS command completed successfully. |
| 12 | `IRQ_SD_ERROR` | An SD/FS command failed; read `ERROR_L` and the control block. |
| 13 | `IRQ_FS_EVENT` | A filesystem command completed. |

As with all MIA IRQ sources, enable bits in `IRQ_MASK` decide whether the
physical `IRQB` line asserts. Reading `IRQ_STATUS_L` at `$FFF0` clears all
latched IRQ sources.

## Errors

SD/FS failures are reported through the normal MIA error queue and mirrored in
`SD_LAST_ERROR`.

| Code | Name | Meaning |
| ---: | --- | --- |
| `$70` | `ERROR_SD_BUSY` | A new request was made while SD/FS was busy. |
| `$71` | `ERROR_SD_INIT_FAILED` | SD card initialization failed. |
| `$72` | `ERROR_SD_NOT_READY` | A raw sector command ran before initialization. |
| `$73` | `ERROR_SD_READ_FAILED` | Raw sector read failed. |
| `$74` | `ERROR_SD_WRITE_FAILED` | Raw sector write failed. |
| `$78` | `ERROR_FS_MOUNT_FAILED` | FAT mount failed. |
| `$79` | `ERROR_FS_OPEN_FAILED` | File open failed. |
| `$7A` | `ERROR_FS_READ_FAILED` | File read or load failed. |
| `$7B` | `ERROR_FS_CLOSE_FAILED` | File close failed. |
| `$7C` | `ERROR_FS_DIR_FAILED` | Directory operation failed. |
| `$7D` | `ERROR_FS_INVALID_REQUEST` | Request parameters are invalid. |
| `$7E` | `ERROR_FS_NO_FILE_OPEN` | File I/O was requested without an open file. |
| `$7F` | `ERROR_FS_WRITE_FAILED` | File write failed or wrote fewer bytes than requested. |
| `$80` | `ERROR_FS_SEEK_FAILED` | File seek failed. |
| `$81` | `ERROR_FS_SYNC_FAILED` | File sync failed. |
| `$82` | `ERROR_FS_STAT_FAILED` | File stat failed. |
| `$83` | `ERROR_FS_MKDIR_FAILED` | Directory creation failed. |
| `$84` | `ERROR_FS_DELETE_FAILED` | File or directory delete failed. |
| `$85` | `ERROR_FS_RENAME_FAILED` | File or directory rename failed. |
| `$86` | `ERROR_FS_FREE_FAILED` | Free-space query failed. |
| `$87` | `ERROR_FS_HANDLE_ALREADY_OPEN` | `FS_OPEN` was requested on a slot that is already open. |
| `$88` | `ERROR_FS_INVALID_HANDLE` | `SD_HANDLE_SELECT` names a slot outside `0`-`15`. |

For filesystem errors, `SD_FATFS_RESULT` contains the raw FatFs result code for
more detailed diagnosis.

## Terminal Diagnostics

The USB terminal exposes:

| Command | Description |
| --- | --- |
| `status sd` | Detailed SD/FS status, pins, memory ranges, indexes, and last result. |
| `sd status` | Same as `status sd`. |
| `sd init` | Request physical SD card initialization. |
| `sd mount` | Request FAT mount. |
| `errors list` | Show queued MIA error codes. |

## Limitations

- The file API supports open/read/write/sync/seek/close, stat, mkdir, delete,
  rename, and free-space query. Formatting is not exposed.
- Whole-file load/save jobs process 512-byte chunks and return to the core 0
  service loop between chunks. Individual SD card operations can still block for
  card-internal erase/program latency.
- Raw sector write exists for advanced tools, but it bypasses FAT consistency
  checks at the MIA API level.
- Up to 16 concurrent file-handle slots are exposed (`SD_HANDLE_SELECT`), but
  only one implicit directory cursor. There is no allocator: a program picks
  which slot to use, same as picking a BASIC/application-level file number.
- No physical card-detect or write-protect GPIO is wired into MIA yet. Present
  means "initialization succeeded."
- FAT long filename support comes from the bundled FatFs configuration. Keep
  filenames ASCII/CP437-safe for predictable 6502 programs.
- `FS_SYNC`, `FS_CLOSE`, metadata updates, and raw sector writes may still block
  core 0 while the card commits data internally.

## Protocol version 6: selected-file information

FS_FILE_INFO ($89) reads SD_HANDLE_SELECT and refreshes the existing
32-bit SD_FILE_SIZE and SD_FILE_POS fields for that open slot. It uses
the normal asynchronous SD request/completion path. Invalid slots and closed
handles report the existing invalid-handle/no-file-open errors. Results must
only be consumed after successful completion.

This query does not flush, move the position, or change EOF state. It accesses
the open FatFs object on Core 0; the Core 1 bus-service loop is unchanged.
Version 6 retains all existing command IDs and buffer layouts.
