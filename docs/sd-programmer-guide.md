# MIA SD and FAT Programmer Guide

This guide describes how Clementina 6502 programs use MIA's SD-card and FAT
filesystem interface. The exact memory layout is defined in [sd.md](sd.md).

## Mental Model

Treat MIA like an intelligent disk controller:

1. Write request data into MIA RAM through an index.
2. Trigger an SD/FS command.
3. Wait until `MIA_STAT_SD_BUSY` clears, or enable SD/FS IRQs.
4. Read the control block and result buffers through indexes.

Normal programs should use the FAT file commands. Raw sector reads and writes
are useful for boot tools, disk image formats, and diagnostics.

The 6502 should not parse FAT. MIA handles SD SPI mode, card type detection,
mounting, directory traversal, file open, file read/write, and cluster-chain
I/O.

`MIA_STAT_SD_PRESENT` means that SD initialization succeeded. It is not a
physical card-detect switch.

## Constants

```asm
IDXA_PORT       = $FFE0
IDXA_SELECT     = $FFE1
IDXB_PORT       = $FFE4
IDXB_SELECT     = $FFE5
CMD_PARAM1      = $FFE6
CMD_PARAM2      = $FFE7
CMD_PARAM3      = $FFE8
CMD_TRIGGER     = $FFE9
STATUS_L        = $FFEA
STATUS_H        = $FFEB
ERROR_L         = $FFEC
IRQ_MASK_L      = $FFEE
IRQ_MASK_H      = $FFEF
IRQ_STATUS_L    = $FFF0
IRQ_STATUS_H    = $FFF1

CMD_SD_INIT         = $70
CMD_SD_READ_SECTOR  = $71
CMD_SD_WRITE_SECTOR = $72
CMD_SD_GET_INFO     = $73

CMD_FS_MOUNT        = $78
CMD_FS_OPENDIR      = $79
CMD_FS_READDIR      = $7A
CMD_FS_OPEN         = $7B
CMD_FS_READ         = $7C
CMD_FS_CLOSE        = $7D
CMD_FS_LOAD_MIA     = $7E
CMD_FS_WRITE        = $7F
CMD_FS_SYNC         = $80
CMD_FS_SEEK         = $81
CMD_FS_STAT         = $82
CMD_FS_MKDIR        = $83
CMD_FS_DELETE       = $84
CMD_FS_RENAME       = $85
CMD_FS_GET_FREE     = $86
CMD_FS_SAVE_MIA     = $87

IIDX_SD_CONTROL     = $E0
IIDX_SD_SECTOR      = $E1
IIDX_FS_PATH        = $E2
IIDX_FS_DIR_ENTRY   = $E3
IIDX_FS_TRANSFER    = $E4
IIDX_FS_PATH2       = $E5

FS_OPEN_READ         = $00
FS_OPEN_WRITE_CREATE = $01
FS_OPEN_WRITE_APPEND = $02
FS_OPEN_READ_WRITE   = $03
```

Status bits:

```asm
; MIA_STATUS low/high word
MIA_STAT_SD_PRESENT = $0200
MIA_STAT_SD_BUSY    = $0400
MIA_STAT_FS_MOUNTED = $0800
```

IRQ bits:

```asm
IRQ_SD_DONE  = $0800
IRQ_SD_ERROR = $1000
IRQ_FS_EVENT = $2000
```

Because these bits are in the high byte:

```asm
IRQ_MASK_H_SD_DONE  = $08
IRQ_MASK_H_SD_ERROR = $10
IRQ_MASK_H_FS_EVENT = $20
STATUS_H_SD_BUSY    = $04
```

Control block offsets, relative to `IIDX_SD_CONTROL`:

