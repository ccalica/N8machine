; test_counter.s - Running loop / halt test
;
; A tight counting loop. Let it run, use halt to interrupt, inspect counter.
; Good for testing: run, halt, read (counter in RAM), reset
;
; Expected workflow with n8gdb:
;   sym test_counter.sym
;   run                   ; let it run freely
;   (wait a moment)
;   halt                  ; interrupt execution
;   regs                  ; see where PC stopped (somewhere in count_loop)
;   read 0200 3           ; read counter bytes: lo, hi, overflow
;   step 5                ; step a few iterations
;   read 0200 3           ; counter should have advanced
;   reset                 ; reset CPU
;   regs                  ; PC should be at _init (reset vector)
;   bp overflow_hit
;   run                   ; run until 16-bit counter overflows

.export   _main
.export   count_loop, overflow_hit, counter_lo, counter_hi, overflow

.segment  "BSS"

counter_lo: .res 1
counter_hi: .res 1
overflow:   .res 1           ; increments when hi wraps

.segment  "CODE"

_main:
        LDA #$00
        STA counter_lo
        STA counter_hi
        STA overflow

count_loop:
        INC counter_lo
        BNE count_loop       ; inner loop: 256 iterations

        INC counter_hi       ; bump high byte
        BNE count_loop       ; outer loop: 256 * 256

overflow_hit:
        INC overflow         ; 64K iterations completed
        JMP count_loop       ; keep going
