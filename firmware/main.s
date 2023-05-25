
; ----------------------------------------------------------------
; 

.export   _main
.export   strA, strB
.import _tty_putc,_tty_puts

.include  "zeropage.inc"
.include  "devices.inc"

strA:    .byte "Welcome banner.  Enjoy this world.",13,10,0
strB:    .byte "PLAY?",13,10,0
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
        LDA #<strA
        LDX #>strA
        JSR _tty_puts
        LDA #<strB
        LDX #>strB
        JSR _tty_puts
rb_test:


        RTS

