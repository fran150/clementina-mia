; kernel.s - Clementina MIA interactive text demo
;
; The MIA fast loader copies this program to $4000 and points the 65C02 reset
; vector here. It displays:
;
;       *** Clementina 6502 computer ***
;       Ready
;
; at the upper-left corner, places a blinking cursor on the line below, then
; polls MIA's input text FIFO and echoes typed characters to the screen. The
; arrow keys move the cursor through the full 40x25 screen.
;
; The background uses the C64 lowercase/uppercase font plane. Nametable cells
; hold C64 screen codes, not ASCII. Printable ASCII is converted just enough for
; normal text: lowercase a-z become screen codes 1-26; other printable bytes are
; written unchanged.
;
; Assemble:
;       ca65 kernel.s -o kernel.o
;       ld65 -C kernel.cfg kernel.o -o kernel.bin

; ---------------------------------------------------------------- MIA registers
IDXA_PORT          = $FFE0
IDXA_SELECT        = $FFE1
CFG_SELECT         = $FFE2
CFG_PORT           = $FFE3
IDXB_PORT          = $FFE4
IDXB_SELECT        = $FFE5
CMD_PARAM1         = $FFE6
CMD_TRIGGER        = $FFE9
INPUT_CHAR         = $FFF3
INPUT_CHAR_COUNT   = $FFF4

; ------------------------------------------------------------ config registers
CFG_IDX0_ADDR_L    = $00
CFG_IDX0_ADDR_M    = $01
CFG_IDX0_ADDR_H    = $02
CFG_IDX0_STEP_L    = $09
CFG_IDX0_STEP_H    = $0A
CFG_IDX0_FLAGS     = $0B
CFG_IDX1_ADDR_L    = $10
CFG_IDX1_ADDR_M    = $11
CFG_IDX1_ADDR_H    = $12
CFG_IDX1_STEP_L    = $19
CFG_IDX1_STEP_H    = $1A
CFG_IDX1_FLAGS     = $1B

; ---------------------------------------------------------------- video indexes
VIDX_LAYER_ENABLE  = $81
VIDX_BG_VIEWPORT   = $82
VIDX_BANK_SELECT   = $85
VIDX_CHR_1BPP      = $86
VIDX_BACKDROP      = $87
VIDX_PALETTE_0     = $90
VIDX_BG_NT_0       = $A8

; ---------------------------------------------------------------- commands
CMD_VIDEO_SET_MODE = $43

; ---------------------------------------------------------------- layout
SCREEN_W           = 40
NT_CELLS           = 1000          ; 40 x 25 nametable cells
BG_NT_BASE_M       = $C2           ; middle byte of video nametable base $00C200
KBD_BITMAP_M       = $10           ; middle byte of keyboard bitmap base $011000
KBD_BITMAP_H       = $01           ; high byte of keyboard bitmap base $011000
INPUT_START        = 2 * SCREEN_W  ; row 2, col 0
TITLE_LEN          = 32
READY_LEN          = 5
TITLE_GAP          = SCREEN_W - TITLE_LEN

; ---------------------------------------------------------- HID keyboard bits
KBD_ARROW_BYTE_9   = $09           ; HID usage $4F: right arrow, bit 7
KBD_ARROW_BYTE_10  = $0A           ; HID usages $50-$52: left/down/up
KBD_RIGHT_MASK     = $80
KBD_LEFT_MASK      = $01
KBD_DOWN_MASK      = $02
KBD_UP_MASK        = $04

ARROW_RIGHT        = $01
ARROW_LEFT         = $02
ARROW_DOWN         = $04
ARROW_UP           = $08

; ---------------------------------------------------------------- characters
CH_SPACE           = $20
CH_CURSOR          = $5F           ; underscore
CH_CR              = $0D
CH_LF              = $0A
CH_BS              = $08
CH_TAB             = $09
CH_ESC             = $1B
CH_DEL             = $7F
CH_CSI             = $5B
CH_ARROW_UP        = $41
CH_ARROW_DOWN      = $42
CH_ARROW_RIGHT     = $43
CH_ARROW_LEFT      = $44

