
; ----------------------------------------------------------------
; 

.export   _main
.export   str
.import _tty_putc,_tty_puts

.include  "zeropage.inc"
.include  "devices.inc"

str:    .byte "This is null terminated.",13,10,0

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
        LDA #<str
        LDX #>str
        JSR _tty_puts
        RTS