```asm
SD_VERSION        = $00
SD_STATUS         = $01
SD_LAST_ERROR     = $02
SD_CARD_TYPE      = $03
SD_LBA0           = $04
SD_LBA1           = $05
SD_LBA2           = $06
SD_LBA3           = $07
SD_REQUEST_LEN_L  = $08
SD_REQUEST_LEN_H  = $09
SD_RESULT_LEN_L   = $0A
SD_RESULT_LEN_H   = $0B
SD_DEST_ADDR_L    = $0C
SD_DEST_ADDR_M    = $0D
SD_DEST_ADDR_H    = $0E
SD_FILE_HANDLE    = $0F
SD_OPEN_MODE      = $10
SD_EOF            = $11
SD_FATFS_RESULT   = $12
SD_CARD_SECTORS0  = $14
SD_FILE_SIZE0     = $18
SD_FILE_POS0      = $1C
SD_FREE_CLUSTERS0 = $20
SD_TOTAL_CLUSTERS0 = $24
SD_CLUSTER_SECTORS_L = $28
SD_TRANSFER_LEN0  = $2A
```

Directory entry offsets, relative to `IIDX_FS_DIR_ENTRY`:

```asm
DIR_ATTR      = $00
DIR_NAME_LEN  = $01
DIR_SIZE0     = $04
DIR_DATE_L    = $08
DIR_DATE_H    = $09
DIR_TIME_L    = $0A
DIR_TIME_H    = $0B
DIR_NAME      = $0C

DIR_ATTR_DIRECTORY = $10
```

## Command Helper

All SD/FS commands ignore `CMD_PARAM1-3`; request fields live in the control
block. It is still good practice to clear the parameter registers for future
compatibility.

```asm
mia_cmd:
    stz CMD_PARAM1
    stz CMD_PARAM2
    stz CMD_PARAM3
    sta CMD_TRIGGER
    rts
```

## Polling For Completion

The simplest path is polling `MIA_STAT_SD_BUSY`. Since the SD busy bit is bit
10, test bit 2 of `STATUS_H`.

```asm
sd_wait:
    lda STATUS_H
    and #STATUS_H_SD_BUSY
    bne sd_wait
    rts
```

After `sd_wait`, read `SD_LAST_ERROR` from the control block. Zero means the
last SD/FS command succeeded.

```asm
sd_last_error:
    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    lda IDXA_PORT       ; SD_VERSION
    lda IDXA_PORT       ; SD_STATUS
    lda IDXA_PORT       ; SD_LAST_ERROR
    rts
```

## IRQ Completion

Programs that prefer interrupts can enable `IRQ_SD_DONE`, `IRQ_SD_ERROR`, and
optionally `IRQ_FS_EVENT`.

```asm
sd_enable_irqs:
    lda #IRQ_MASK_H_SD_DONE | IRQ_MASK_H_SD_ERROR | IRQ_MASK_H_FS_EVENT
    sta IRQ_MASK_H
    rts
```

In an IRQ handler, read `$FFF1` first if you need high-byte flags, then read
`$FFF0` to clear all pending MIA IRQ sources.

```asm
irq_handler:
    lda IRQ_STATUS_H
    sta irq_status_h_shadow
    lda IRQ_STATUS_L        ; clears all IRQ_STATUS bits

    lda irq_status_h_shadow
    and #IRQ_MASK_H_SD_ERROR
    bne handle_sd_error

    lda irq_status_h_shadow
    and #IRQ_MASK_H_SD_DONE
    bne handle_sd_done
    rti
```

## Mounting The Filesystem

`FS_MOUNT` initializes the SD card if needed and mounts the first FAT volume.

```asm
fs_mount:
    lda #CMD_FS_MOUNT
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts                 ; A = 0 on success
```

`SD_INIT` is available when you only want raw sectors. Normal file code can call
`FS_MOUNT` directly.

## Writing A Path

Paths are written as null-terminated strings into `IIDX_FS_PATH`.

