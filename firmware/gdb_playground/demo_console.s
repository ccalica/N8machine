; demo_console.s - Console API Visual Showcase
;
; Auto-cycling demo exercising all 9 console kernel routines.
; Phases: title, cascade fill, scroll vortex, diamond draw,
; keyboard echo, text waterfall, status + finale.
; Loops forever.
;
; Requires kernel loaded at $F000:
;   load demo_console 0xE000
;   load ../n8_kernel 0xF000
;   write 0xFFFC <reset_lo><reset_hi>
;   reset
;   run

.export   _main
.export   phase_title, phase_cascade, phase_vortex
.export   phase_diamond, phase_keyboard, phase_waterfall
.export   phase_finale

; --- Kernel entry points ---
K_CON_GETKEY    = $FE09
K_CON_SETMODE   = $FE0C
K_CON_GETSTATUS = $FE0F
K_CON_PUTCHAR   = $FE12
K_CON_NEWLINE   = $FE15
K_CON_CLEAR     = $FE18
K_CON_SCROLL    = $FE1B
K_CON_MOVCURSOR = $FE1E
K_CON_SETCURSOR = $FE21

; --- Hardware (cursor style — not in console API) ---
VID_CURSOR = $D845

; --- Delay tuning ---
DELAY_TYPE   = 25          ; per-char typing speed
DELAY_FILL   = 2           ; per-char cascade fill
DELAY_SCROLL = 12          ; per scroll step
DELAY_DRAW   = 10          ; per diamond/border char
DELAY_LINE   = 50          ; per waterfall line
PAUSE_REPS   = 8           ; × 255 units ≈ 2.6s

; --- Key codes ---
KEY_ENTER  = $0D
KEY_ESCAPE = $1B

; --- Zero page ---
.segment "ZEROPAGE"
zp_tmp:    .res 1
zp_char:   .res 1
zp_col:    .res 1
zp_row:    .res 1
zp_cnt:    .res 2             ; 16-bit counter
zp_str:    .res 2             ; string pointer
zp_i:      .res 1             ; loop variable

.segment "CODE"

; ============================================================
; Entry
; ============================================================

_main:
        LDA #$00
        STA VID_CURSOR         ; cursor off

        ; Streaming mode: advance + wrap + scroll
        LDA #$00
        LDX #$07
        JSR K_CON_SETMODE

restart:

; ============================================================
; Phase 1: Title — con_clear, con_setcursor, con_putchar
; ============================================================

phase_title:
        JSR K_CON_CLEAR

        ; Type title at center
        LDX #32
        LDY #11
        JSR K_CON_SETCURSOR
        LDA #<str_title
        STA zp_str
        LDA #>str_title
        STA zp_str+1
        JSR type_zp_str

        ; Type subtitle
        LDX #30
        LDY #13
        JSR K_CON_SETCURSOR
        LDA #<str_subtitle
        STA zp_str
        LDA #>str_subtitle
        STA zp_str+1
        JSR type_zp_str

        JSR phase_pause

; ============================================================
; Phase 2: Cascade Fill — con_setcursor, con_putchar streaming
; Stream 2000 cycling ASCII chars across the whole screen.
; ============================================================

phase_cascade:
        LDX #0
        LDY #0
        JSR K_CON_SETCURSOR

        LDA #$21               ; '!'
        STA zp_char
        LDA #0
        STA zp_cnt
        STA zp_cnt+1

@fill:  LDA zp_char
        JSR K_CON_PUTCHAR
        LDA #DELAY_FILL
        JSR delay

        INC zp_char
        LDA zp_char
        CMP #$7F
        BCC @no_wrap
        LDA #$21
        STA zp_char
@no_wrap:
        INC zp_cnt
        BNE @check
        INC zp_cnt+1
@check: LDA zp_cnt+1
        CMP #$07
        BCC @fill
        LDA zp_cnt
        CMP #$D0
        BCC @fill

        JSR phase_pause

; ============================================================
; Phase 3: Scroll Vortex — con_scroll in all 4 directions
; Rotate the ASCII pattern around the screen.
; ============================================================

phase_vortex:
        LDA #3
        STA zp_i               ; 3 rotations

@vortex:
        ; Up 6
        LDA #6
        STA zp_tmp
@vu:    LDX #0
        LDY #$FF               ; -1 = up
        JSR K_CON_SCROLL
        LDA #DELAY_SCROLL
        JSR delay
        DEC zp_tmp
        BNE @vu

        ; Right 10
        LDA #10
        STA zp_tmp
@vr:    LDX #1
        LDY #0
        JSR K_CON_SCROLL
        LDA #DELAY_SCROLL
        JSR delay
        DEC zp_tmp
        BNE @vr

        ; Down 6
        LDA #6
        STA zp_tmp
@vd:    LDX #0
        LDY #1
        JSR K_CON_SCROLL
        LDA #DELAY_SCROLL
        JSR delay
        DEC zp_tmp
        BNE @vd

        ; Left 10
        LDA #10
        STA zp_tmp
