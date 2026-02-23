; test_monitor.s - Keyboard + Video monitor
;
; Simple terminal: reads keyboard, echoes to frame buffer via video hardware.
; Tests: keyboard polling, frame buffer writes, video registers, cursor,
;        scroll, printable ASCII, backspace, enter.
;
; Expected workflow with n8gdb:
;   load test_monitor 0xE000
;   reset
;   run
;   (type in the emulator window -- text appears on Screen)

.export   _main
.export   cursor_on, cursor_off, put_char, new_line, scroll_up, clear_screen
.export   kbd_wait, kbd_read

; --- Hardware registers ---
KBD_DATA   = $D860
KBD_STATUS = $D861
KBD_ACK    = $D861

VID_OPER   = $D844
VID_CURSOR = $D845
VID_CURCOL = $D846
VID_CURROW = $D847

FB_BASE    = $C000

; Video operation codes
VIDOP_SCROLL_UP = $01

; Cursor: flash + block + rate 15
;   bits 0-1: mode  = 2 (flash)
;   bits 2-3: shape = 1 (block) = $04
;   bits 4-7: rate  = 15        = $F0
CURSOR_STYLE = $F6

; --- Zero page variables ---
.segment "ZEROPAGE"
zp_col:   .res 1               ; current column (0-79)
zp_row:   .res 1               ; current row (0-24)
zp_fb:    .res 2               ; frame buffer pointer
zp_str:   .res 2               ; string pointer (separate from fb)
zp_tmp:   .res 1               ; temp for Y save

.segment "CODE"

_main:
        JSR clear_screen

        ; Print welcome banner
        LDA #<str_banner
        STA zp_str
        LDA #>str_banner
        STA zp_str+1
        JSR print_str
        JSR new_line

        LDA #<str_prompt
        STA zp_str
        LDA #>str_prompt
        STA zp_str+1
        JSR print_str

        JSR cursor_on

; --- Main loop: poll keyboard, echo to screen ---
main_loop:
        JSR kbd_wait
        JSR kbd_read            ; A = keycode

        ; Backspace ($08)?
        CMP #$08
        BEQ do_backspace

        ; Enter ($0D)?
        CMP #$0D
        BEQ do_enter

        ; Printable ASCII ($20-$7E)?
        CMP #$20
        BCC main_loop
        CMP #$7F
        BCS main_loop

        ; Print the character
        PHA
        JSR cursor_off
        PLA
        JSR put_char
        JSR cursor_on
        JMP main_loop

do_backspace:
        LDA zp_col
        BEQ main_loop           ; already at column 0
        JSR cursor_off
        DEC zp_col
        LDA #$20                ; erase with space
        JSR put_char_at_cur
        DEC zp_col
        JSR cursor_on
        JMP main_loop

do_enter:
        JSR cursor_off
        JSR new_line
        LDA #<str_prompt
        STA zp_str
        LDA #>str_prompt
        STA zp_str+1
        JSR print_str
        JSR cursor_on
        JMP main_loop

; ============================================================
; Subroutines
; ============================================================

; --- Print null-terminated string at (zp_str) ---
print_str:
        LDY #$00
@loop:  LDA (zp_str),Y
        BEQ @done
        STY zp_tmp
        JSR put_char
        LDY zp_tmp
        INY
        BNE @loop
@done:  RTS

; --- Put character A at cursor position, advance cursor ---
put_char:
        PHA
        JSR calc_fb_addr
        PLA
        LDY #$00
        STA (zp_fb),Y

        INC zp_col
        LDA zp_col
        CMP #80
        BCC @update
        LDA #$00
        STA zp_col
        JSR advance_row
@update:
        LDA zp_col
        STA VID_CURCOL
        LDA zp_row
        STA VID_CURROW
        RTS

; --- Put char A at cursor without advancing row (for backspace erase) ---
put_char_at_cur:
        PHA
        JSR calc_fb_addr
        PLA
        LDY #$00
        STA (zp_fb),Y
        INC zp_col
        LDA zp_col
        STA VID_CURCOL
        RTS

; --- Calculate FB address: zp_fb = FB_BASE + row*80 + col ---
;     row*80 = row*64 + row*16
;     Compute row*16 first, then shift left 2 more for row*64, add them.
calc_fb_addr:
        ; row << 4 (row * 16)
        LDA #$00
        STA zp_fb+1
        LDA zp_row
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        STA zp_fb               ; zp_fb = row*16

        ; Save row*16 on stack
        LDA zp_fb+1
        PHA
        LDA zp_fb
        PHA

        ; row*64 = row*16 << 2
        ASL zp_fb
        ROL zp_fb+1
        ASL zp_fb
        ROL zp_fb+1
        ; zp_fb = row*64

        ; row*80 = row*64 + row*16
        PLA                     ; lo(row*16)
        CLC
        ADC zp_fb
        STA zp_fb
        PLA                     ; hi(row*16)
        ADC zp_fb+1
        STA zp_fb+1

        ; + col
        CLC
        LDA zp_fb
        ADC zp_col
        STA zp_fb
        LDA zp_fb+1
        ADC #$00
        STA zp_fb+1

        ; + FB_BASE ($C000)
        CLC
        LDA zp_fb+1
        ADC #>FB_BASE
        STA zp_fb+1
        RTS

; --- New line ---
new_line:
        LDA #$00
        STA zp_col
        JSR advance_row
        LDA zp_col
        STA VID_CURCOL
        LDA zp_row
        STA VID_CURROW
        RTS

; --- Advance row, scroll if at bottom ---
advance_row:
        INC zp_row
        LDA zp_row
        CMP #25
        BCC @done
        JSR scroll_up
        LDA #24
        STA zp_row
@done:  RTS

; --- Scroll up via hardware ---
scroll_up:
        LDA #VIDOP_SCROLL_UP
        STA VID_OPER
        RTS

; --- Clear screen ---
clear_screen:
        LDA #$00
        STA zp_col
        STA zp_row

        LDA #<FB_BASE
        STA zp_fb
        LDA #>FB_BASE
        STA zp_fb+1

        LDA #$20
        LDX #$08                ; 8 pages (2048 bytes, covers 80*25=2000)
        LDY #$00
@page:  STA (zp_fb),Y
        INY
        BNE @page
        INC zp_fb+1
        DEX
        BNE @page

        LDA #$00
        STA VID_CURCOL
        STA VID_CURROW
        RTS

; --- Cursor on (flashing block) ---
cursor_on:
        LDA zp_col
        STA VID_CURCOL
        LDA zp_row
        STA VID_CURROW
        LDA #CURSOR_STYLE
        STA VID_CURSOR
        RTS

; --- Cursor off ---
cursor_off:
        LDA #$00
        STA VID_CURSOR
        RTS

; --- Wait for key ---
kbd_wait:
        LDA KBD_STATUS
        AND #$01
        BEQ kbd_wait
        RTS

; --- Read and ack key ---
kbd_read:
        LDA KBD_DATA
        PHA
        LDA #$01
        STA KBD_ACK
        PLA
        RTS

; ============================================================
; Strings
; ============================================================
str_banner:
        .byte "N8 Machine Monitor v0.1", 0
str_prompt:
        .byte "> ", 0
