; test_vidperf.s - Video Write Method Benchmark
;
; Compares three methods of writing a line of text + newline:
;   1) Software FB:  calculate address, write to $C000+, software scroll
;   2) Kernel:       JSR _con_putchar / _con_newline (ROM routines)
;   3) Hardware:     write VID_DATA + VID_CURCOL/VID_CURROW/VID_OPER directly
;
; Workload: print 40-char line + newline, 1024 times (fills screen + scrolls)
; Measures VSync frames elapsed per method.
;
; Requires: n8_kernel loaded at $F000 (standard ROM)

.export   _main
.import   _exit

; ============================================================
; Hardware registers
; ============================================================
VID_MODE   = $D840
VID_WIDTH  = $D841
VID_HEIGHT = $D842
VID_STRIDE = $D843
VID_OPER   = $D844
VID_CURCOL = $D846
VID_CURROW = $D847
VID_VSYNC  = $D848
VID_DATA   = $D84A

FB_BASE    = $C000

VIDOP_CLEAR     = $05
VIDOP_SCROLL_UP = $01

LINE_LEN   = 40               ; chars per line
NUM_LINES  = 1024             ; total lines to print

; ============================================================
; Kernel entry points (from n8_kernel.sym)
; ============================================================
con_putchar  = $F106
con_newline  = $F10A
con_clear    = $F124
con_setcursor = $F1B0

; ============================================================
; Zero page
; ============================================================
.segment "ZEROPAGE"
zp_col:      .res 1           ; software cursor column
zp_row:      .res 1           ; software cursor row
zp_fb:       .res 2           ; frame buffer pointer
zp_str:      .res 2           ; string pointer (for result output)
last_vsync:  .res 1
frame_count: .res 2           ; 16-bit frame counter
line_count:  .res 2           ; 16-bit line counter
dividend:    .res 4
divisor:     .res 2
quotient:    .res 4
remainder:   .res 2
print_buf:   .res 10
print_len:   .res 1
tmp1:        .res 1
tmp2:        .res 1

; ============================================================
; BSS
; ============================================================
.segment "BSS"
result1:    .res 2             ; frames for software FB
result2:    .res 2             ; frames for kernel putchar
result3:    .res 2             ; frames for kernel putstr
result4:    .res 2             ; frames for hardware direct

; ============================================================
; CODE
; ============================================================
.segment "CODE"

_main:
        ; Clear screen via hardware
        LDA #VIDOP_CLEAR
        STA VID_OPER

        ; Title (via hardware VID_DATA — quick and dirty)
        LDA #0
        STA VID_CURCOL
        STA VID_CURROW
        LDA #<str_title
        STA zp_str
        LDA #>str_title
        STA zp_str+1
        JSR print_vid

        ; ---- Test 1: Software FB ----
        JSR test_software_fb
        LDA frame_count
        STA result1
        LDA frame_count+1
        STA result1+1

        ; ---- Test 2: Kernel routines ----
        JSR test_kernel
        LDA frame_count
        STA result2
        LDA frame_count+1
        STA result2+1

        ; ---- Test 3: Kernel putstr ----
        JSR test_kernel_putstr
        LDA frame_count
        STA result3
        LDA frame_count+1
        STA result3+1

        ; ---- Test 4: Hardware direct ----
        JSR test_hardware
        LDA frame_count
        STA result4
        LDA frame_count+1
        STA result4+1

        ; ---- Display Results ----
        LDA #VIDOP_CLEAR
        STA VID_OPER

        ; Title
        LDA #0
        STA VID_CURCOL
        STA VID_CURROW
        LDA #<str_title
        STA zp_str
        LDA #>str_title
        STA zp_str+1
        JSR print_vid

        ; Row 2: header
        LDA #0
        STA VID_CURCOL
        LDA #2
        STA VID_CURROW
        LDA #<str_header
        STA zp_str
        LDA #>str_header
        STA zp_str+1
        JSR print_vid

        ; Row 3: separator
        LDA #0
        STA VID_CURCOL
        LDA #3
        STA VID_CURROW
        LDA #<str_sep
        STA zp_str
        LDA #>str_sep
        STA zp_str+1
        JSR print_vid

        ; Row 4: Software FB
        LDA #0
        STA VID_CURCOL
        LDA #4
        STA VID_CURROW
        LDA #<str_sw
        STA zp_str
        LDA #>str_sw
        STA zp_str+1
        JSR print_vid
        ; Print frame count at col 35
        LDA #35
        STA VID_CURCOL
        LDA result1
        STA dividend
        LDA result1+1
        STA dividend+1
        LDA #0
        STA dividend+2
        STA dividend+3
        JSR print_dec_vid

        ; Row 5: Kernel
        LDA #0
        STA VID_CURCOL
        LDA #5
        STA VID_CURROW
        LDA #<str_kern
        STA zp_str
        LDA #>str_kern
        STA zp_str+1
        JSR print_vid
        LDA #35
        STA VID_CURCOL
        LDA result2
        STA dividend
        LDA result2+1
        STA dividend+1
        LDA #0
        STA dividend+2
        STA dividend+3
        JSR print_dec_vid

        ; Row 6: Kernel putstr
        LDA #0
        STA VID_CURCOL
        LDA #6
        STA VID_CURROW
        LDA #<str_kstr
        STA zp_str
        LDA #>str_kstr
        STA zp_str+1
        JSR print_vid
        LDA #35
        STA VID_CURCOL
        LDA result3
        STA dividend
        LDA result3+1
        STA dividend+1
        LDA #0
        STA dividend+2
        STA dividend+3
        JSR print_dec_vid

        ; Row 7: Hardware
        LDA #0
        STA VID_CURCOL
        LDA #7
        STA VID_CURROW
        LDA #<str_hw
        STA zp_str
        LDA #>str_hw
        STA zp_str+1
        JSR print_vid
        LDA #35
        STA VID_CURCOL
        LDA result4
        STA dividend
        LDA result4+1
        STA dividend+1
        LDA #0
        STA dividend+2
        STA dividend+3
        JSR print_dec_vid

        ; Row 9: footer
        LDA #0
        STA VID_CURCOL
        LDA #9
        STA VID_CURROW
        LDA #<str_foot
        STA zp_str
        LDA #>str_foot
        STA zp_str+1
        JSR print_vid

        ; Row 10: done
        LDA #0
        STA VID_CURCOL
        LDA #10
        STA VID_CURROW
        LDA #<str_done
        STA zp_str
        LDA #>str_done
        STA zp_str+1
        JSR print_vid

