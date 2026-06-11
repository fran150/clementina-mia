; kernel.s - Clementina MIA "Hello world" demo
;
; The MIA fast loader copies this program to $4000 and points the 65C02 reset
; vector here. It displays:
;
;       Hello world from clementina!!!!
;
; using the C64 PETSCII font that the MIA firmware preloads into CHR bank 0:
;
;       bank 0 plane 0 = C64 uppercase / graphics set
;       bank 0 plane 1 = C64 lowercase / uppercase set
;
; The background uses bank 0 in 1bpp mode, plane 1 (the lowercase/uppercase
; set), so the mixed-case message renders correctly. Nametable cells hold C64
; screen codes, not ASCII.
;
; Assemble:
;       ca65 kernel.s -o kernel.o
;       ld65 -C kernel.cfg kernel.o -o kernel.bin

; ---------------------------------------------------------------- MIA registers
IDXA_PORT          = $FFE0
IDXA_SELECT        = $FFE1
CMD_PARAM1         = $FFE6
CMD_TRIGGER        = $FFE9

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
NT_CELLS           = 1000          ; 40 x 25 nametable cells
MSG_OFFSET         = 12*40 + 4     ; row 12, col 4 (centers a 31-char line)
MSG_LEN            = 31
TAIL_COUNT         = NT_CELLS - MSG_OFFSET - MSG_LEN

; ---------------------------------------------------------------- colors (RGB565)
COL_BG             = $0014         ; background: dark blue
COL_FG             = $FFFF         ; text: white

; ---------------------------------------------------------------- zero page
ptr                = $00           ; 16-bit down-counter / scratch

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

        ; ---- 1bpp: bank 0 = 1bpp, background plane selector = 1 (lowercase) ----
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

        ; ---- paint nametable 0: leading spaces, message, trailing spaces ----
        lda     #VIDX_BG_NT_0
        sta     IDXA_SELECT

        ldx     #$20                    ; space (screen code 32), held in X
        lda     #<MSG_OFFSET
        sta     ptr
        lda     #>MSG_OFFSET
        sta     ptr+1
pre_fill:
        stx     IDXA_PORT
        jsr     dec_ptr
        bne     pre_fill

        ldy     #$00
msg_loop:
        lda     message,y
        sta     IDXA_PORT
        iny
        cpy     #MSG_LEN
        bne     msg_loop

        ldx     #$20
        lda     #<TAIL_COUNT
        sta     ptr
        lda     #>TAIL_COUNT
        sta     ptr+1
post_fill:
        stx     IDXA_PORT
        jsr     dec_ptr
        bne     post_fill

        ; ---- enable video last, so the first synced frame is complete ----
        lda     #$01                    ; VIDEO_MODE.enable
        sta     CMD_PARAM1
        lda     #CMD_VIDEO_SET_MODE
        sta     CMD_TRIGGER

hang:
        jmp     hang

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

; "Hello world from clementina!!!!" as C64 lowercase-set screen codes.
; Uppercase, space, and '!' share their ASCII values; lowercase a-z map to 1-26.
message:
        .byte $48,$05,$0C,$0C,$0F        ; H e l l o
        .byte $20                        ; (space)
        .byte $17,$0F,$12,$0C,$04        ; w o r l d
        .byte $20                        ; (space)
        .byte $06,$12,$0F,$0D            ; f r o m
        .byte $20                        ; (space)
        .byte $03,$0C,$05,$0D,$05,$0E,$14,$09,$0E,$01  ; c l e m e n t i n a
        .byte $21,$21,$21,$21            ; ! ! ! !
