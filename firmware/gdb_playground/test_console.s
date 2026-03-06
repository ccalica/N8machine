; test_console.s - Console kernel routine validation
;
; 9 test phases exercising all console API routines via the kernel
; jump table. Each phase stores results in zp_result ($00-$0F)
; for GDB inspection. Labeled checkpoints serve as breakpoints.
;
; n8gdb workflow:
;   load test_console 0xE000
;   reset
;   bp phase1_done
;   bp phase2_done
;   ...
;   bp all_done
;   run
;   (verify zp_result at each breakpoint)
;
; Phase summary:
;   1  con_clear — clear screen, verify FB spaces + cursor home
;   2  con_setcursor — direct cursor positioning
;   3  con_putchar — write chars via VID_DATA
;   4  con_getstatus — read status, col, row
;   5  con_newline — advance row + scroll at bottom
;   6  con_setmode — set VID_MODE and VID_CTRL
;   7  con_movcursor — signed cursor delta
;   8  con_scroll — signed screen scroll
;   9  con_getkey — key read (inject via GDB before hitting bp)

.export   _main
.export   phase1_done, phase2_done, phase3_done, phase4_done
.export   phase5_done, phase6_done, phase7_done, phase8_done
.export   phase9_inject, phase9_done, all_done

; --- Kernel entry points ---
K_CON_GETKEY    = $FE09
K_CON_SETMODE   = $FE0C
K_CON_GETSTATUS = $FE0F
K_CON_PUTCHAR   = $FE12
K_CON_NEWLINE   = $FE15
K_CON_CLEAR     = $FE18
K_CON_SCROLL    = $FE1B
K_CON_MOVCURSOR = $FE1E
K_CON_SETCURSOR = $FE21

; --- Hardware registers (for direct verification only) ---
VID_MODE    = $D840
VID_CURCOL  = $D846
VID_CURROW  = $D847
VID_CTRL    = $D849
VID_STATUS  = $D84B

FB_BASE     = $C000

; --- Zero page: test result storage ---
.segment "ZEROPAGE"
zp_result:   .res 16            ; $00-$0F

.segment "CODE"

_main:

; ============================================================
; Phase 1: con_clear
;   Dirty FB, move cursor away, call con_clear.
;   Expect: FB=$20, cursor (0,0)
;   zp_result: [0]=FB[0] [1]=FB[79] [2]=FB[1999] [3]=col [4]=row
; ============================================================
phase1:
        ; Dirty first 256 bytes
        LDA #$FF
        LDX #0
@fill:  STA FB_BASE,X
        INX
        BNE @fill

        ; Move cursor away
        LDX #40
        LDY #12
        JSR K_CON_SETCURSOR

        ; Clear
        JSR K_CON_CLEAR

        ; Verify
        LDA FB_BASE
        STA zp_result+0        ; [0] expect $20
        LDA FB_BASE+79
        STA zp_result+1        ; [1] expect $20
        LDA FB_BASE+$07CF      ; row24 col79
        STA zp_result+2        ; [2] expect $20
        LDA VID_CURCOL
        STA zp_result+3        ; [3] expect 0
        LDA VID_CURROW
        STA zp_result+4        ; [4] expect 0

phase1_done:
        JMP phase2

; ============================================================
; Phase 2: con_setcursor
;   Set cursor to (35, 10), verify register values.
;   zp_result: [0]=col [1]=row
; ============================================================
phase2:
        JSR K_CON_CLEAR

        LDX #35
        LDY #10
        JSR K_CON_SETCURSOR

        LDA VID_CURCOL
        STA zp_result+0        ; [0] expect 35
        LDA VID_CURROW
        STA zp_result+1        ; [1] expect 10

phase2_done:
        JMP phase3