done:   JMP _exit


; ============================================================
; Test 1: Software FB — all work done in software
; ============================================================
; Calculates FB address, writes chars, handles wrap + scroll
test_software_fb:
        ; Init software cursor
        LDA #0
        STA zp_col
        STA zp_row
        STA frame_count
        STA frame_count+1
        STA line_count
        STA line_count+1

        ; Clear framebuffer (software)
        JSR sw_clear

        ; Sync to vsync
        LDA VID_VSYNC
        STA last_vsync
@sync:  LDA VID_VSYNC
        CMP last_vsync
        BEQ @sync
        STA last_vsync

@line:
        ; Write LINE_LEN chars
        LDX #LINE_LEN
@char:
        ; calc FB address: zp_fb = FB_BASE + zp_row*80 + zp_col
        JSR sw_calc_addr
        LDA #'A'
        LDY #0
        STA (zp_fb),Y
        INC zp_col
        DEX
        BNE @char

        ; Newline: col=0, row++, scroll if needed
        LDA #0
        STA zp_col
        INC zp_row
        LDA zp_row
        CMP #25
        BCC @no_scroll

        ; Scroll up: copy rows 1-24 to 0-23, clear row 24
        JSR sw_scroll
        DEC zp_row              ; stay at row 24

@no_scroll:
        ; Check vsync
        LDA VID_VSYNC
        CMP last_vsync
        BEQ @no_frame
        STA last_vsync
        INC frame_count
        BNE @no_frame
        INC frame_count+1
@no_frame:

        ; Increment line counter
        INC line_count
        BNE @chk
        INC line_count+1
@chk:   LDA line_count+1
        CMP #>NUM_LINES
        BNE @line
        LDA line_count
        CMP #<NUM_LINES
        BNE @line

        ; Ensure at least 1 frame
        LDA frame_count
        ORA frame_count+1
        BNE @ret
        LDA #1
        STA frame_count
@ret:   RTS


; --- sw_calc_addr: zp_fb = FB_BASE + zp_row*80 + zp_col ---
sw_calc_addr:
        LDA #0
        STA zp_fb+1
        LDA zp_row
        ; row * 16
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        ASL A
        ROL zp_fb+1
        STA zp_fb

        ; save row*16
        LDA zp_fb+1
        PHA
        LDA zp_fb
        PHA

        ; row*64
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

        ; + FB_BASE
        CLC
        LDA zp_fb+1
        ADC #>FB_BASE
        STA zp_fb+1
        RTS