@vl:    LDX #$FF               ; -1 = left
        LDY #0
        JSR K_CON_SCROLL
        LDA #DELAY_SCROLL
        JSR delay
        DEC zp_tmp
        BNE @vl

        DEC zp_i
        BNE @vortex

        JSR phase_pause

; ============================================================
; Phase 4: Diamond Draw — con_clear, con_setcursor, con_movcursor
; Draw a diamond shape char-by-char using cursor movement.
; ============================================================

phase_diamond:
        JSR K_CON_CLEAR

        ; --- Top to Right arm: (39,5) → (45,11) ---
        LDX #39
        LDY #5
        JSR K_CON_SETCURSOR
        LDA #7
        STA zp_tmp
@d_tr:  LDA #'*'
        JSR K_CON_PUTCHAR       ; writes + advances cursor right 1
        LDX #0                  ; putchar already moved right 1
        LDY #1
        JSR K_CON_MOVCURSOR
        LDA #DELAY_DRAW
        JSR delay
        DEC zp_tmp
        BNE @d_tr

        ; --- Right to Bottom arm: (44,12) → (39,17) ---
        ; (skip (45,11) overlap — start 1 step inward)
        LDX #44
        LDY #12
        JSR K_CON_SETCURSOR
        LDA #6
        STA zp_tmp
@d_rb:  LDA #'*'
        JSR K_CON_PUTCHAR
        LDX #<(-2)             ; left 2 (net left 1 from write pos)
        LDY #1
        JSR K_CON_MOVCURSOR
        LDA #DELAY_DRAW
        JSR delay
        DEC zp_tmp
        BNE @d_rb

        ; --- Bottom to Left arm: (38,16) → (33,11) ---
        LDX #38
        LDY #16
        JSR K_CON_SETCURSOR
        LDA #6
        STA zp_tmp
@d_bl:  LDA #'*'
        JSR K_CON_PUTCHAR
        LDX #<(-2)
        LDY #$FF               ; up 1
        JSR K_CON_MOVCURSOR
        LDA #DELAY_DRAW
        JSR delay
        DEC zp_tmp
        BNE @d_bl

        ; --- Left to Top arm: (34,10) → (38,6) ---
        LDX #34
        LDY #10
        JSR K_CON_SETCURSOR
        LDA #5
        STA zp_tmp
@d_lt:  LDA #'*'
        JSR K_CON_PUTCHAR
        LDX #0
        LDY #$FF               ; up 1
        JSR K_CON_MOVCURSOR
        LDA #DELAY_DRAW
        JSR delay
        DEC zp_tmp
        BNE @d_lt

        ; Label inside diamond
        LDX #31
        LDY #11
        JSR K_CON_SETCURSOR
        LDA #<str_diamond
        STA zp_str
        LDA #>str_diamond
        STA zp_str+1
        JSR print_zp_str

        JSR phase_pause

; ============================================================
; Phase 5: Keyboard Echo — con_getkey, con_putchar, con_newline
; Interactive: type text, Enter for newline, Esc to advance.
; ============================================================

phase_keyboard:
        JSR K_CON_CLEAR
        JSR drain_keys

        ; Header
        LDX #5
        LDY #2
        JSR K_CON_SETCURSOR
        LDA #<str_kbd_title
        STA zp_str
        LDA #>str_kbd_title
        STA zp_str+1
        JSR print_zp_str

        ; Instructions
        LDX #5
        LDY #4
        JSR K_CON_SETCURSOR
        LDA #<str_kbd_hint
        STA zp_str
        LDA #>str_kbd_hint
        STA zp_str+1
        JSR print_zp_str

        ; First prompt
        LDX #5
        LDY #6
        JSR K_CON_SETCURSOR
        LDA #'>'
        JSR K_CON_PUTCHAR
        LDA #' '
        JSR K_CON_PUTCHAR

        ; Blinking cursor on
        LDA #$F6               ; flash + block + rate 15
        STA VID_CURSOR

        ; Timeout counter
        LDA #0
        STA zp_cnt
        STA zp_cnt+1

@kloop: ; Brief delay between polls
        LDA #4
        JSR delay

        JSR K_CON_GETKEY
        CMP #0
        BNE @kgot

        ; Timeout: auto-advance after ~12s
        INC zp_cnt
        BNE @kloop
        INC zp_cnt+1
        LDA zp_cnt+1
        CMP #$06
        BCC @kloop
        JMP @kdone

@kgot:  ; Reset timeout
        LDX #0
        STX zp_cnt
        STX zp_cnt+1

        CMP #KEY_ESCAPE
        BEQ @kdone

        CMP #KEY_ENTER
        BEQ @kenter

        ; Printable? ($20-$7E)
        CMP #$20
        BCC @kloop
        CMP #$7F
        BCS @kloop

        ; Echo character
        JSR K_CON_PUTCHAR
        JMP @kloop