```asm
fs_write_path:
    lda #IIDX_FS_PATH
    sta IDXA_SELECT
    ldy #$00
@loop:
    lda path,y
    sta IDXA_PORT
    beq @done
    iny
    bne @loop
@done:
    rts

path:
    .byte "/GAMES/DEMO.PRG",0
```

Use `/` as the path separator. Stick to ASCII filenames for predictable behavior
from 6502 software.

## Loading A Whole File To MIA RAM

`FS_LOAD_TO_MIA_RAM` opens the path, reads from the start of the file into MIA
RAM, and closes the file. MIA performs the load as a 512-byte chunked job; keep
waiting for `MIA_STAT_SD_BUSY` to clear before reading final results.

Set:

- `SD_DEST_ADDR` to the 24-bit MIA RAM destination.
- `SD_REQUEST_LEN` to the maximum bytes to load, or zero to load until EOF or
  the end of MIA RAM.

This example loads `/GAMES/DEMO.PRG` to MIA RAM address `$14000`.

```asm
fs_load_demo:
    jsr fs_mount
    bne @done

    jsr fs_write_path

    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_REQUEST_LEN_L.
    ldx #SD_REQUEST_LEN_L
@skip_req:
    lda IDXA_PORT
    dex
    bne @skip_req

    stz IDXA_PORT       ; SD_REQUEST_LEN_L = 0, load until EOF
    stz IDXA_PORT       ; SD_REQUEST_LEN_H = 0
    lda IDXA_PORT       ; SD_RESULT_LEN_L, preserve
    lda IDXA_PORT       ; SD_RESULT_LEN_H, preserve

    lda #$00
    sta IDXA_PORT       ; SD_DEST_ADDR_L
    lda #$40
    sta IDXA_PORT       ; SD_DEST_ADDR_M
    lda #$01
    sta IDXA_PORT       ; SD_DEST_ADDR_H = $014000

    lda #CMD_FS_LOAD_MIA
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
@done:
    rts
```

After completion:

- `SD_RESULT_LEN` contains the low 16 bits of the loaded byte count.
- `SD_FILE_POS` contains the full 32-bit loaded byte count.
- `SD_EOF` is nonzero if the load reached end of file.

## Opening And Streaming A File

Use `FS_OPEN` and repeated `FS_READ` when your program wants to process a file
in chunks.

```asm
fs_open_demo:
    jsr fs_write_path

    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_OPEN_MODE.
    ldx #SD_OPEN_MODE
@skip:
    lda IDXA_PORT
    dex
    bne @skip

    lda #FS_OPEN_READ
    sta IDXA_PORT

    lda #CMD_FS_OPEN
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts
```

To read up to 256 bytes into the transfer buffer:

```asm
fs_read_256:
    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_REQUEST_LEN_L.
    ldx #SD_REQUEST_LEN_L
@skip:
    lda IDXA_PORT
    dex
    bne @skip

    lda #$00
    sta IDXA_PORT       ; low byte of 256
    lda #$01
    sta IDXA_PORT       ; high byte of 256

    lda #CMD_FS_READ
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    bne @done

    lda #IIDX_FS_TRANSFER
    sta IDXA_SELECT
    ; Read SD_RESULT_LEN bytes from IDXA_PORT.
@done:
    rts
```

If `SD_REQUEST_LEN` is zero, `FS_READ` fills as much of the 1984-byte transfer
buffer as possible. `SD_EOF` becomes nonzero after the read that reaches the end
of the file.

Close the implicit file handle when finished:

```asm
fs_close:
    lda #CMD_FS_CLOSE
    jsr mia_cmd
    jsr sd_wait
    rts
```

## Saving A File

For normal save files, use `FS_OPEN_WRITE_CREATE` to create or truncate the
path, copy bytes into `IIDX_FS_TRANSFER`, set `SD_REQUEST_LEN`, then issue
`FS_WRITE`. Use `FS_SYNC` when the save must be committed before the file is
closed. `FS_CLOSE` also flushes the file.