; ---------------------------------------------------------------- colors (RGB565)
COL_BG             = $0014         ; background: dark blue
COL_FG             = $FFFF         ; text: white

; ---------------------------------------------------------------- timing
BLINK_TICKS_HI     = $20

; ---------------------------------------------------------------- zero page
ptr                = $00           ; 16-bit down-counter / scratch
cursor_lo          = $02           ; active nametable offset, low byte
cursor_hi          = $03           ; active nametable offset, high byte
cursor_col         = $04           ; active column, 0-39
cursor_visible     = $05
blink_lo           = $06
blink_hi           = $07
cell_value         = $08
cursor_under       = $09
arrow_current      = $0A
arrow_previous     = $0B
arrow_pressed      = $0C
escape_state       = $0D

.segment "CODE"

reset:
        sei
        cld
        ldx     #$FF
        txs

        ; ---- palette bank 0: color 0 = background, color 1 = text ----
        lda     #VIDX_PALETTE_0
        sta     IDXA_SELECT
        lda     #<COL_BG
        sta     IDXA_PORT
        lda     #>COL_BG
        sta     IDXA_PORT
        lda     #<COL_FG
        sta     IDXA_PORT
        lda     #>COL_FG
        sta     IDXA_PORT

        ; ---- enable background layer only ----
        lda     #VIDX_LAYER_ENABLE
        sta     IDXA_SELECT
        lda     #$01                    ; bit0 = background
        sta     IDXA_PORT

        ; ---- viewport mode 0 (single 40x25, table 0), active set 0 ----
        lda     #VIDX_BG_VIEWPORT
        sta     IDXA_SELECT
        lda     #$00                    ; BG_VIEWPORT_MODE = 0
        sta     IDXA_PORT
        lda     #$00                    ; BG_ACTIVE_SET = 0
        sta     IDXA_PORT

        ; ---- CHR banks: bg, bg_alt, overlay, overlay_alt, sprite = 0 ----
        lda     #VIDX_BANK_SELECT
        sta     IDXA_SELECT
        lda     #$00
        sta     IDXA_PORT
        sta     IDXA_PORT
        sta     IDXA_PORT
        sta     IDXA_PORT
        sta     IDXA_PORT

        ; ---- 1bpp: bank 0 = 1bpp, background plane selector = 1 ----
        lda     #VIDX_CHR_1BPP
        sta     IDXA_SELECT
        lda     #$01                    ; CHR_1BPP_MASK: bank 0 decoded as 1bpp
        sta     IDXA_PORT
        lda     #$01                    ; CHR_1BPP_PLANES: BG_PLANE (bits 0-1) = 1
        sta     IDXA_PORT

        ; ---- backdrop color = palette 0, color 0 ----
        lda     #VIDX_BACKDROP
        sta     IDXA_SELECT
        lda     #$00
        sta     IDXA_PORT

        jsr     configure_indexes
        jsr     paint_screen
        jsr     init_cursor
        jsr     show_cursor

        ; ---- enable video last, so the first synced frame is complete ----
        lda     #$01                    ; VIDEO_MODE.enable
        sta     CMD_PARAM1
        lda     #CMD_VIDEO_SET_MODE
        sta     CMD_TRIGGER

main_loop:
        jsr     poll_input
        jsr     poll_keyboard
        jsr     tick_cursor
        jmp     main_loop

; ---------------------------------------------------------------------------
; Initialization
; ---------------------------------------------------------------------------

configure_indexes:
        ldx     #CFG_IDX0_ADDR_M
        lda     #KBD_BITMAP_M
        jsr     write_cfg
        ldx     #CFG_IDX0_ADDR_H
        lda     #KBD_BITMAP_H
        jsr     write_cfg
        ldx     #CFG_IDX0_STEP_L
        lda     #$00
        jsr     write_cfg
        ldx     #CFG_IDX0_STEP_H
        lda     #$00
        jsr     write_cfg
        ldx     #CFG_IDX0_FLAGS
        lda     #$00
        jsr     write_cfg

        ldx     #CFG_IDX1_STEP_L
        lda     #$00
        jsr     write_cfg
        ldx     #CFG_IDX1_STEP_H
        lda     #$00
        jsr     write_cfg
        ldx     #CFG_IDX1_FLAGS
        lda     #$00
        jsr     write_cfg
        lda     #$01
        sta     IDXB_SELECT
        rts

