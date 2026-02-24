; test_benchmark.s - 6502 Benchmark
;
; Measures CPU cycles per frame using two methods:
;   A) Count iterations completed in 4 VSync frames
;   B) Count VSync frames needed for 131072 iterations (512 batches)
;
; 10 test categories covering core 6502 instruction groups.
; Displays results on 80x25 frame buffer with derived MHz.
;
; Expected workflow with n8gdb:
;   load test_benchmark 0xE000
;   reset
;   run
;   (results appear on Screen window)

.export   _main

; ============================================================
; Hardware registers
; ============================================================
VID_VSYNC  = $D848
VID_OPER   = $D844
FB_BASE    = $C000

VIDOP_SCROLL_UP = $01

; ============================================================
; Zero page allocation
; ============================================================
.segment "ZEROPAGE"
zp_col:      .res 1           ; $00 - cursor column
zp_row:      .res 1           ; $01 - cursor row
zp_fb:       .res 2           ; $02-03 - frame buffer pointer
zp_str:      .res 2           ; $04-05 - string pointer
last_vsync:  .res 1           ; $06 - last VSync value
test_idx:    .res 1           ; $07 - current test index (0-9)
frame_count: .res 1           ; $08 - frames counted (method B)
batch_count: .res 2           ; $09-0A - batches counted (16-bit)
iter:        .res 3           ; $0B-0D - iteration count (24-bit)
cpf:         .res 3           ; $0E-10 - cycles/frame result (24-bit)
dividend:    .res 4           ; $11-14 - 32-bit dividend
divisor:     .res 2           ; $15-16 - 16-bit divisor
quotient:    .res 4           ; $17-1A - 32-bit quotient
remainder:   .res 2           ; $1B-1C - 16-bit remainder
print_buf:   .res 10          ; $1D-26 - decimal print buffer
print_len:   .res 1           ; $27 - digits in print buffer
target:      .res 2           ; $28-29 - target batch count (method B)
ind_ptr:     .res 2           ; $2A-2B - indirect pointer for test 8
tmp1:        .res 1           ; $2C
tmp2:        .res 1           ; $2D
tmp3:        .res 1           ; $2E
tmp4:        .res 1           ; $2F
test_body:   .res 2           ; $30-31 - pointer to current batch routine

; ============================================================
; BSS — absolute-addressed test targets
; ============================================================
.segment "BSS"
abs_target:  .res 1           ; target for absolute addressing tests
zp_target    = $E0            ; ZP location for ZP tests (firmware ZP area)
arith_lo:    .res 1           ; 16-bit arith operand low
arith_hi:    .res 1           ; 16-bit arith operand high
zp_loop      = $E1            ; ZP loop counter for register test

; ============================================================
; CODE
; ============================================================
.segment "CODE"

