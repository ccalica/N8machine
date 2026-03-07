; -------------------------------------------------------
; shell.s -- Interactive echo shell at $E000
; -------------------------------------------------------
; Uses console kernel entry points for keyboard input and
; video output.  Printable ASCII is echoed to screen;
; Enter produces a newline, Backspace erases one character.
; All other keys (arrows, F-keys, etc.) are ignored.

.include "kentry.inc"
.include "n8_memory_map.inc"

.segment "RODATA"
banner:  .byte "N8 Shell v0.0.2",0

.segment "CODE"

.export _shell

_shell:
        ; --- Init: clear screen, enable blinking underline cursor ---
        JSR K_CON_CLEAR
        LDA #(N8_VID_CURSOR_FLASH | N8_VID_CURSOR_UNDERLINE | $20)
        STA N8_VID_CURSOR       ; flash rate = 2 ($20), underline, blink

        ; --- Print banner ---
        LDA #<banner
        STA $E0                 ; ZP_A_PTR low
        LDA #>banner
        STA $E1                 ; ZP_A_PTR high
        LDY #$00
        LDA ($E0),Y
        BEQ @banner_done
@puts:  JSR K_CON_PUTCHAR
        INY
        LDA ($E0),Y
        BNE @puts
@banner_done:
        JSR K_CON_NEWLINE

        ; --- Echo loop ---
echo_loop:
        JSR K_CON_GETKEY
        CMP #$00
        BEQ echo_loop           ; no key available

        ; Enter → newline
        CMP #N8_KEY_ENTER
        BNE @not_enter
        JSR K_CON_NEWLINE
        JMP echo_loop
@not_enter:

        ; Backspace → erase one character
        CMP #N8_KEY_BACKSPACE
        BNE @not_bs
        LDA #N8_VIDOP_CURSOR_LEFT
        STA N8_VID_OPER
        LDA #' '
        JSR K_CON_PUTCHAR       ; overwrite with space (cursor advances)
        LDA #N8_VIDOP_CURSOR_LEFT
        STA N8_VID_OPER         ; back up again
        JMP echo_loop
@not_bs:

        ; Ignore non-printable: < $20 or >= $80
        CMP #$20
        BCC echo_loop           ; control codes → ignore
        CMP #$80
        BCS echo_loop           ; F-keys etc. → ignore

        ; Printable ASCII → echo
        JSR K_CON_PUTCHAR
        JMP echo_loop