paint_screen:
        lda     #VIDX_BG_NT_0
        sta     IDXA_SELECT

        ldx     #CH_SPACE
        lda     #<NT_CELLS
        sta     ptr
        lda     #>NT_CELLS
        sta     ptr+1
clear_loop:
        stx     IDXA_PORT
        jsr     dec_ptr
        bne     clear_loop

        ldy     #$00
title_loop:
        lda     title_text,y
        sta     IDXA_PORT
        iny
        cpy     #TITLE_LEN
        bne     title_loop

        ldx     #TITLE_GAP
title_gap_loop:
        lda     #CH_SPACE
        sta     IDXA_PORT
        dex
        bne     title_gap_loop

        ldy     #$00
ready_loop:
        lda     ready_text,y
        sta     IDXA_PORT
        iny
        cpy     #READY_LEN
        bne     ready_loop
        rts

init_cursor:
        lda     #<INPUT_START
        sta     cursor_lo
        lda     #>INPUT_START
        sta     cursor_hi
        lda     #$00
        sta     cursor_col
        sta     cursor_visible
        sta     blink_lo
        sta     blink_hi
        sta     arrow_current
        sta     arrow_previous
        sta     arrow_pressed
        sta     escape_state
        lda     #CH_SPACE
        sta     cursor_under
        rts

; ---------------------------------------------------------------------------
; Input polling and editing
; ---------------------------------------------------------------------------

poll_input:
        lda     INPUT_CHAR_COUNT
        beq     :+
@loop:
        lda     INPUT_CHAR
        jsr     handle_input_byte
        lda     INPUT_CHAR_COUNT
        bne     @loop
:       rts

handle_input_byte:
        sta     cell_value
        lda     escape_state
        beq     @normal
        cmp     #$01
        beq     @after_esc

        lda     #$00
        sta     escape_state
        lda     cell_value
        cmp     #CH_ARROW_UP
        beq     esc_up
        cmp     #CH_ARROW_DOWN
        beq     esc_down
        cmp     #CH_ARROW_RIGHT
        beq     esc_right
        cmp     #CH_ARROW_LEFT
        beq     esc_left
        rts

@after_esc:
        lda     #$00
        sta     escape_state
        lda     cell_value
        cmp     #CH_CSI
        bne     @normal
        lda     #$02
        sta     escape_state
        rts

@normal:
        lda     cell_value
        cmp     #CH_ESC
        bne     :+
        lda     #$01
        sta     escape_state
        rts

:       lda     cell_value
        cmp     #CH_CR
        beq     input_newline
        cmp     #CH_LF
        beq     input_newline
        cmp     #CH_BS
        beq     input_backspace
        cmp     #CH_DEL
        beq     input_backspace
        cmp     #CH_TAB
        beq     input_tab
        cmp     #CH_SPACE
        bcc     input_done

        jsr     ascii_to_screen
        jsr     put_screen_char
input_done:
        rts

esc_up:
        jsr     move_cursor_up
        rts

esc_down:
        jsr     move_cursor_down
        rts

esc_right:
        jsr     move_cursor_right
        rts

esc_left:
        jsr     move_cursor_left
        rts

input_tab:
        lda     #CH_SPACE
        jsr     put_screen_char
        lda     #CH_SPACE
        jsr     put_screen_char
        lda     #CH_SPACE
        jsr     put_screen_char
        lda     #CH_SPACE
        jsr     put_screen_char
        rts

input_newline:
        jsr     hide_cursor
        lda     #SCREEN_W
        sec
        sbc     cursor_col
        jsr     add_to_cursor
        lda     #$00
        sta     cursor_col
        jsr     wrap_cursor_if_needed
        jsr     reset_blink
        jsr     show_cursor
        rts