; ============================================================
; Main entry point
; ============================================================
_main:
        JSR clear_screen

        ; --- Title ---
        LDA #0
        STA zp_col
        STA zp_row
        LDA #<str_title
        STA zp_str
        LDA #>str_title
        STA zp_str+1
        JSR print_str

        ; --- Calibration: Method A on NOP test ---
        ; Set row for "Cycles/frame" line
        LDA #2
        STA zp_row
        LDA #0
        STA zp_col

        ; Print "  Cycles/frame: "
        LDA #<str_cpf_label
        STA zp_str
        LDA #>str_cpf_label
        STA zp_str+1
        JSR print_str

        ; Run Method A on test 0 (NOP) for calibration
        LDA #<batch_nop
        STA test_body
        LDA #>batch_nop
        STA test_body+1
        LDA #7                  ; cycles per iteration for NOP
        JSR method_a

        ; cpf = iter * 7 / 4 (already computed in method_a result)
        ; method_a stores cpf in cpf (3 bytes)

        ; Print calibration CPF
        LDA cpf
        STA dividend
        LDA cpf+1
        STA dividend+1
        LDA cpf+2
        STA dividend+2
        LDA #0
        STA dividend+3
        JSR print_dec24

        ; Print "    ~"
        LDA #<str_mhz_mid
        STA zp_str
        LDA #>str_mhz_mid
        STA zp_str+1
        JSR print_str

        ; MHz = cpf / 13000
        ; cpf is 24-bit, 13000 is 16-bit
        LDA cpf
        STA dividend
        LDA cpf+1
        STA dividend+1
        LDA cpf+2
        STA dividend+2
        LDA #0
        STA dividend+3
        LDA #<13000
        STA divisor
        LDA #>13000
        STA divisor+1
        JSR div32x16

        ; quotient is integer MHz; remainder/13000 gives decimal
        ; Print integer part
        LDA quotient
        STA dividend
        LDA quotient+1
        STA dividend+1
        LDA #0
        STA dividend+2
        STA dividend+3
        JSR print_dec24

        ; Print "."
        LDA #'.'
        JSR put_char

        ; Fractional: remainder * 10 / 13000
        ; remainder is 16-bit, * 10 fits 24-bit
        LDA remainder
        STA dividend
        LDA remainder+1
        STA dividend+1
        LDA #0
        STA dividend+2
        STA dividend+3
        ; multiply by 10: shift left 3 + shift left 1
        ; simpler: just add 10 times... or use mul
        ; dividend = remainder * 10
        JSR mul_by_10
        ; Now divide by 13000
        LDA #<13000
        STA divisor
        LDA #>13000
        STA divisor+1
        JSR div32x16
        ; Print single digit
        LDA quotient
        CLC
        ADC #'0'
        JSR put_char

        ; Print " MHz  (13ms/frame)"
        LDA #<str_mhz_tail
        STA zp_str
        LDA #>str_mhz_tail
        STA zp_str+1
        JSR print_str

        ; --- Table header ---
        LDA #4
        STA zp_row
        LDA #0
        STA zp_col
        LDA #<str_hdr1
        STA zp_str
        LDA #>str_hdr1
        STA zp_str+1
        JSR print_str

        LDA #5
        STA zp_row
        LDA #0
        STA zp_col
        LDA #<str_hdr2
        STA zp_str
        LDA #>str_hdr2
        STA zp_str+1
        JSR print_str

        ; --- Run all 10 tests ---
        LDA #0
        STA test_idx

test_loop:
        ; Set output row = test_idx + 6
        LDA test_idx
        CLC
        ADC #6
        STA zp_row
        LDA #0
        STA zp_col

        ; Print "  " + test name
        LDA #' '
        JSR put_char
        LDA #' '
        JSR put_char

        ; Print test name from name table
        LDA test_idx
        ASL A                   ; *2 for pointer table
        TAX
        LDA tbl_names,X
        STA zp_str
        LDA tbl_names+1,X
        STA zp_str+1
        JSR print_str

        ; Pad to column 22 (for C/I field)
        LDA #22
        STA zp_col

        ; Print cycles/iter from table
        LDX test_idx
        LDA tbl_cpi,X
        STA tmp1                ; save cpi for later
        STA dividend
        LDA #0
        STA dividend+1
        STA dividend+2
        STA dividend+3
        JSR print_dec24_rj6

        ; --- Load batch routine pointer ---
        LDA test_idx
        ASL A
        TAX
        LDA tbl_batches,X
        STA test_body
        LDA tbl_batches+1,X
        STA test_body+1

        ; === Method A ===
        LDA #28
        STA zp_col

        LDA tmp1                ; cycles/iter
        JSR method_a

        ; Print iterations (24-bit) right-justified in 8 cols
        LDA iter
        STA dividend
        LDA iter+1
        STA dividend+1
        LDA iter+2
        STA dividend+2
        LDA #0
        STA dividend+3
        JSR print_dec24_rj8

        ; Print CPF right-justified in 8 cols
        LDA #38
        STA zp_col
        LDA cpf
        STA dividend
        LDA cpf+1
        STA dividend+1
        LDA cpf+2
        STA dividend+2
        LDA #0
        STA dividend+3
        JSR print_dec24_rj8

        ; === Method B ===
        LDA tmp1                ; cycles/iter
        JSR method_b

        ; Print frames right-justified in 8 cols
        LDA #46
        STA zp_col
        LDA frame_count
        STA dividend
        LDA #0
        STA dividend+1
        STA dividend+2
        STA dividend+3
        JSR print_dec24_rj8

        ; Print CPF right-justified in 8 cols
        LDA #54
        STA zp_col
        LDA cpf
        STA dividend
        LDA cpf+1
        STA dividend+1
        LDA cpf+2
        STA dividend+2
        LDA #0
        STA dividend+3
        JSR print_dec24_rj8

        ; Next test
        INC test_idx
        LDA test_idx
        CMP #10
        BCS tests_done
        JMP test_loop