```asm
save_path:
    .byte "/STATE.BIN",0

save_bytes:
    .byte $43,$4C,$45,$4D,$01,$00,$00,$00
save_bytes_end:

fs_save_demo:
    lda #IIDX_FS_PATH
    sta IDXA_SELECT
    ldy #$00
@path:
    lda save_path,y
    sta IDXA_PORT
    beq @path_done
    iny
    bne @path
@path_done:

    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_OPEN_MODE.
    ldx #SD_OPEN_MODE
@skip_mode:
    lda IDXA_PORT
    dex
    bne @skip_mode

    lda #FS_OPEN_WRITE_CREATE
    sta IDXA_PORT

    lda #CMD_FS_OPEN
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    bne @done

    lda #IIDX_FS_TRANSFER
    sta IDXA_SELECT
    ldx #$00
@copy:
    lda save_bytes,x
    sta IDXA_PORT
    inx
    cpx #(save_bytes_end - save_bytes)
    bne @copy

    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_REQUEST_LEN_L.
    ldx #SD_REQUEST_LEN_L
@skip_len:
    lda IDXA_PORT
    dex
    bne @skip_len

    lda #(save_bytes_end - save_bytes)
    sta IDXA_PORT       ; low byte
    stz IDXA_PORT       ; high byte

    lda #CMD_FS_WRITE
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    bne @close

    lda #CMD_FS_SYNC
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error

@close:
    lda #CMD_FS_CLOSE
    jsr mia_cmd
    jsr sd_wait
@done:
    rts
```

`FS_OPEN_WRITE_APPEND` is useful for logs because FatFs places the file pointer
at EOF. `FS_OPEN_READ_WRITE` is useful for save slots or databases that patch
fixed offsets in an existing file.

## Saving MIA RAM Directly

`FS_SAVE_FROM_MIA_RAM` is the whole-file counterpart to `FS_LOAD_TO_MIA_RAM`.
It opens the path, writes bytes from MIA RAM in 512-byte chunks, and closes the
file when finished.

Set:

- `SD_DEST_ADDR` to the 24-bit MIA RAM source address.
- `SD_TRANSFER_LEN` to the 32-bit byte count to save.
- `SD_OPEN_MODE` to `FS_OPEN_WRITE_CREATE`, `FS_OPEN_WRITE_APPEND`, or
  `FS_OPEN_READ_WRITE`.

```asm
fs_save_ram_demo:
    jsr fs_write_path

    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_DEST_ADDR_L.
    ldx #SD_DEST_ADDR_L
@skip_dest:
    lda IDXA_PORT
    dex
    bne @skip_dest

    lda #$00
    sta IDXA_PORT       ; SD_DEST_ADDR_L
    lda #$40
    sta IDXA_PORT       ; SD_DEST_ADDR_M
    lda #$01
    sta IDXA_PORT       ; SD_DEST_ADDR_H = $014000 source
    lda IDXA_PORT       ; SD_FILE_HANDLE, preserve

    lda #FS_OPEN_WRITE_CREATE
    sta IDXA_PORT       ; SD_OPEN_MODE

    ; Skip to SD_TRANSFER_LEN0.
    ldx #(SD_TRANSFER_LEN0 - SD_EOF)
@skip_len:
    lda IDXA_PORT
    dex
    bne @skip_len

    lda #$00
    sta IDXA_PORT       ; 4096 bytes = $00001000
    lda #$10
    sta IDXA_PORT
    stz IDXA_PORT
    stz IDXA_PORT

    lda #CMD_FS_SAVE_MIA
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts
```

After completion, `SD_RESULT_LEN` contains the low 16 bits of the saved byte
count and `SD_FILE_POS` contains the full 32-bit saved byte count.

## Seeking

`FS_SEEK` uses `SD_FILE_POS` as its input. Write the target offset, trigger
`FS_SEEK`, then read `SD_FILE_POS` again to learn the actual position. This is
most useful with `FS_OPEN_READ_WRITE`.

