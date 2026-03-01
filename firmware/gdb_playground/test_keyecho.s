; test_keyecho.s - Key echo monitor for non-display keys
;
; Echoes all keystrokes with visual feedback:
;   Printable ASCII ($20-$7E) without modifiers: echo raw character
;   Enter ($0D) without modifiers: new line + prompt
;   Backspace ($08) without modifiers: erase previous character
;   All other keys or any key with modifiers: display [TAG]
;     e.g. [UP] [F1] [C-a] [S-ENTER] [A-TAB]
;
; Tests the new keycode mapping:
;   Nav keys $01-$1B, function keys $80-$8B,
;   modifier bits in KBD_STATUS.
;
; n8gdb workflow:
;   load test_keyecho 0xE000
;   reset
;   bp key_handled
;   run                          -- waits for first key
;   kbd_inject '[up]'            -- screen shows [UP]
;   run
;   kbd_inject '[f1]'            -- screen shows [F1]
;   run
;   kbd_inject 'A --ctrl'        -- screen shows [C-A]
;   run
;   kbd_inject 'hello[enter]'    -- screen shows hello + new line

.export   _main
.export   key_handled, show_tag, print_key_name
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

; Key codes (matching n8_memory_map.h)
KEY_UP      = $01
KEY_DOWN    = $02
KEY_LEFT    = $03
KEY_RIGHT   = $04
KEY_HOME    = $05
KEY_END     = $06
KEY_BS      = $08
KEY_TAB     = $09
KEY_PGUP    = $0A
KEY_PGDN    = $0B
KEY_ENTER   = $0D
KEY_INS     = $0E
KEY_DEL     = $0F
KEY_PRTSC   = $10
KEY_PAUSE   = $11
KEY_ESC     = $1B
KEY_F1      = $80

; KBD_STATUS modifier bits
MOD_SHIFT   = $04
MOD_CTRL    = $08
MOD_ALT     = $10
MOD_MASK    = $3C           ; SHIFT|CTRL|ALT|CAPS

; Cursor: flash + block + rate 15
CURSOR_STYLE = $F6

; --- Zero page variables ---
.segment "ZEROPAGE"
zp_col:   .res 1
zp_row:   .res 1
zp_fb:    .res 2
zp_str:   .res 2
zp_tmp:   .res 1
zp_key:   .res 1
zp_mods:  .res 1

.segment "CODE"

; ============================================================
; Entry point
; ============================================================

_main:
        JSR clear_screen

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

; ============================================================
; Main loop
; ============================================================

main_loop:
        JSR kbd_wait

        ; Read modifier bits before ACK
        LDA KBD_STATUS
        AND #MOD_MASK
        STA zp_mods

        ; Read keycode + ACK
        JSR kbd_read
        STA zp_key

        ; Modifiers present -> always show tag
        LDA zp_mods
        BNE show_tag

        ; No modifiers -- dispatch plain key
        LDA zp_key

        CMP #KEY_ENTER
        BEQ do_enter

        CMP #KEY_BS
        BEQ do_backspace

        ; Printable ASCII?
        CMP #$20
        BCC show_tag           ; < $20 -> non-printable -> tag
        CMP #$7F
        BCS show_tag           ; >= $7F -> function key etc -> tag

        ; Echo raw printable character
        PHA
        JSR cursor_off
        PLA
        JSR put_char
        JSR cursor_on
        JMP key_handled

do_enter:
        JSR cursor_off
        JSR new_line
        LDA #<str_prompt
        STA zp_str
        LDA #>str_prompt
        STA zp_str+1
        JSR print_str
        JSR cursor_on
        JMP key_handled

do_backspace:
        LDA zp_col
        BEQ key_handled        ; at column 0, nothing to erase
        JSR cursor_off
        DEC zp_col
        LDA #$20
        JSR put_char_at_cur
        DEC zp_col
        JSR cursor_on
        JMP key_handled

; --- Display [MOD+NAME] tag ---
show_tag:
        JSR cursor_off
        LDA #'['
        JSR put_char

        ; Modifier prefixes
        LDA zp_mods
        AND #MOD_CTRL
        BEQ @no_c
        LDA #'C'
        JSR put_char
        LDA #'-'
        JSR put_char
@no_c:
        LDA zp_mods
        AND #MOD_SHIFT
        BEQ @no_s
        LDA #'S'
        JSR put_char
        LDA #'-'
        JSR put_char
@no_s:
        LDA zp_mods
        AND #MOD_ALT
        BEQ @no_a
        LDA #'A'
        JSR put_char
        LDA #'-'
        JSR put_char
@no_a:
        ; Print key name
        LDA zp_key
        JSR print_key_name

        LDA #']'
        JSR put_char
        JSR cursor_on
        ; Fall through to key_handled

key_handled:
        JMP main_loop

; ============================================================
; print_key_name -- print name for keycode in A
; ============================================================

print_key_name:
        ; Nav range ($00-$1B)?
        CMP #$1C
        BCS @not_nav

        ; Table lookup
        TAX
        LDA nav_name_lo,X
        STA zp_str
        LDA nav_name_hi,X
        STA zp_str+1
        ORA zp_str
        BEQ @unknown
        JMP print_str          ; tail call

@not_nav:
        ; Printable ASCII ($20-$7E)?
        CMP #$20
        BCC @unknown
        CMP #$7F
        BCS @check_fn
        JMP put_char           ; tail call -- print the character