tests_done:
        ; --- Footer ---
        LDA #17
        STA zp_row
        LDA #0
        STA zp_col
        LDA #<str_foot1
        STA zp_str
        LDA #>str_foot1
        STA zp_str+1
        JSR print_str

        LDA #18
        STA zp_row
        LDA #0
        STA zp_col
        LDA #<str_foot2
        STA zp_str
        LDA #>str_foot2
        STA zp_str+1
        JSR print_str

        LDA #20
        STA zp_row
        LDA #0
        STA zp_col
        LDA #<str_done
        STA zp_str
        LDA #>str_done
        STA zp_str+1
        JSR print_str

done:   JMP done                ; halt


; ============================================================
; Method A: Count iterations in 4 VSync frames
; ============================================================
; Input:  test_body = pointer to batch routine
;         A = cycles per iteration
; Output: iter (24-bit) = total iterations
;         cpf (24-bit) = cycles/frame
;
; Runs batch routines (256 iters each) until 4 VSync frames elapse.
; iter = batch_count * 256
; cpf = iter * cpi / 4
method_a:
        STA tmp1                ; save cycles/iter

        ; Zero counters
        LDA #0
        STA batch_count
        STA batch_count+1

        ; Wait for next VSync edge (synchronize)
        LDA VID_VSYNC
        STA last_vsync
@sync:  LDA VID_VSYNC
        CMP last_vsync
        BEQ @sync
        STA last_vsync          ; synced — frame just started

        ; Record start vsync + 4
        CLC
        ADC #4
        STA tmp2                ; target vsync value

@run:
        ; Call batch routine via indirect JMP (push return-1, JMP (ptr))
        LDA #>((@ret)-1)
        PHA
        LDA #<((@ret)-1)
        PHA
        JMP (test_body)
@ret:
        ; Increment batch count
        INC batch_count
        BNE @chk
        INC batch_count+1
@chk:
        ; Check if 4 frames elapsed
        LDA VID_VSYNC
        CMP tmp2
        BNE @run                ; keep going until vsync matches target

        ; iter = batch_count * 256 (shift left 8)
        LDA #0
        STA iter                ; low byte = 0
        LDA batch_count
        STA iter+1
        LDA batch_count+1
        STA iter+2

        ; cpf = iter * cpi / 4
        ; First: total_cycles = iter * cpi (24-bit * 8-bit → 32-bit)
        LDA iter
        STA dividend
        LDA iter+1
        STA dividend+1
        LDA iter+2
        STA dividend+2
        LDA #0
        STA dividend+3
        LDA tmp1                ; cpi
        JSR mul24x8

        ; Divide by 4 (shift right 2)
        JSR div_by_4

        ; Store result in cpf
        LDA dividend
        STA cpf
        LDA dividend+1
        STA cpf+1
        LDA dividend+2
        STA cpf+2

        RTS