; --- sw_scroll: copy rows 1-24 up, clear row 24 ---
sw_scroll:
        ; Source = FB_BASE + 80, Dest = FB_BASE
        ; Copy 24*80 = 1920 bytes
        LDA #<FB_BASE
        STA zp_fb               ; dest lo
        LDA #>FB_BASE
        STA zp_fb+1             ; dest hi

        ; Use tmp1/tmp2 as source pointer
        LDA #<(FB_BASE + 80)
        STA tmp1
        LDA #>(FB_BASE + 80)
        STA tmp2

        ; Copy 1920 bytes (7 full pages + 128 bytes)
        ; We'll do byte-by-byte with indexed
        LDX #7                  ; full pages
        LDY #0
@page:  LDA (tmp1),Y
        STA (zp_fb),Y
        INY
        BNE @page
        INC zp_fb+1
        INC tmp2
        DEX
        BNE @page
        ; remaining 128 bytes
        LDY #0
@tail:  LDA (tmp1),Y
        STA (zp_fb),Y
        INY
        CPY #128
        BNE @tail

        ; Clear last row (row 24): FB_BASE + 24*80 = FB_BASE + 1920
        ; zp_fb should be at FB_BASE + 1920 after the copy
        LDA #' '
        LDY #0
@clr:   STA (zp_fb),Y
        INY
        CPY #80
        BNE @clr
        RTS


; --- sw_clear: fill entire FB with spaces ---
sw_clear:
        LDA #<FB_BASE
        STA zp_fb
        LDA #>FB_BASE
        STA zp_fb+1
        LDA #' '
        LDX #8                  ; 8 pages = 2048 bytes
        LDY #0
@page:  STA (zp_fb),Y
        INY
        BNE @page
        INC zp_fb+1
        DEX
        BNE @page
        RTS


; ============================================================
; Test 2: Kernel routines — JSR con_putchar / con_newline
; ============================================================
test_kernel:
        LDA #0
        STA frame_count
        STA frame_count+1
        STA line_count
        STA line_count+1

        ; Clear via kernel
        JSR con_clear

        ; Set cursor to 0,0 via kernel
        LDX #0
        LDY #0
        JSR con_setcursor

        ; Sync to vsync
        LDA VID_VSYNC
        STA last_vsync
@sync:  LDA VID_VSYNC
        CMP last_vsync
        BEQ @sync
        STA last_vsync

@line:
        ; Write LINE_LEN chars via kernel
        LDX #LINE_LEN
@char:
        TXA
        PHA
        LDA #'B'
        JSR con_putchar
        PLA
        TAX
        DEX
        BNE @char

        ; Newline via kernel (handles scroll)
        JSR con_newline

        ; Check vsync
        LDA VID_VSYNC
        CMP last_vsync
        BEQ @no_frame
        STA last_vsync
        INC frame_count
        BNE @no_frame
        INC frame_count+1
@no_frame:

        INC line_count
        BNE @chk
        INC line_count+1
@chk:   LDA line_count+1
        CMP #>NUM_LINES
        BNE @line
        LDA line_count
        CMP #<NUM_LINES
        BNE @line

        LDA frame_count
        ORA frame_count+1
        BNE @ret
        LDA #1
        STA frame_count
@ret:   RTS


; ============================================================
; Test 3: Kernel putstr — simulated con_putstr via VID_DATA
; ============================================================
; Like kernel putchar but uses an internal loop: set cursor,
; then LDA (zp),Y / STA VID_DATA for each char. One JSR per line.
test_kernel_putstr:
        LDA #0
        STA frame_count
        STA frame_count+1
        STA line_count
        STA line_count+1

        ; Clear via kernel
        JSR con_clear

        ; Set cursor to 0,0
        LDX #0
        LDY #0
        JSR con_setcursor

        ; Sync to vsync
        LDA VID_VSYNC
        STA last_vsync
@sync:  LDA VID_VSYNC
        CMP last_vsync
        BEQ @sync
        STA last_vsync

@line:
        ; Print 40-char string via simulated putstr
        LDA #<str_testline
        STA zp_str
        LDA #>str_testline
        STA zp_str+1
        JSR sim_putstr

        ; Newline via kernel (handles scroll)
        JSR con_newline

        ; Check vsync
        LDA VID_VSYNC
        CMP last_vsync
        BEQ @no_frame
        STA last_vsync
        INC frame_count
        BNE @no_frame
        INC frame_count+1
@no_frame:

        INC line_count
        BNE @chk
        INC line_count+1