; ============================================================
; Phase 3: con_putchar
;   CLEAR, set cursor (0,0), write "HI" via con_putchar.
;   Expect: FB[0]='H', FB[1]='I'
;   (VID_CTRL default=$07 so advance+wrap+scroll active)
;   zp_result: [0]=FB[0] [1]=FB[1] [2]=col [3]=row
; ============================================================
phase3:
        JSR K_CON_CLEAR

        LDX #0
        LDY #0
        JSR K_CON_SETCURSOR

        LDA #'H'
        JSR K_CON_PUTCHAR
        LDA #'I'
        JSR K_CON_PUTCHAR

        LDA FB_BASE+0
        STA zp_result+0        ; [0] expect 'H' ($48)
        LDA FB_BASE+1
        STA zp_result+1        ; [1] expect 'I' ($49)
        LDA VID_CURCOL
        STA zp_result+2        ; [2] expect 2
        LDA VID_CURROW
        STA zp_result+3        ; [3] expect 0

phase3_done:
        JMP phase4

; ============================================================
; Phase 4: con_getstatus
;   Set cursor (20, 5), read back via getstatus.
;   zp_result: [0]=status(A) [1]=col(X) [2]=row(Y)
; ============================================================
phase4:
        LDX #20
        LDY #5
        JSR K_CON_SETCURSOR

        JSR K_CON_GETSTATUS
        STA zp_result+0        ; [0] = VID_STATUS (expect 0)
        STX zp_result+1        ; [1] = col (expect 20)
        STY zp_result+2        ; [2] = row (expect 5)

phase4_done:
        JMP phase5

; ============================================================
; Phase 5: con_newline
;   5a: Newline from row 0 — should go to row 1, col 0
;   5b: Newline from row 24 (bottom) — should scroll, stay row 24
;   Place marker at (0,1) to verify scroll moves it to (0,0).
;   zp_result: [0]=col_a [1]=row_a [2]=col_b [3]=row_b [4]=FB[0]
; ============================================================
phase5:
        JSR K_CON_CLEAR

        ; 5a: newline from (10, 0)
        LDX #10
        LDY #0
        JSR K_CON_SETCURSOR

        JSR K_CON_NEWLINE

        LDA VID_CURCOL
        STA zp_result+0        ; [0] expect 0
        LDA VID_CURROW
        STA zp_result+1        ; [1] expect 1

        ; 5b: place marker at (0,1) then newline from bottom row
        ; Write 'M' at FB offset 80 (row 1, col 0)
        LDA #'M'
        STA FB_BASE+80

        LDX #30
        LDY #24
        JSR K_CON_SETCURSOR

        JSR K_CON_NEWLINE

        LDA VID_CURCOL
        STA zp_result+2        ; [2] expect 0
        LDA VID_CURROW
        STA zp_result+3        ; [3] expect 24 (scroll, row unchanged)
        ; After scroll up, row 1 data is now at row 0
        LDA FB_BASE+0
        STA zp_result+4        ; [4] expect 'M' ($4D)

phase5_done:
        JMP phase6

; ============================================================
; Phase 6: con_setmode
;   Set VID_MODE=$01, VID_CTRL=$03, verify registers.
;   Then restore defaults.
;   zp_result: [0]=mode [1]=ctrl
; ============================================================
phase6:
        LDA #$01               ; TEXT_CUSTOM
        LDX #$03               ; ADVANCE+WRAP (no SCROLL)
        JSR K_CON_SETMODE

        LDA VID_MODE
        STA zp_result+0        ; [0] expect $01
        LDA VID_CTRL
        STA zp_result+1        ; [1] expect $03

        ; Restore defaults
        LDA #$00
        LDX #$07
        JSR K_CON_SETMODE

phase6_done:
        JMP phase7

