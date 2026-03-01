; adventure.s - Sprawl Adventure engine
;
; Frame buffer + video registers + keyboard I/O.
; FB I/O routines adapted from test_monitor.s.
; Readline adapted from mon1.s for keyboard+FB.

.export   _main
.export   cursor_on, cursor_off, put_char, new_line, scroll_up, clear_screen
.export   kbd_wait, kbd_read, print_str, readline_fb

.import   str_banner, str_prompt, str_unknown

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
CURSOR_STYLE = $F6

; Keys
KEY_ENTER = $0D
KEY_BS    = $08

; Line buffer
BUF_SIZE  = 79                  ; max chars (leave room for null)

; --- Zero page variables ---
.segment "ZEROPAGE"
zp_col:   .res 1               ; current column (0-79)
zp_row:   .res 1               ; current row (0-24)
zp_fb:    .res 2               ; frame buffer pointer
zp_str:   .res 2               ; string pointer
zp_tmp:   .res 1               ; temp for Y save
line_len: .res 1               ; current input line length

; --- BSS (RAM) ---
.segment "BSS"
LINE_BUF: .res 80              ; input line buffer

; =====================================================================
.segment "CODE"

_main:
        JSR clear_screen

        ; Print banner
        LDA #<str_banner
        STA zp_str
        LDA #>str_banner
        STA zp_str+1
        JSR print_str
        JSR new_line

main_loop:
        ; Print prompt
        LDA #<str_prompt
        STA zp_str
        LDA #>str_prompt
        STA zp_str+1
        JSR print_str
        JSR cursor_on

        ; Read input line
        JSR readline_fb

        ; Empty line -> re-prompt
        LDA line_len
        BEQ main_loop

        ; Phase 1: echo back the input
        LDA #<LINE_BUF
        STA zp_str
        LDA #>LINE_BUF
        STA zp_str+1
        JSR print_str
        JSR new_line

        JMP main_loop

; =====================================================================
; Readline — keyboard poll + frame buffer echo
; Fills LINE_BUF, sets line_len. Null-terminates.
; =====================================================================
readline_fb:
        LDA #$00
        STA line_len
@loop:
        JSR kbd_wait
        JSR kbd_read            ; A = keycode

        ; Enter -> done
        CMP #KEY_ENTER
        BEQ @done

        ; Backspace
        CMP #KEY_BS
        BEQ @bs

        ; Printable ASCII ($20-$7E)?
        CMP #$20
        BCC @loop
        CMP #$7F
        BCS @loop

        ; Buffer full?
        LDX line_len
        CPX #BUF_SIZE
        BCS @loop

        ; Store in buffer
        STA LINE_BUF,X
        INC line_len

        ; Echo to screen
        PHA
        JSR cursor_off
        PLA
        JSR put_char
        JSR cursor_on
        JMP @loop

@bs:
        LDX line_len
        BEQ @loop               ; nothing to erase
        DEC line_len
        JSR cursor_off
        DEC zp_col
        LDA #$20                ; erase with space
        JSR put_char_at_cur
        DEC zp_col
        JSR cursor_on
        JMP @loop

@done:
        ; Null-terminate
        LDX line_len
        LDA #$00
        STA LINE_BUF,X
        ; Move to next line
        JSR cursor_off
        JSR new_line
        JSR cursor_on
        RTS

; =====================================================================
; Frame buffer I/O (from test_monitor.s)
; =====================================================================

; --- Print null-terminated string at (zp_str) ---
; Handles $0D as newline within strings.
print_str:
        LDY #$00
@loop:  LDA (zp_str),Y
        BEQ @done
        CMP #$0D
        BEQ @nl
        STY zp_tmp
        JSR put_char
        LDY zp_tmp
        INY
        BNE @loop
@done:  RTS
@nl:    STY zp_tmp
        JSR new_line
        LDY zp_tmp
        INY
        BNE @loop

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
