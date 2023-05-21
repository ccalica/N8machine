
; ----------------------------------------------------------------
; 

.export   _main
.export   str

.include  "zeropage.inc"
.include  "devices.inc"

str:    .byte "This is null terminated."
_main:  LDX #$00
        LDA #$02
        STA TXT_BUFF,X
        LDA #$05
        INX
        STA TXT_BUFF,X
        LDA #$08
        INX
        STA TXT_BUFF,X
        RTS