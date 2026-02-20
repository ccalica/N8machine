; common_init.s - minimal startup for playground test programs
; No cc65 runtime. Just stack init and jump to _main.

.export   _init, _exit

.import   _main

.segment  "CODE"

_init:  LDX #$FF            ; Stack pointer to $01FF
        TXS
        CLD                  ; Clear decimal mode
        SEI                  ; Disable IRQs (tests don't need them)
        JMP _main

_exit:  JMP _exit            ; Spin forever on exit