```asm
fs_seek_start:
    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_FILE_POS0.
    ldx #SD_FILE_POS0
@skip_pos:
    lda IDXA_PORT
    dex
    bne @skip_pos

    stz IDXA_PORT       ; offset 0
    stz IDXA_PORT
    stz IDXA_PORT
    stz IDXA_PORT

    lda #CMD_FS_SEEK
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts
```

## Managing Files And Directories

`FS_STAT`, `FS_MKDIR`, and `FS_DELETE` read their path from `IIDX_FS_PATH`.
`FS_STAT` fills `IIDX_FS_DIR_ENTRY` with the same metadata layout used by
`FS_READDIR`.

```asm
fs_stat_path:
    jsr fs_write_path
    lda #CMD_FS_STAT
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    bne @done

    lda #IIDX_FS_DIR_ENTRY
    sta IDXA_SELECT
    lda IDXA_PORT       ; DIR_ATTR
    sta dir_attr
    lda IDXA_PORT       ; DIR_NAME_LEN
    sta dir_name_len
@done:
    rts
```

Directory creation uses `FS_MKDIR`. Parent directories must already exist.
Deleting uses `FS_DELETE`; FAT only deletes empty directories.

```asm
fs_mkdir_path:
    jsr fs_write_path
    lda #CMD_FS_MKDIR
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts

fs_delete_path:
    jsr fs_write_path
    lda #CMD_FS_DELETE
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts
```

`FS_RENAME` uses two paths. Write the old path to `IIDX_FS_PATH` and the new
path to `IIDX_FS_PATH2`. `IIDX_FS_PATH2` overlays the first 256 bytes of the
transfer buffer, so do not preserve transfer data across a rename.

```asm
new_path:
    .byte "/STATE2.BIN",0

fs_rename_demo:
    jsr fs_write_path   ; old path from the guide's path label

    lda #IIDX_FS_PATH2
    sta IDXA_SELECT
    ldy #$00
@new_path:
    lda new_path,y
    sta IDXA_PORT
    beq @ren
    iny
    bne @new_path
@ren:
    lda #CMD_FS_RENAME
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts
```

`FS_GET_FREE` updates free-space fields in the SD control block:

- `SD_FREE_CLUSTERS`
- `SD_TOTAL_CLUSTERS`
- `SD_CLUSTER_SECTORS`

Free bytes are `SD_FREE_CLUSTERS * SD_CLUSTER_SECTORS * 512`.

```asm
fs_get_free:
    lda #CMD_FS_GET_FREE
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts
```

## Listing A Directory

Write the directory path, then issue `FS_OPENDIR`. Use `/` for the root
directory.

```asm
root_path:
    .byte "/",0

fs_open_root:
    lda #IIDX_FS_PATH
    sta IDXA_SELECT
    lda #'/'
    sta IDXA_PORT
    stz IDXA_PORT

    lda #CMD_FS_OPENDIR
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    rts
```

Each `FS_READDIR` fills `IIDX_FS_DIR_ENTRY` with one entry.

```asm
fs_read_dir_entry:
    lda #CMD_FS_READDIR
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    bne @done

    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_EOF.
    ldx #SD_EOF
@skip_eof:
    lda IDXA_PORT
    dex
    bne @skip_eof

    lda IDXA_PORT       ; SD_EOF
    bne @end_of_dir

    lda #IIDX_FS_DIR_ENTRY
    sta IDXA_SELECT
    lda IDXA_PORT       ; DIR_ATTR
    sta dir_attr
    lda IDXA_PORT       ; DIR_NAME_LEN
    sta dir_name_len
    ; Continue reading fields/name as needed.
@done:
    rts

@end_of_dir:
    ; Directory exhausted.
    rts
```

If `DIR_ATTR & DIR_ATTR_DIRECTORY` is nonzero, the entry is a directory.

