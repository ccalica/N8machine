; ----------------------------------------------------------------
; 

.export   _init, spin
.import   _main, str

.include  "zeropage.inc"
.include  "devices.inc"

; ---------------------------------------------------------------------------
; Place the startup code in a special segment

.segment  "STARTUP"

; ---------------------------------------------------------------------------
; A little light 6502 housekeeping

_init:  LDX     #$FF                 ; Initialize stack pointer to $01FF
        TXS
        CLD                          ; Clear decimal mode
        JSR    _main

; Should really terminate gracefully at this point
spin:   JMP spin
other:  lda str
        JMP spin