@chk:   LDA line_count+1
        CMP #>NUM_LINES
        BNE @line
        LDA line_count
        CMP #<NUM_LINES
        BNE @line

        LDA frame_count
        ORA frame_count+1
        BNE @ret
        LDA #1
        STA frame_count
@ret:   RTS

; --- sim_putstr: write null-terminated string at (zp_str) via VID_DATA ---
; This simulates what a kernel con_putstr would do internally.
sim_putstr:
        LDY #0
@loop:  LDA (zp_str),Y
        BEQ @done
        STA VID_DATA
        INY
        BNE @loop
@done:  RTS


; ============================================================
; Test 4: Hardware direct — VID_DATA + register writes
; ============================================================
test_hardware:
        LDA #0
        STA frame_count
        STA frame_count+1
        STA line_count
        STA line_count+1
        STA zp_col
        STA zp_row

        ; Clear via hardware
        LDA #VIDOP_CLEAR
        STA VID_OPER

        ; Set cursor
        LDA #0
        STA VID_CURCOL
        STA VID_CURROW

        ; Sync to vsync
        LDA VID_VSYNC
        STA last_vsync
@sync:  LDA VID_VSYNC
        CMP last_vsync
        BEQ @sync
        STA last_vsync

@line:
        ; Stream LINE_LEN chars via VID_DATA (cursor auto-advances)
        LDA #'C'
        LDY #LINE_LEN
@char:  STA VID_DATA
        DEY
        BNE @char

        ; Newline: set col=0, advance row, scroll if needed
        LDA #0
        STA VID_CURCOL
        INC zp_row
        LDA zp_row
        CMP #25
        BCC @set_row
        ; Scroll via hardware
        LDA #VIDOP_SCROLL_UP
        STA VID_OPER
        DEC zp_row
@set_row:
        LDA zp_row
        STA VID_CURROW

        ; Check vsync
        LDA VID_VSYNC
        CMP last_vsync
        BEQ @no_frame
        STA last_vsync
        INC frame_count
        BNE @no_frame
        INC frame_count+1
@no_frame:

        INC line_count
        BNE @chk
        INC line_count+1
@chk:   LDA line_count+1
        CMP #>NUM_LINES
        BNE @line
        LDA line_count
        CMP #<NUM_LINES
        BNE @line

        LDA frame_count
        ORA frame_count+1
        BNE @ret
        LDA #1
        STA frame_count
@ret:   RTS


; ============================================================
; Output helpers (use VID_DATA for result display)
; ============================================================

; --- print_vid: print null-terminated string via VID_DATA ---
print_vid:
        LDY #0
@loop:  LDA (zp_str),Y
        BEQ @done
        STA VID_DATA
        INY
        BNE @loop
@done:  RTS

; --- print_dec_vid: print dividend[0..2] as decimal via VID_DATA ---
print_dec_vid:
        LDA #0
        STA print_len

        LDA dividend
        ORA dividend+1
        ORA dividend+2
        BNE @nonzero
        LDA #'0'
        STA VID_DATA
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
        STA VID_DATA
        JMP @ploop
@pdone: RTS

; --- div32x16 ---
div32x16:
        LDA #0
        STA quotient
        STA quotient+1
        STA quotient+2
        STA quotient+3
        STA remainder
        STA remainder+1

        LDX #32
@loop:
        ASL dividend
        ROL dividend+1
        ROL dividend+2
        ROL dividend+3
        ROL remainder
        ROL remainder+1

        ASL quotient
        ROL quotient+1
        ROL quotient+2
        ROL quotient+3

        LDA remainder
        SEC
        SBC divisor
        STA tmp1
        LDA remainder+1
        SBC divisor+1
        STA tmp2

        BCC @skip
        LDA tmp1
        STA remainder
        LDA tmp2
        STA remainder+1
        INC quotient
@skip:
        DEX
        BNE @loop
        RTS


; ============================================================
; RODATA
; ============================================================
.segment "RODATA"

str_title:
        .byte "  VIDEO WRITE METHOD BENCHMARK", 0

str_header:
        .byte "  Method                       Frames", 0

str_sep:
        .byte "  ------                       ------", 0

str_sw:
        .byte "  Software FB  (calc+scroll)", 0

str_kern:
        .byte "  Kernel       (con_putchar)", 0

str_kstr:
        .byte "  Kernel       (putstr sim)", 0

str_hw:
        .byte "  Hardware     (VID_DATA direct)", 0

str_foot:
        .byte "  1024 lines x 40 chars (lower=faster)", 0

str_done:
        .byte "  Done.", 0

; 40-char test line for putstr benchmark
str_testline:
        .byte "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD", 0
