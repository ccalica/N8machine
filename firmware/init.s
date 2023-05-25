; ----------------------------------------------------------------
; 

.export   _init, _exit, spin
.import   _main

.export   __STARTUP__ : absolute = 1        ; mark as startup
.import   __RAM_START__, __RAM_SIZE__       ; Linker generated

.import   zerobss, copydata
.import   initlib, donelib
.import   strA


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

; ----------------------------------------------------------------------------
; Setup cc65 argument stack
        LDA     #<(__RAM_START__ + __RAM_SIZE__)
        STA     sp
        LDA     #>(__RAM_START__ + __RAM_SIZE__)
        STA     sp+1

; ===============================================================================
; Init memory storage
        JSR zerobss
        JSR copydata
        JSR initlib
        CLI                          ; Clear IRQ disable
        JSR    _main

_exit:  JSR donelib

; Should really terminate gracefully at this point
spin:   JMP spin
other:  lda strA
        JMP spin