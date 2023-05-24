; -------------------------------------------------------
; interrupt.s
; -------------------------------------------------------
;
; Interrupt handler.

.export     _irq_int, _nmi_int, brken
.import     _tty_putc

.include    "devices.inc"

.segment    "CODE"

; -------------------------------------------------------
; NMI routine

_nmi_int:   RTI                      ; Return from all NMI interrupts

; -------------------------------------------------------
; Maskable interrupt (IRQ) service routine

_irq_int:   PHA                 ; Push A
            TXA                 ; Save X to stack
            PHA
            TYA                 ; Save Y to stack
            PHA
            TSX                 ; Transfer stack pointer to X
            LDA $104,X         ; Load prev status (STACK PAGE + 4)
            AND #$10            ; Get just B bit
            BNE brken           ; BRK op executed

; -------------------------------------------------------
; Handle interrupts here
irq:        LDA TTY_IN_CTRL     ; check for tty char
            STA TXT_BUFF
            AND #$01
            BEQ irq_rtn
            LDA TTY_IN_DATA     ; load the char
            STA TXT_BUFF+1
            JSR _tty_putc
            CMP #$0D            ; '\r'
            BNE irq
            LDA #$0A            ; '\n'
            JSR _tty_putc
            JMP irq

irq_rtn:    PLA                 ; Load Y from stack
            TAY
            PLA                 ; Load X from stack
            TAX
            PLA                 ; Pop A
            RTI                 ; Return

; ---------------------------------------------------------------------------
; BRK oops

brken:      JMP brken           ; So very very very broken