input_backspace:
        jsr     hide_cursor
        lda     cursor_hi
        bne     :+
        lda     cursor_lo
        beq     @done
        jmp     @move_back

:       lda     cursor_lo
@move_back:
        lda     cursor_lo
        bne     :+
        dec     cursor_hi
:       dec     cursor_lo

        lda     cursor_col
        bne     @same_line
        lda     #SCREEN_W - 1
        sta     cursor_col
        jmp     @clear_cell

@same_line:
        dec     cursor_col

@clear_cell:
        lda     #CH_SPACE
        jsr     write_cursor_cell

@done:
        jsr     reset_blink
        jsr     show_cursor
        rts

poll_keyboard:
        lda     #$00
        sta     arrow_current

        lda     #KBD_ARROW_BYTE_9
        jsr     read_keyboard_byte
        and     #KBD_RIGHT_MASK
        beq     :+
        lda     arrow_current
        ora     #ARROW_RIGHT
        sta     arrow_current

:       lda     #KBD_ARROW_BYTE_10
        jsr     read_keyboard_byte
        sta     cell_value

        lda     cell_value
        and     #KBD_LEFT_MASK
        beq     :+
        lda     arrow_current
        ora     #ARROW_LEFT
        sta     arrow_current

:       lda     cell_value
        and     #KBD_DOWN_MASK
        beq     :+
        lda     arrow_current
        ora     #ARROW_DOWN
        sta     arrow_current

:       lda     cell_value
        and     #KBD_UP_MASK
        beq     :+
        lda     arrow_current
        ora     #ARROW_UP
        sta     arrow_current

:       lda     arrow_previous
        eor     #$FF
        and     arrow_current
        sta     arrow_pressed

        lda     arrow_pressed
        and     #ARROW_LEFT
        beq     :+
        jsr     move_cursor_left

:       lda     arrow_pressed
        and     #ARROW_RIGHT
        beq     :+
        jsr     move_cursor_right

:       lda     arrow_pressed
        and     #ARROW_UP
        beq     :+
        jsr     move_cursor_up

:       lda     arrow_pressed
        and     #ARROW_DOWN
        beq     :+
        jsr     move_cursor_down

:       lda     arrow_current
        sta     arrow_previous
        rts

read_keyboard_byte:
        ldx     #CFG_IDX0_ADDR_L
        jsr     write_cfg
        lda     #$00
        sta     IDXA_SELECT
        lda     IDXA_PORT
        rts

move_cursor_left:
        jsr     hide_cursor
        lda     cursor_hi
        bne     :+
        lda     cursor_lo
        beq     @done

:       lda     cursor_lo
        bne     :+
        dec     cursor_hi
:       dec     cursor_lo

        lda     cursor_col
        bne     @same_line
        lda     #SCREEN_W - 1
        sta     cursor_col
        jmp     @done

@same_line:
        dec     cursor_col

@done:
        jsr     reset_blink
        jsr     show_cursor
        rts

move_cursor_right:
        jsr     hide_cursor
        jsr     advance_cursor
        jsr     reset_blink
        jsr     show_cursor
        rts

move_cursor_up:
        jsr     hide_cursor
        lda     cursor_hi
        bne     @move
        lda     cursor_lo
        cmp     #SCREEN_W
        bcc     @done

@move:
        lda     cursor_lo
        sec
        sbc     #SCREEN_W
        sta     cursor_lo
        bcs     @done
        dec     cursor_hi

@done:
        jsr     reset_blink
        jsr     show_cursor
        rts

move_cursor_down:
        jsr     hide_cursor
        lda     cursor_hi
        cmp     #>(NT_CELLS - SCREEN_W)
        bcc     @move
        bne     @done
        lda     cursor_lo
        cmp     #<(NT_CELLS - SCREEN_W)
        bcs     @done

@move:
        lda     #SCREEN_W
        jsr     add_to_cursor