@check_fn:
        ; Function key ($80-$8B)?
        CMP #KEY_F1
        BCC @unknown
        CMP #KEY_F1 + 12
        BCS @unknown
        JMP print_fn_name      ; tail call

@unknown:
        ; Print "$XX"
        PHA
        LDA #'$'
        JSR put_char
        PLA
        JMP print_hex_byte     ; tail call

; --- Print function key name F1-F12 ---
; A = $80-$8B
print_fn_name:
        SEC
        SBC #KEY_F1            ; A = 0-11
        CMP #9
        BCS @two
        ; F1-F9: single digit
        PHA
        LDA #'F'
        JSR put_char
        PLA
        CLC
        ADC #'1'
        JMP put_char           ; tail call

@two:   ; F10-F12
        PHA
        LDA #'F'
        JSR put_char
        LDA #'1'
        JSR put_char
        PLA
        SEC
        SBC #9                 ; 9->0, 10->1, 11->2
        CLC
        ADC #'0'
        JMP put_char           ; tail call

; --- Print A as two hex digits ---
print_hex_byte:
        PHA
        LSR A
        LSR A
        LSR A
        LSR A
        JSR print_hex_nib
        PLA
        AND #$0F
print_hex_nib:
        CMP #$0A
        BCS @letter
        CLC
        ADC #'0'
        JMP put_char
@letter:
        CLC
        ADC #('A' - $0A)
        JMP put_char

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

; --- Put character A at cursor, advance ---
put_char:
        PHA
        JSR calc_fb_addr
        PLA
        LDY #$00
        STA (zp_fb),Y

        INC zp_col
        LDA zp_col
        CMP #80
        BCC @upd
        LDA #$00
        STA zp_col
        JSR advance_row
@upd:   LDA zp_col
        STA VID_CURCOL
        LDA zp_row
        STA VID_CURROW
        RTS

; --- Put char at cursor, no row advance (for backspace erase) ---
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

        LDA zp_fb+1
        PHA
        LDA zp_fb
        PHA

        ASL zp_fb
        ROL zp_fb+1
        ASL zp_fb
        ROL zp_fb+1              ; zp_fb = row*64

        PLA
        CLC
        ADC zp_fb
        STA zp_fb
        PLA
        ADC zp_fb+1
        STA zp_fb+1              ; zp_fb = row*80

        CLC
        LDA zp_fb
        ADC zp_col
        STA zp_fb
        LDA zp_fb+1
        ADC #$00
        STA zp_fb+1

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

; --- Advance row, scroll if needed ---
advance_row:
        INC zp_row
        LDA zp_row
        CMP #25
        BCC @done
        JSR scroll_up
        LDA #24
        STA zp_row
@done:  RTS

; --- Scroll up ---
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
        LDX #$08
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

; --- Read key + ACK ---
kbd_read:
        LDA KBD_DATA
        PHA
        LDA #$01
        STA KBD_ACK
        PLA
        RTS

; ============================================================
; Data
; ============================================================

.segment "RODATA"

str_banner: .byte "N8 Key Echo Monitor", 0
str_prompt: .byte "> ", 0

; Nav key names
str_up:    .byte "UP", 0
str_down:  .byte "DOWN", 0
str_left:  .byte "LEFT", 0
str_right: .byte "RIGHT", 0
str_home:  .byte "HOME", 0
str_end:   .byte "END", 0
str_bs:    .byte "BS", 0
str_tab:   .byte "TAB", 0
str_pgup:  .byte "PGUP", 0
str_pgdn:  .byte "PGDN", 0
str_enter: .byte "ENTER", 0
str_ins:   .byte "INS", 0
str_del:   .byte "DEL", 0
str_prtsc: .byte "PRTSC", 0
str_pause: .byte "PAUSE", 0
str_esc:   .byte "ESC", 0

; Nav key name pointer tables (indexed by keycode $00-$1B)
nav_name_lo:
        .byte 0              ; $00 NONE
        .byte <str_up        ; $01
        .byte <str_down      ; $02
        .byte <str_left      ; $03
        .byte <str_right     ; $04
        .byte <str_home      ; $05
        .byte <str_end       ; $06
        .byte 0              ; $07 reserved
        .byte <str_bs        ; $08
        .byte <str_tab       ; $09
        .byte <str_pgup      ; $0A
        .byte <str_pgdn      ; $0B
        .byte 0              ; $0C reserved
        .byte <str_enter     ; $0D
        .byte <str_ins       ; $0E
        .byte <str_del       ; $0F
        .byte <str_prtsc     ; $10
        .byte <str_pause     ; $11
        .byte 0,0,0,0,0,0,0,0,0 ; $12-$1A reserved
        .byte <str_esc       ; $1B

nav_name_hi:
        .byte 0              ; $00 NONE
        .byte >str_up        ; $01
        .byte >str_down      ; $02
        .byte >str_left      ; $03
        .byte >str_right     ; $04
        .byte >str_home      ; $05
        .byte >str_end       ; $06
        .byte 0              ; $07 reserved
        .byte >str_bs        ; $08
        .byte >str_tab       ; $09
        .byte >str_pgup      ; $0A
        .byte >str_pgdn      ; $0B
        .byte 0              ; $0C reserved
        .byte >str_enter     ; $0D
        .byte >str_ins       ; $0E
        .byte >str_del       ; $0F
        .byte >str_prtsc     ; $10
        .byte >str_pause     ; $11
        .byte 0,0,0,0,0,0,0,0,0 ; $12-$1A reserved
        .byte >str_esc       ; $1B
