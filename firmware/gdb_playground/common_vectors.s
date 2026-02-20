; common_vectors.s - interrupt vector table for playground test programs

.import   _init

.segment  "VECTORS"

.addr     nmi_stub           ; NMI vector
.addr     _init              ; Reset vector
.addr     irq_stub           ; IRQ/BRK vector

.segment  "CODE"

nmi_stub: RTI
irq_stub: RTI