## Reading A Raw Sector

Raw sectors use the `SD_LBA` field and the 512-byte sector buffer.

```asm
sd_read_lba0:
    lda #CMD_SD_INIT
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    bne @done

    lda #IIDX_SD_CONTROL
    sta IDXA_SELECT

    ; Skip to SD_LBA0.
    ldx #SD_LBA0
@skip_lba:
    lda IDXA_PORT
    dex
    bne @skip_lba

    stz IDXA_PORT       ; LBA byte 0
    stz IDXA_PORT       ; LBA byte 1
    stz IDXA_PORT       ; LBA byte 2
    stz IDXA_PORT       ; LBA byte 3

    lda #CMD_SD_READ_SECTOR
    jsr mia_cmd
    jsr sd_wait
    jsr sd_last_error
    bne @done

    lda #IIDX_SD_SECTOR
    sta IDXA_SELECT
    ; Stream 512 bytes from IDXA_PORT.
@done:
    rts
```

`SD_WRITE_SECTOR` writes the 512-byte sector buffer to `SD_LBA`. Use it only for
tools that understand the on-card format. It can corrupt FAT media if used on
sectors owned by the filesystem.

## Error Handling

On failure:

- `SD_LAST_ERROR` contains a MIA error code.
- `ERROR_L` exposes the normal MIA error queue.
- `SD_FATFS_RESULT` contains a raw FatFs result for filesystem commands.
- `IRQ_SD_ERROR` is latched.

Common MIA SD/FS errors:

| Code | Name |
| ---: | --- |
| `$70` | `ERROR_SD_BUSY` |
| `$71` | `ERROR_SD_INIT_FAILED` |
| `$72` | `ERROR_SD_NOT_READY` |
| `$73` | `ERROR_SD_READ_FAILED` |
| `$74` | `ERROR_SD_WRITE_FAILED` |
| `$78` | `ERROR_FS_MOUNT_FAILED` |
| `$79` | `ERROR_FS_OPEN_FAILED` |
| `$7A` | `ERROR_FS_READ_FAILED` |
| `$7B` | `ERROR_FS_CLOSE_FAILED` |
| `$7C` | `ERROR_FS_DIR_FAILED` |
| `$7D` | `ERROR_FS_INVALID_REQUEST` |
| `$7E` | `ERROR_FS_NO_FILE_OPEN` |
| `$7F` | `ERROR_FS_WRITE_FAILED` |
| `$80` | `ERROR_FS_SEEK_FAILED` |
| `$81` | `ERROR_FS_SYNC_FAILED` |
| `$82` | `ERROR_FS_STAT_FAILED` |
| `$83` | `ERROR_FS_MKDIR_FAILED` |
| `$84` | `ERROR_FS_DELETE_FAILED` |
| `$85` | `ERROR_FS_RENAME_FAILED` |
| `$86` | `ERROR_FS_FREE_FAILED` |

The terminal command `status sd` is the fastest way to inspect the last SD/FS
state during bring-up.

## Practical Advice

- Use FAT32 on SD cards unless there is a specific reason to use FAT16.
- Keep filenames short and ASCII while the kernel and monitor are young.
- Mount once at startup, then open/read/close as needed.
- Prefer `FS_LOAD_TO_MIA_RAM` for program and asset loading.
- Prefer `FS_OPEN` plus repeated `FS_READ` for parsers and stream formats.
- Use `FS_OPEN_WRITE_CREATE`, `FS_WRITE`, `FS_SYNC`, and `FS_CLOSE` for small
  save records that the 6502 is already streaming through the transfer buffer.
- Use `FS_SAVE_FROM_MIA_RAM` for larger contiguous MIA RAM saves.
- Use `FS_MKDIR` before saving into a new directory; it creates one level only.
- Close open files and directory cursors before deleting or renaming paths.
- Keep raw writes behind explicit tools; they bypass file-level safety.
