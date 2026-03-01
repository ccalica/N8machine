; -------------------------------------------------------
; monitor.s -- Interactive echo shell at $E000
; -------------------------------------------------------
; Uses kernel entry points (kentry.inc) — no direct imports.

.include "kentry.inc"

.segment "RODATA"
banner:  .byte "N8 Monitor v1.0",13,10,0

.segment "CODE"

.export _monitor

_monitor:
        ; --- Print banner ---
        LDA #<banner
        STA $E0             ; ZP_A_PTR low
        LDA #>banner
        STA $E1             ; ZP_A_PTR high
        LDY #$00
        LDA ($E0),Y
        BEQ echo_loop
@puts:  JSR K_TTY_PUTC
        INY
        LDA ($E0),Y
        BNE @puts

        ; --- Echo loop ---
echo_loop:
        JSR K_TTY_GETC
        BEQ echo_loop
        JSR K_TTY_PUTC
        CMP #$0D            ; '\r'
        BNE echo_loop
        LDA #$0A            ; '\n'
        JSR K_TTY_PUTC
        JMP echo_loop