@kenter:
        LDA #$00
        STA VID_CURSOR
        JSR K_CON_NEWLINE
        LDA #'>'
        JSR K_CON_PUTCHAR
        LDA #' '
        JSR K_CON_PUTCHAR
        LDA #$F6
        STA VID_CURSOR
        JMP @kloop

@kdone:
        LDA #$00
        STA VID_CURSOR
        JSR phase_pause

; ============================================================
; Phase 6: Waterfall — con_putchar, con_newline, auto-scroll
; Print numbered lines; lines 26+ scroll off the top.
; ============================================================

phase_waterfall:
        JSR K_CON_CLEAR

        LDX #0
        LDY #0
        JSR K_CON_SETCURSOR

        LDA #1
        STA zp_i               ; line number

@wloop: LDA zp_i
        JSR print_decimal
        LDA #'.'
        JSR K_CON_PUTCHAR
        LDA #' '
        JSR K_CON_PUTCHAR

        LDA #<str_waterfall
        STA zp_str
        LDA #>str_waterfall
        STA zp_str+1
        JSR print_zp_str

        JSR K_CON_NEWLINE

        LDA #DELAY_LINE
        JSR delay

        INC zp_i
        LDA zp_i
        CMP #36                ; 35 lines
        BCC @wloop

        JSR phase_pause

; ============================================================
; Phase 7: Finale — con_getstatus, scroll wipe, restart
; ============================================================

phase_finale:
        ; Wipe screen by scrolling up 25 times
        LDA #25
        STA zp_tmp
@wipe:  LDX #0
        LDY #$FF               ; up 1
        JSR K_CON_SCROLL
        LDA #25
        JSR delay
        DEC zp_tmp
        BNE @wipe

        ; Show cursor position via con_getstatus
        LDX #42
        LDY #10
        JSR K_CON_SETCURSOR
        JSR K_CON_GETSTATUS     ; A=status, X=col, Y=row
        STX zp_col
        STY zp_row

        ; Display position
        LDX #26
        LDY #10
        JSR K_CON_SETCURSOR
        LDA #<str_cursor
        STA zp_str
        LDA #>str_cursor
        STA zp_str+1
        JSR print_zp_str
        LDA zp_col
        JSR print_decimal
        LDA #','
        JSR K_CON_PUTCHAR
        LDA zp_row
        JSR print_decimal
        LDA #')'
        JSR K_CON_PUTCHAR

        ; Restart message
        LDX #27
        LDY #14
        JSR K_CON_SETCURSOR
        LDA #<str_finale
        STA zp_str
        LDA #>str_finale
        STA zp_str+1
        JSR type_zp_str

        JSR phase_pause
        JSR phase_pause

        JMP restart

; ============================================================
; Helper Subroutines
; ============================================================

; delay — busy-wait, A = delay units (~1.3ms each at 1MHz)
delay:
        TAX
        BEQ @done
@outer: LDY #$FF
@inner: DEY
        BNE @inner
        DEX
        BNE @outer
@done:  RTS

; phase_pause — ~2.6s inter-phase pause
phase_pause:
        LDA #PAUSE_REPS
        STA zp_tmp
@loop:  LDA #$FF
        JSR delay
        DEC zp_tmp
        BNE @loop
        RTS

; drain_keys — consume all pending keys
drain_keys:
        JSR K_CON_GETKEY
        CMP #0
        BNE drain_keys
        RTS

; type_zp_str — type null-terminated string at (zp_str) with delay
type_zp_str:
        LDY #0
@loop:  LDA (zp_str),Y
        BEQ @done
        STY zp_tmp
        JSR K_CON_PUTCHAR
        LDA #DELAY_TYPE
        JSR delay
        LDY zp_tmp
        INY
        BNE @loop
@done:  RTS

; print_zp_str — print null-terminated string at (zp_str) instantly
print_zp_str:
        LDY #0
@loop:  LDA (zp_str),Y
        BEQ @done
        STY zp_tmp
        JSR K_CON_PUTCHAR
        LDY zp_tmp
        INY
        BNE @loop
@done:  RTS

; print_decimal — print A as 2-digit decimal (00-99)
print_decimal:
        LDX #0
@tens:  CMP #10
        BCC @ones
        SEC
        SBC #10
        INX
        JMP @tens
@ones:  PHA
        TXA
        CLC
        ADC #'0'
        JSR K_CON_PUTCHAR
        PLA
        CLC
        ADC #'0'
        JSR K_CON_PUTCHAR
        RTS

; ============================================================
; String Data
; ============================================================

.segment "RODATA"

str_title:     .byte "N8 CONSOLE DEMO", 0
str_subtitle:  .byte "Kernel API Showcase", 0
str_diamond:   .byte "con_movcursor", 0
str_kbd_title: .byte "KEYBOARD INPUT", 0
str_kbd_hint:  .byte "Type text. Enter=newline  Esc=next", 0
str_waterfall: .byte "N8 Machine console output line", 0
str_cursor:    .byte "con_getstatus -> (", 0
str_finale:    .byte "[ Restarting demo... ]", 0