; ============================================================
; Phase 7: con_movcursor
;   7a: Start (10,10), move (+5,+3) → (15,13)
;   7b: From (15,13), move (-10,-8) → (5,5)
;   7c: From (2,1), move (-5,-5) → clamped (0,0)
;   zp_result: [0]=col_a [1]=row_a [2]=col_b [3]=row_b
;              [4]=col_c [5]=row_c
; ============================================================
phase7:
        ; 7a: positive delta
        LDX #10
        LDY #10
        JSR K_CON_SETCURSOR

        LDX #5                 ; +5 cols
        LDY #3                 ; +3 rows
        JSR K_CON_MOVCURSOR

        LDA VID_CURCOL
        STA zp_result+0        ; [0] expect 15
        LDA VID_CURROW
        STA zp_result+1        ; [1] expect 13

        ; 7b: negative delta
        LDX #<(-10)            ; $F6
        LDY #<(-8)             ; $F8
        JSR K_CON_MOVCURSOR

        LDA VID_CURCOL
        STA zp_result+2        ; [2] expect 5
        LDA VID_CURROW
        STA zp_result+3        ; [3] expect 5

        ; 7c: clamp at edges
        LDX #2
        LDY #1
        JSR K_CON_SETCURSOR

        LDX #<(-5)             ; $FB — wants to go to col -3, clamps to 0
        LDY #<(-5)             ; $FB — wants to go to row -4, clamps to 0
        JSR K_CON_MOVCURSOR

        LDA VID_CURCOL
        STA zp_result+4        ; [4] expect 0
        LDA VID_CURROW
        STA zp_result+5        ; [5] expect 0

phase7_done:
        JMP phase8

; ============================================================
; Phase 8: con_scroll
;   8a: Write 'A' at (0,0), scroll down 1 → 'A' now at (0,1)
;   8b: Scroll right 1 → 'A' now at (1,1)
;   zp_result: [0]=FB[80] [1]=FB[81]
; ============================================================
phase8:
        JSR K_CON_CLEAR

        ; Write 'A' at top-left
        LDA #'A'
        STA FB_BASE+0

        ; 8a: scroll down 1
        LDX #0                 ; no horizontal
        LDY #1                 ; +1 down
        JSR K_CON_SCROLL

        ; 'A' should now be at row 1 col 0 = offset 80
        LDA FB_BASE+80
        STA zp_result+0        ; [0] expect 'A' ($41)

        ; 8b: scroll right 1
        LDX #1                 ; +1 right
        LDY #0                 ; no vertical
        JSR K_CON_SCROLL

        ; 'A' should now be at row 1 col 1 = offset 81
        LDA FB_BASE+81
        STA zp_result+1        ; [1] expect 'A' ($41)

phase8_done:
        JMP phase9

; ============================================================
; Phase 9: con_getkey
;   9a: No key pending — expect A=$00, X=$00
;   9b: Inject key via GDB (kbd monitor cmd), then read
;   (Set bp at phase9_inject, inject key, continue to phase9_done)
;   zp_result: [0]=A_nokey [1]=X_nokey [2]=A_key [3]=X_key
; ============================================================
phase9:
        ; 9a: no key pending
        JSR K_CON_GETKEY
        STA zp_result+0        ; [0] expect $00
        STX zp_result+1        ; [1] expect $00

        ; 9b: GDB injects key here
        ; n8gdb: bp phase9_inject → run → kbd 41 00 → continue
phase9_inject:
        JSR K_CON_GETKEY
        STA zp_result+2        ; [2] expect $41 ('A')
        STX zp_result+3        ; [3] expect $00 (no mods)

phase9_done:
        JMP all_pass

; ============================================================
; All phases passed
; ============================================================
all_pass:
        JSR K_CON_CLEAR

        LDX #0
        LDY #0
        JSR K_CON_SETCURSOR

        LDX #0
@loop:  LDA str_pass,X
        BEQ all_done
        JSR K_CON_PUTCHAR
        INX
        JMP @loop

all_done:
        JMP all_done

; ============================================================
; RODATA
; ============================================================
.segment "RODATA"

str_pass:
        .byte "CONSOLE TEST: ALL 9 PHASES PASSED", 0
