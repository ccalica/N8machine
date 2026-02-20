; test_regs.s - Register read/write test
;
; Loads known values into all registers in sequence with NOP padding.
; Good for testing: regs, wreg, step, pc
;
; Expected workflow with n8gdb:
;   sym test_regs.sym
;   bp load_a          ; break at first load
;   run                ; hit breakpoint
;   regs               ; verify A=?? (before load)
;   step               ; execute LDA #$AA
;   regs               ; verify A=$AA
;   wreg a 55          ; override A to $55
;   step               ; continue stepping...

.export   _main
.export   load_a, load_x, load_y, set_flags, transfers, final_state, done

.segment  "CODE"

_main:

; --- Load A with known values ---
load_a:
        LDA #$AA
        NOP
        NOP

; --- Load X with known value ---
load_x:
        LDX #$BB
        NOP
        NOP

; --- Load Y with known value ---
load_y:
        LDY #$CC
        NOP
        NOP

; --- Test flag manipulation ---
set_flags:
        SEC                  ; Set carry
        SED                  ; Set decimal
        NOP
        CLC                  ; Clear carry
        CLD                  ; Clear decimal
        NOP

; --- Transfer between registers ---
transfers:
        LDA #$11
        TAX                  ; A -> X, now X=$11
        LDA #$22
        TAY                  ; A -> Y, now Y=$22
        TXA                  ; X -> A, now A=$11
        NOP

; --- All registers with distinct values ---
final_state:
        LDA #$DE
        LDX #$AD
        LDY #$42
        NOP                  ; Break here to verify: A=$DE X=$AD Y=$42

done:   JMP _main            ; Loop forever
