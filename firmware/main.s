
; ----------------------------------------------------------------
; 

.export   _main
.export   str
.import _tty_putc

.include  "zeropage.inc"
.include  "devices.inc"

str:    .byte "This is null terminated."
.byte 13
.byte 10
.byte 0
_main:  LDX #$00
        LDA #$02
        STA TXT_BUFF,X
        LDA #$05
        INX
        STA TXT_BUFF,X
        LDA #$08
        INX
        STA TXT_BUFF,X
tty_test:
        LDX #$00
        LDA str,X
        BEQ @done
@loop:  JSR _tty_putc
        INX
        LDA str,X
        BNE @loop
@done:  RTS
