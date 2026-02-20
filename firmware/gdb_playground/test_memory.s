; test_memory.s - Memory read/write test
;
; Writes known patterns to RAM regions. Verifiable with n8gdb read/write.
; Good for testing: read, write, load, step
;
; Expected workflow with n8gdb:
;   sym test_memory.sym
;   bp fill_done
;   run                   ; let it fill memory
;   read 0300 40          ; verify ascending pattern at $0300
;   read 0400 10          ; verify $AA pattern at $0400
;   write 0300 DEADBEEF   ; overwrite 4 bytes at $0300
;   read 0300 10          ; verify the write
;   read 0500 20          ; verify checkerboard at $0500

.export   _main
.export   fill_ascending, fill_aa, fill_checker, fill_markers, fill_done, verify_loop

.segment  "CODE"

_main:

; --- Fill $0300-$033F with ascending values (0,1,2,...63) ---
fill_ascending:
        LDX #$00
@loop:  TXA
        STA $0300,X
        INX
        CPX #$40
        BNE @loop

; --- Fill $0400-$040F with $AA ---
fill_aa:
        LDA #$AA
        LDX #$00
@loop:  STA $0400,X
        INX
        CPX #$10
        BNE @loop

; --- Fill $0500-$051F with checkerboard ($55/$AA) ---
fill_checker:
        LDX #$00
@loop:  TXA
        AND #$01
        BEQ @even
        LDA #$AA
        JMP @store
@even:  LDA #$55
@store: STA $0500,X
        INX
        CPX #$20
        BNE @loop

; --- Write known bytes to specific addresses ---
fill_markers:
        LDA #$DE
        STA $0600
        LDA #$AD
        STA $0601
        LDA #$BE
        STA $0602
        LDA #$EF
        STA $0603

fill_done:
        NOP                  ; Break here — all patterns written

; --- Verify loop: re-read and compare (keeps CPU busy) ---
verify_loop:
        LDA $0300            ; should be $00
        LDA $0301            ; should be $01
        LDA $0400            ; should be $AA
        LDA $0500            ; should be $55
        LDA $0501            ; should be $AA
        LDA $0600            ; should be $DE
        JMP verify_loop
