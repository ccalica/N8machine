
; ----------------------------------------------------------------
; 

.export   _main
.export   strA, strB, tty_test, rb_test
.import _tty_putc,_tty_puts,_tty_getc,_tty_peekc

.include  "zeropage.inc"
.include  "devices.inc"

.data
strA:    .byte "Welcome banner.  Enjoy this world.",13,10,0
strB:    .byte "PLAY?",13,10,0

.code
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

        ; --------------------------------------------
        ; Set up echo using our buffer and "BIOS" routines
        ; =============================================
rb_test:
        JSR _tty_getc
        BEQ rb_test
        JSR _tty_putc
        CMP #$0D            ; '\r'
        BNE rb_test
        LDA #$0A            ; '\n'
        JSR _tty_putc
        JMP rb_test


        RTS