; ============================================================
; Method B: Count frames for 131072 iterations (512 batches)
; ============================================================
; Input:  test_body = pointer to batch routine
;         A = cycles per iteration
; Output: frame_count (8-bit) = frames elapsed
;         cpf (24-bit) = cycles/frame
;
; Runs exactly 512 batches (131072 iterations).
; Counts VSync frames elapsed.
; cpf = 131072 * cpi / frame_count
method_b:
        STA tmp1                ; save cycles/iter

        ; target = 512 batches
        LDA #<512
        STA target
        LDA #>512
        STA target+1

        ; Zero batch counter
        LDA #0
        STA batch_count
        STA batch_count+1

        ; Wait for next VSync edge
        LDA VID_VSYNC
        STA last_vsync
@sync:  LDA VID_VSYNC
        CMP last_vsync
        BEQ @sync
        STA last_vsync          ; synced

        ; frame_count = 0 (start counting from sync point)
        LDA #0
        STA frame_count

@run:
        ; Call batch routine
        LDA #>((@ret)-1)
        PHA
        LDA #<((@ret)-1)
        PHA
        JMP (test_body)
@ret:
        ; Increment batch count
        INC batch_count
        BNE @no_hi
        INC batch_count+1
@no_hi:
        ; Check VSync — did a frame pass?
        LDA VID_VSYNC
        CMP last_vsync
        BEQ @no_frame
        STA last_vsync
        INC frame_count
@no_frame:
        ; Check if we hit target batches
        LDA batch_count
        CMP target
        BNE @run
        LDA batch_count+1
        CMP target+1
        BNE @run

        ; If frame_count is 0, force to 1 to avoid divide-by-zero
        LDA frame_count
        BNE @have_frames
        LDA #1
        STA frame_count
@have_frames:

        ; cpf = 131072 * cpi / frame_count
        ; 131072 = $020000
        ; total_cycles = 131072 * cpi (up to 131072*25 = 3276800 = $31F400, fits 32-bit)
        LDA #0
        STA dividend            ; low byte of 131072 = $00
        STA dividend+1          ; next byte = $00
        LDA #$02
        STA dividend+2          ; $02
        LDA #0
        STA dividend+3
        LDA tmp1                ; cpi
        JSR mul24x8             ; dividend = 131072 * cpi (32-bit)

        ; Divide by frame_count (8-bit)
        LDA frame_count
        STA divisor
        LDA #0
        STA divisor+1
        JSR div32x16

        ; Store result in cpf
        LDA quotient
        STA cpf
        LDA quotient+1
        STA cpf+1
        LDA quotient+2
        STA cpf+2

        RTS


; ============================================================
; Batch routines — each runs 256 iterations and returns via RTS
; ============================================================

; --- Test 0: NOP (7 cyc/iter: NOP=2, DEX=2, BNE=3) ---
batch_nop:
        LDX #0                  ; 256 iterations (wraps)
@lp:    NOP
        DEX
        BNE @lp
        RTS

