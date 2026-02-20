; test_breakpoints.s - Breakpoint set/clear test
;
; Multiple labeled subroutines called in a loop.
; Good for testing: bp, bpc, run, step, goto
;
; Expected workflow with n8gdb:
;   sym test_breakpoints.sym
;   bp func_a             ; set breakpoint on func_a
;   bp func_c             ; set breakpoint on func_c
;   run                   ; should stop at func_a
;   regs                  ; verify PC at func_a
;   run                   ; should stop at func_c (skipping func_b)
;   bpc func_a            ; clear func_a breakpoint
;   run                   ; should stop at func_c again (func_a no longer triggers)
;   bp loop_top           ; break at top of loop
;   run                   ; stops at loop_top on next iteration

.export   _main
.export   loop_top, func_a, func_b, func_c, func_d, counter

.segment  "BSS"

counter: .res 1             ; iteration counter

.segment  "CODE"

_main:
        LDA #$00
        STA counter

loop_top:
        INC counter
        JSR func_a
        JSR func_b
        JSR func_c
        JSR func_d
        JMP loop_top

; --- Four distinct subroutines ---

func_a:
        LDA #$0A            ; A = $0A
        LDX counter
        NOP
        RTS

func_b:
        LDA #$0B            ; A = $0B
        LDY counter
        NOP
        RTS

func_c:
        LDA #$0C            ; A = $0C
        TAX                  ; X = $0C
        TAY                  ; Y = $0C
        NOP
        RTS

func_d:
        LDA counter
        ASL A                ; A = counter * 2
        NOP
        RTS
