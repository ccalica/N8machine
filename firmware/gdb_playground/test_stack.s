; test_stack.s - Stack operation test
;
; Push/pop sequences and nested JSR/RTS. Watch SP change and read stack memory.
; Good for testing: regs (SP tracking), read $0100 (stack page), step through JSR/RTS
;
; Expected workflow with n8gdb:
;   sym test_stack.sym
;   bp push_start
;   run                   ; stop before pushes
;   regs                  ; note SP value ($FF)
;   step 3                ; execute 3 pushes
;   regs                  ; SP should be $FC
;   read 01FD 3           ; read pushed values from stack: $11 $22 $33
;   bp depth_3
;   run                   ; let it nest 3 deep
;   regs                  ; SP reflects 3 JSR frames (6 bytes of return addrs)
;   step                  ; step through RTS chain

.export   _main
.export   push_start, pop_start, flag_test, nest_start, depth_1, depth_2, depth_3, done

.segment  "CODE"

_main:

; --- Push known values onto stack ---
push_start:
        LDA #$11
        PHA
        LDA #$22
        PHA
        LDA #$33
        PHA

; --- Pop them back (should come in reverse) ---
pop_start:
        PLA                  ; A = $33
        NOP
        PLA                  ; A = $22
        NOP
        PLA                  ; A = $11
        NOP

; --- Push/pop flags ---
flag_test:
        SEC                  ; Set carry
        PHP                  ; Push processor status
        CLC                  ; Clear carry
        PLP                  ; Pull status — carry should be set again
        NOP

; --- Nested subroutine calls (3 deep) ---
nest_start:
        JSR depth_1
        NOP

done:   JMP _main            ; Loop

; --- Nested call chain ---
depth_1:
        LDA #$01
        JSR depth_2
        RTS

depth_2:
        LDA #$02
        JSR depth_3
        RTS

depth_3:
        LDA #$03             ; Deepest point: 3 frames on stack
        NOP
        RTS