; --- Test 1: Register ops (18 cyc/iter: LDA#=2,TAX=2,TXA=2,TAY=2,TYA=2, DEC zp=5,BNE=3) ---
; Uses DEC zp/BNE because it clobbers X and Y
batch_reg:
        LDA #0
        STA zp_loop             ; 256 iterations
@lp:    LDA #$AA
        TAX
        TXA
        TAY
        TYA
        DEC zp_loop
        BNE @lp
        RTS

; --- Test 2: ZP R/W (11 cyc/iter: LDA zp=3, STA zp=3, DEX=2, BNE=3) ---
batch_zp:
        LDX #0
@lp:    LDA zp_target
        STA zp_target
        DEX
        BNE @lp
        RTS

; --- Test 3: Stack (12 cyc/iter: PHA=3, PLA=4, DEX=2, BNE=3) ---
batch_stack:
        LDX #0
        LDA #$55
@lp:    PHA
        PLA
        DEX
        BNE @lp
        RTS

; --- Test 4: Absolute R/W (13 cyc/iter: LDA abs=4, STA abs=4, DEX=2, BNE=3) ---
batch_abs:
        LDX #0
@lp:    LDA abs_target
        STA abs_target
        DEX
        BNE @lp
        RTS

; --- Test 5: Indexed ZP,X (13 cyc/iter: LDA zp,X=4, STA zp,X=4, DEY=2, BNE=3) ---
; Uses Y as loop counter since X is the index
batch_zpx:
        LDX #$10                ; fixed index offset
        LDY #0                  ; 256 iterations
@lp:    LDA $C0,X
        STA $C0,X
        DEY
        BNE @lp
        RTS

; --- Test 6: Branch (10 cyc/iter: CLC=2, BCC=3, DEX=2, BNE=3) ---
batch_branch:
        LDX #0
@lp:    CLC
        BCC :+
:       DEX
        BNE @lp
        RTS

; --- Test 7: JSR/RTS (17 cyc/iter: JSR=6, RTS=6, DEX=2, BNE=3) ---
batch_jsr:
        LDX #0
@lp:    JSR sub_rts
        DEX
        BNE @lp
        RTS
sub_rts:
        RTS

; --- Test 8: Indirect (zp),Y (16 cyc/iter: LDA(zp),Y=5, STA(zp),Y=6, DEX=2, BNE=3) ---
batch_ind:
        ; Set up pointer to abs_target
        LDA #<abs_target
        STA ind_ptr
        LDA #>abs_target
        STA ind_ptr+1
        LDX #0
        LDY #0
@lp:    LDA (ind_ptr),Y
        STA (ind_ptr),Y
        DEX
        BNE @lp
        RTS

; --- Test 9: 16-bit Arith (25 cyc/iter: CLC=2, LDA zp=3, ADC zp=3, STA zp=3,
;             LDA zp=3, ADC zp=3, STA zp=3, DEX=2, BNE=3) ---
batch_arith:
        LDX #0
        LDA #0
        STA arith_lo
        STA arith_hi
@lp:    CLC
        LDA arith_lo
        ADC #$01
        STA arith_lo
        LDA arith_hi
        ADC #$00
        STA arith_hi
        DEX
        BNE @lp
        RTS


; ============================================================
; Math routines
; ============================================================

; --- mul24x8: dividend (32-bit) = dividend[0..2] * A ---
; Input:  dividend[0..2] = 24-bit multiplicand, A = 8-bit multiplier
; Output: dividend[0..3] = 32-bit product
; Destroys: tmp3, tmp4, X
mul24x8:
        STA tmp3                ; multiplier
        ; Copy multiplicand to safe place (use quotient as temp)
        LDA dividend
        STA quotient
        LDA dividend+1
        STA quotient+1
        LDA dividend+2
        STA quotient+2

        ; Zero product
        LDA #0
        STA dividend
        STA dividend+1
        STA dividend+2
        STA dividend+3

        LDX #8                  ; 8 bits
@loop:  LSR tmp3                ; shift multiplier right
        BCC @no_add
        ; Add multiplicand to product
        CLC
        LDA dividend
        ADC quotient
        STA dividend
        LDA dividend+1
        ADC quotient+1
        STA dividend+1
        LDA dividend+2
        ADC quotient+2
        STA dividend+2
        LDA dividend+3
        ADC #0
        STA dividend+3
@no_add:
        ; Shift multiplicand left
        ASL quotient
        ROL quotient+1
        ROL quotient+2
        DEX
        BNE @loop
        RTS

; --- div32x16: quotient (32-bit) = dividend (32-bit) / divisor (16-bit) ---
; Input:  dividend[0..3], divisor[0..1]
; Output: quotient[0..3], remainder[0..1]
; Destroys: X
div32x16:
        ; Zero quotient and remainder
        LDA #0
        STA quotient
        STA quotient+1
        STA quotient+2
        STA quotient+3
        STA remainder
        STA remainder+1

        LDX #32                 ; 32 bits
@loop:
        ; Shift dividend left into remainder
        ASL dividend
        ROL dividend+1
        ROL dividend+2
        ROL dividend+3
        ROL remainder
        ROL remainder+1

        ; Shift quotient left
        ASL quotient
        ROL quotient+1
        ROL quotient+2
        ROL quotient+3

        ; Try subtract: remainder - divisor
        LDA remainder
        SEC
        SBC divisor
        STA tmp3
        LDA remainder+1
        SBC divisor+1
        STA tmp4

        ; If borrow (carry clear), remainder < divisor, skip
        BCC @skip
        ; Accept subtraction
        LDA tmp3
        STA remainder
        LDA tmp4
        STA remainder+1
        ; Set bit 0 of quotient
        INC quotient
@skip:
        DEX
        BNE @loop
        RTS

; --- div_by_4: shift dividend (32-bit) right by 2 ---
div_by_4:
        LSR dividend+3
        ROR dividend+2
        ROR dividend+1
        ROR dividend
        LSR dividend+3
        ROR dividend+2
        ROR dividend+1
        ROR dividend
        RTS

; --- mul_by_10: dividend (32-bit) = dividend (32-bit) * 10 ---
; Uses shift-and-add: x*10 = x*8 + x*2
mul_by_10:
        ; Save original in quotient (temp)
        LDA dividend
        STA quotient
        LDA dividend+1
        STA quotient+1
        LDA dividend+2
        STA quotient+2
        LDA dividend+3
        STA quotient+3

        ; dividend *= 2
        ASL dividend
        ROL dividend+1
        ROL dividend+2
        ROL dividend+3

        ; Save x*2 in remainder (temp)
        LDA dividend
        STA remainder
        LDA dividend+1
        STA remainder+1

        ; We need 4 bytes for x*2, use tmp3/tmp4 for high bytes
        LDA dividend+2
        STA tmp3
        LDA dividend+3
        STA tmp4

        ; dividend *= 4 (now x*8)
        ASL dividend
        ROL dividend+1
        ROL dividend+2
        ROL dividend+3
        ASL dividend
        ROL dividend+1
        ROL dividend+2
        ROL dividend+3

        ; dividend = x*8 + x*2
        CLC
        LDA dividend
        ADC remainder
        STA dividend
        LDA dividend+1
        ADC remainder+1
        STA dividend+1
        LDA dividend+2
        ADC tmp3
        STA dividend+2
        LDA dividend+3
        ADC tmp4
        STA dividend+3
        RTS


; ============================================================
; Display routines
; ============================================================

; --- clear_screen: fill FB with spaces ---
clear_screen:
        LDA #0
        STA zp_col
        STA zp_row

        LDA #<FB_BASE
        STA zp_fb
        LDA #>FB_BASE
        STA zp_fb+1

        LDA #$20
        LDX #$08                ; 8 pages = 2048 bytes
        LDY #0
@page:  STA (zp_fb),Y
        INY
        BNE @page
        INC zp_fb+1
        DEX
        BNE @page
        RTS

; --- calc_fb_addr: zp_fb = FB_BASE + row*80 + col ---
calc_fb_addr:
        ; row * 16
        LDA #0
        STA zp_fb+1
        LDA zp_row
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        STA zp_fb               ; zp_fb = row*16

        ; Save row*16
        LDA zp_fb+1
        PHA
        LDA zp_fb
        PHA

        ; row*64 = row*16 << 2
        ASL zp_fb
        ROL zp_fb+1
        ASL zp_fb
        ROL zp_fb+1

        ; row*80 = row*64 + row*16
        PLA
        CLC
        ADC zp_fb
        STA zp_fb
        PLA
        ADC zp_fb+1
        STA zp_fb+1

        ; + col
        CLC
        LDA zp_fb
        ADC zp_col
        STA zp_fb
        LDA zp_fb+1
        ADC #0
        STA zp_fb+1

        ; + FB_BASE ($C000)
        CLC
        LDA zp_fb+1
        ADC #>FB_BASE
        STA zp_fb+1
        RTS

; --- put_char: write A to frame buffer at (zp_col, zp_row), advance col ---
put_char:
        PHA
        JSR calc_fb_addr
        PLA
        LDY #0
        STA (zp_fb),Y
        INC zp_col
        RTS

; --- print_str: print null-terminated string at (zp_str) ---
print_str:
        LDY #0
@loop:  LDA (zp_str),Y
        BEQ @done
        PHA
        TYA
        PHA
        LDA (zp_str),Y          ; re-read (Y was saved)
        JSR put_char
        PLA
        TAY
        PLA                     ; balance the PHA of char
        INY
        BNE @loop
@done:  RTS

; --- print_dec24: print dividend[0..2] as decimal at current cursor ---
; Converts 24-bit value to decimal string and prints it.
print_dec24:
        LDA #0
        STA print_len

        ; Handle zero specially
        LDA dividend
        ORA dividend+1
        ORA dividend+2
        BNE @nonzero
        LDA #'0'
        JSR put_char
        RTS

@nonzero:
        ; Repeated divide by 10, collect remainders
@div_loop:
        ; Check if dividend is zero
        LDA dividend
        ORA dividend+1
        ORA dividend+2
        BEQ @print

        ; div32x16 with divisor=10
        LDA #10
        STA divisor
        LDA #0
        STA divisor+1
        STA dividend+3          ; extend to 32-bit
        JSR div32x16

        ; remainder[0] is the digit
        LDA remainder
        CLC
        ADC #'0'
        LDX print_len
        STA print_buf,X
        INC print_len

        ; quotient back to dividend for next iteration
        LDA quotient
        STA dividend
        LDA quotient+1
        STA dividend+1
        LDA quotient+2
        STA dividend+2

        JMP @div_loop

@print:
        ; Print digits in reverse order
        LDX print_len
@ploop: DEX
        BMI @pdone
        LDA print_buf,X
        JSR put_char
        JMP @ploop
@pdone: RTS

; --- print_dec24_rj6: print dividend as right-justified decimal in 6 columns ---
print_dec24_rj6:
        JSR count_digits        ; print_len = number of digits
        ; Pad with spaces: 6 - print_len
        LDA #6
        SEC
        SBC print_len
        TAX
        BEQ @no_pad
        BMI @no_pad
@pad:   LDA #' '
        JSR put_char
        DEX
        BNE @pad
@no_pad:
        JMP print_dec24_body

; --- print_dec24_rj8: print dividend as right-justified decimal in 8 columns ---
print_dec24_rj8:
        JSR count_digits
        LDA #8
        SEC
        SBC print_len
        TAX
        BEQ @no_pad
        BMI @no_pad
@pad:   LDA #' '
        JSR put_char
        DEX
        BNE @pad
@no_pad:
        JMP print_dec24_body

; --- count_digits: count decimal digits of dividend, store in print_len ---
; Non-destructive: saves and restores dividend.
count_digits:
        ; Save dividend
        LDA dividend
        PHA
        LDA dividend+1
        PHA
        LDA dividend+2
        PHA
        LDA dividend+3
        PHA

        LDA #0
        STA print_len

        ; Check for zero
        LDA dividend
        ORA dividend+1
        ORA dividend+2
        BNE @count
        LDA #1
        STA print_len
        JMP @restore

@count:
        LDA #10
        STA divisor
        LDA #0
        STA divisor+1
        STA dividend+3
@loop:  LDA dividend
        ORA dividend+1
        ORA dividend+2
        BEQ @restore
        JSR div32x16
        INC print_len
        LDA quotient
        STA dividend
        LDA quotient+1
        STA dividend+1
        LDA quotient+2
        STA dividend+2
        LDA #0
        STA dividend+3
        JMP @loop

@restore:
        PLA
        STA dividend+3
        PLA
        STA dividend+2
        PLA
        STA dividend+1
        PLA
        STA dividend
        RTS

; --- print_dec24_body: convert and print (shared by print_dec24 and rj variants) ---
print_dec24_body:
        LDA #0
        STA print_len

        ; Handle zero
        LDA dividend
        ORA dividend+1
        ORA dividend+2
        BNE @nonzero
        LDA #'0'
        JSR put_char
        RTS

@nonzero:
@div_loop:
        LDA dividend
        ORA dividend+1
        ORA dividend+2
        BEQ @print

        LDA #10
        STA divisor
        LDA #0
        STA divisor+1
        STA dividend+3
        JSR div32x16

        LDA remainder
        CLC
        ADC #'0'
        LDX print_len
        STA print_buf,X
        INC print_len

        LDA quotient
        STA dividend
        LDA quotient+1
        STA dividend+1
        LDA quotient+2
        STA dividend+2

        JMP @div_loop

@print:
        LDX print_len
@ploop: DEX
        BMI @pdone
        LDA print_buf,X
        JSR put_char
        JMP @ploop
@pdone: RTS


; ============================================================
; RODATA — strings and tables
; ============================================================
.segment "RODATA"

str_title:
        .byte "         N8 MACHINE  6502 BENCHMARK", 0

str_cpf_label:
        .byte "  Cycles/frame: ", 0

str_mhz_mid:
        .byte "    ~", 0

str_mhz_tail:
        .byte " MHz  (13ms/frame)", 0

str_hdr1:
        .byte "  Test              C/I   A:Iters    A:CPF  B:Frms   B:CPF", 0

str_hdr2:
        .byte "  ----              ---   -------    -----  ------   -----", 0

str_foot1:
        .byte "  A = iterations in 4 frames  B = frames for 131072 iters", 0

str_foot2:
        .byte "  CPF = cycles/frame = iters * cyc/iter / frames", 0

str_done:
        .byte "  Done.", 0

; --- Test names ---
str_nop:     .byte "NOP               ", 0
str_reg:     .byte "Register          ", 0
str_zp:      .byte "ZP R/W            ", 0
str_stack:   .byte "Stack             ", 0
str_abs:     .byte "Absolute          ", 0
str_zpx:     .byte "Indexed ZP,X      ", 0
str_branch:  .byte "Branch            ", 0
str_jsr:     .byte "JSR/RTS           ", 0
str_ind:     .byte "Indirect (zp),Y   ", 0
str_arith:   .byte "16-bit Arith      ", 0

; --- Pointer table: test names ---
tbl_names:
        .addr str_nop
        .addr str_reg
        .addr str_zp
        .addr str_stack
        .addr str_abs
        .addr str_zpx
        .addr str_branch
        .addr str_jsr
        .addr str_ind
        .addr str_arith

; --- Cycles per iteration table ---
tbl_cpi:
        .byte 7                 ; NOP
        .byte 18                ; Register
        .byte 11                ; ZP R/W
        .byte 12                ; Stack
        .byte 13                ; Absolute
        .byte 13                ; Indexed ZP,X
        .byte 10                ; Branch
        .byte 17                ; JSR/RTS
        .byte 16                ; Indirect (zp),Y
        .byte 25                ; 16-bit Arith

; --- Pointer table: batch routines ---
tbl_batches:
        .addr batch_nop
        .addr batch_reg
        .addr batch_zp
        .addr batch_stack
        .addr batch_abs
        .addr batch_zpx
        .addr batch_branch
        .addr batch_jsr
        .addr batch_ind
        .addr batch_arith