@done:
        jsr     reset_blink
        jsr     show_cursor
        rts

put_screen_char:
        pha
        jsr     hide_cursor
        pla
        jsr     write_cursor_cell
        jsr     advance_cursor
        jsr     reset_blink
        jsr     show_cursor
        rts

advance_cursor:
        lda     #$01
        jsr     add_to_cursor
        inc     cursor_col
        lda     cursor_col
        cmp     #SCREEN_W
        bne     :+
        lda     #$00
        sta     cursor_col
:       jsr     wrap_cursor_if_needed
        rts

add_to_cursor:
        clc
        adc     cursor_lo
        sta     cursor_lo
        bcc     :+
        inc     cursor_hi
:       rts

wrap_cursor_if_needed:
        lda     cursor_hi
        cmp     #>NT_CELLS
        bcc     :+
        bne     @wrap
        lda     cursor_lo
        cmp     #<NT_CELLS
        bcc     :+
@wrap:
        lda     #$00
        sta     cursor_lo
        sta     cursor_hi
        sta     cursor_col
:       rts

ascii_to_screen:
        cmp     #$61                    ; 'a'
        bcc     :+
        cmp     #$7B                    ; 'z' + 1
        bcs     :+
        sec
        sbc     #$60
:       rts

; ---------------------------------------------------------------------------
; Cursor drawing
; ---------------------------------------------------------------------------

tick_cursor:
        inc     blink_lo
        bne     :+
        inc     blink_hi
        lda     blink_hi
        cmp     #BLINK_TICKS_HI
        bne     :+
        jsr     reset_blink
        lda     cursor_visible
        beq     @show
        jsr     hide_cursor
        rts
@show:
        jsr     show_cursor
:       rts

reset_blink:
        lda     #$00
        sta     blink_lo
        sta     blink_hi
        rts

show_cursor:
        lda     cursor_visible
        bne     :+
        jsr     read_cursor_cell
        sta     cursor_under
        lda     #CH_CURSOR
        jsr     write_cursor_cell
        lda     #$01
        sta     cursor_visible
:       rts

hide_cursor:
        lda     cursor_visible
        beq     :+
        lda     cursor_under
        jsr     write_cursor_cell
        lda     #$00
        sta     cursor_visible
:       rts

read_cursor_cell:
        jsr     position_cursor_index
        lda     IDXB_PORT
        rts

write_cursor_cell:
        sta     cell_value
        jsr     position_cursor_index
        lda     cell_value
        sta     IDXB_PORT
        rts

position_cursor_index:
        ldx     #CFG_IDX1_ADDR_L
        lda     cursor_lo
        jsr     write_cfg

        ldx     #CFG_IDX1_ADDR_M
        lda     cursor_hi
        clc
        adc     #BG_NT_BASE_M
        jsr     write_cfg

        ldx     #CFG_IDX1_ADDR_H
        lda     #$00
        jsr     write_cfg

        lda     #$01
        sta     IDXB_SELECT
        rts

write_cfg:
        stx     CFG_SELECT
        sta     CFG_PORT
        rts

; ---------------------------------------------------------------------------
; Utilities and text
; ---------------------------------------------------------------------------

; Decrement the 16-bit counter at ptr. On return Z=0 while ptr is still
; nonzero, Z=1 once it reaches zero. Clobbers A.
dec_ptr:
        lda     ptr
        bne     :+
        dec     ptr+1
:       dec     ptr
        lda     ptr
        ora     ptr+1
        rts

; "*** Clementina 6502 computer ***" as C64 lowercase-set screen codes.
title_text:
        .byte $2A,$2A,$2A,$20
        .byte $43,$0C,$05,$0D,$05,$0E,$14,$09,$0E,$01
        .byte $20,$36,$35,$30,$32,$20
        .byte $03,$0F,$0D,$10,$15,$14,$05,$12
        .byte $20,$2A,$2A,$2A

; "Ready" as C64 lowercase-set screen codes.
ready_text:
        .byte $52,$05,$01,$04,$19
