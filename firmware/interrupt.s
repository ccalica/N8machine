; -------------------------------------------------------
; interrupt.s -- IRQ/NMI/BRK handlers
; -------------------------------------------------------

.export     _irq_int, _nmi_int, brken, irq
.import     tty_recv

.include    "devices.inc"

.segment    "CODE"

; -------------------------------------------------------
; NMI routine

_nmi_int:   RTI

; -------------------------------------------------------
; Maskable interrupt (IRQ) service routine

_irq_int:   PHA
            TXA
            PHA
            TYA
            PHA
            TSX
            LDA $0104,X         ; saved P register on stack
            AND #$10            ; B flag set?
            BNE brken           ; yes — BRK instruction

; -------------------------------------------------------
; Drain TTY receive FIFO into firmware ring buffer
irq:        LDA N8_TTY_IN_STATUS
            AND #$01
            BEQ irq_rtn         ; no more chars
            LDA N8_TTY_IN_DATA
            JSR tty_recv
            JMP irq

irq_rtn:    PLA
            TAY
            PLA
            TAX
            PLA
            RTI

; -------------------------------------------------------
; BRK — spin forever (unrecoverable)

brken:      JMP brken
