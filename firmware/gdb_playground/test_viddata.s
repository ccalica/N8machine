; test_viddata.s - VID_DATA / VID_CTRL / VID_STATUS register validation
;
; 10 test phases exercising all new video registers and VID_OPER codes.
; Each phase stores results in zp_result ($00-$0F) for GDB inspection.
; Labeled checkpoints (phase1_done..phase10_done) serve as breakpoints.
;
; Expected workflow with n8gdb:
;   load test_viddata 0xE000
;   reset
;   bp phase1_done
;   ...
;   bp all_done
;   run
;   (verify zp_result at each breakpoint)
;
; Phase summary:
;   1  VID_OPER CLEAR — fill FB $20, cursor (0,0), status 0
;   2  VID_DATA write + ADVANCE — "HELLO" at origin
;   3  ADVANCE + WRAP across row boundary
;   4  ADVANCE + WRAP + SCROLL at bottom-right corner
;   5  ADVANCE only (no wrap) — OVERFLOW at col boundary
;   6  VID_DATA read + ADVANCE
;   7  Read never scrolls (SCROLL bit ignored on read)
;   8  Cursor movement VID_OPER codes + clamping
;   9  Stream write — fill row with OVERFLOW stop
;  10  Stream read — read back row with OVERFLOW stop

.export   _main
.export   phase1_done, phase2_done, phase3_done, phase4_done
.export   phase5_done, phase6_done, phase7_done, phase8_done
.export   phase9_done, phase10_done, all_done

; --- Hardware registers (absolute addresses) ---
VID_OPER    = $D844
VID_CURCOL  = $D846
VID_CURROW  = $D847
VID_CTRL    = $D849
VID_DATA    = $D84A
VID_STATUS  = $D84B

FB_BASE     = $C000

; --- VID_OPER codes ---
VIDOP_SCROLL_UP    = $01
VIDOP_CLEAR        = $05
VIDOP_CURSOR_UP    = $06
VIDOP_CURSOR_DOWN  = $07
VIDOP_CURSOR_LEFT  = $08
VIDOP_CURSOR_RIGHT = $09
VIDOP_CURSOR_HOME  = $0A

; --- VID_CTRL bits ---
VIDCTRL_ADVANCE = $01
VIDCTRL_WRAP    = $02
VIDCTRL_SCROLL  = $04
VIDCTRL_ALL     = $07

; --- Zero page: test result storage ---
.segment "ZEROPAGE"
zp_result:   .res 16            ; $00-$0F: test result bytes for GDB

.segment "CODE"

_main:

; ============================================================
; Phase 1: VID_OPER CLEAR
;   Dirty FB with $FF, then CLEAR.
;   Expect: FB=$20, cursor (0,0), status=0
;   zp_result: [0]=FB[0] [1]=FB[79] [2]=FB[1999] [3]=col [4]=row [5]=status
; ============================================================
phase1:
        ; Dirty first 512 bytes of FB
        LDA #$FF
        LDX #0
@fill:  STA FB_BASE,X
        STA FB_BASE+$100,X
        INX
        BNE @fill

        ; Move cursor away from origin
        LDA #40
        STA VID_CURCOL
        LDA #12
        STA VID_CURROW

        ; CLEAR
        LDA #VIDOP_CLEAR
        STA VID_OPER

        ; Sample results
        LDA FB_BASE
        STA zp_result+0     ; [0] expect $20
        LDA FB_BASE+79
        STA zp_result+1     ; [1] expect $20
        ; Row 24, col 79 = 24*80+79 = 1999 = $07CF
        LDA FB_BASE+$07CF
        STA zp_result+2     ; [2] expect $20
        LDA VID_CURCOL
        STA zp_result+3     ; [3] expect 0
        LDA VID_CURROW
        STA zp_result+4     ; [4] expect 0
        LDA VID_STATUS
        STA zp_result+5     ; [5] expect 0

phase1_done:
        JMP phase2

; ============================================================
; Phase 2: VID_DATA write + ADVANCE
;   HOME, ADVANCE only, write "HELLO".
;   Expect: FB[0..4]="HELLO", col=5, row=0, status=0
;   zp_result: [0]..[4]=chars [5]=col [6]=row [7]=status
; ============================================================
phase2:
        LDA #VIDOP_CURSOR_HOME
        STA VID_OPER

        LDA #VIDCTRL_ADVANCE
        STA VID_CTRL

        LDA #'H'
        STA VID_DATA
        LDA #'E'
        STA VID_DATA
        LDA #'L'
        STA VID_DATA
        LDA #'L'
        STA VID_DATA
        LDA #'O'
        STA VID_DATA

        LDA FB_BASE+0
        STA zp_result+0     ; [0] expect 'H' ($48)
        LDA FB_BASE+1
        STA zp_result+1     ; [1] expect 'E' ($45)
        LDA FB_BASE+2
        STA zp_result+2     ; [2] expect 'L' ($4C)
        LDA FB_BASE+3
        STA zp_result+3     ; [3] expect 'L' ($4C)
        LDA FB_BASE+4
        STA zp_result+4     ; [4] expect 'O' ($4F)
        LDA VID_CURCOL
        STA zp_result+5     ; [5] expect 5
        LDA VID_CURROW
        STA zp_result+6     ; [6] expect 0
        LDA VID_STATUS
        STA zp_result+7     ; [7] expect 0

phase2_done:
        JMP phase3

; ============================================================
; Phase 3: ADVANCE + WRAP across row boundary
;   CLEAR, cursor at (78,0), write 'X','Y','Z' with ADVANCE+WRAP.
;   X at (78,0), Y at (79,0), Z wraps to (0,1). Cursor at (1,1).
;   zp_result: [0]=FB[78] [1]=FB[79] [2]=FB[80] [3]=col [4]=row [5]=status
; ============================================================
phase3:
        LDA #VIDOP_CLEAR
        STA VID_OPER

        LDA #(VIDCTRL_ADVANCE | VIDCTRL_WRAP)
        STA VID_CTRL

        LDA #78
        STA VID_CURCOL
        LDA #0
        STA VID_CURROW

        LDA #'X'
        STA VID_DATA         ; write (78,0) → advance to (79,0)
        LDA #'Y'
        STA VID_DATA         ; write (79,0) → wrap to (0,1)
        LDA #'Z'
        STA VID_DATA         ; write (0,1)  → advance to (1,1)

        LDA FB_BASE+78
        STA zp_result+0     ; [0] expect 'X' ($58)
        LDA FB_BASE+79
        STA zp_result+1     ; [1] expect 'Y' ($59)
        LDA FB_BASE+80
        STA zp_result+2     ; [2] expect 'Z' ($5A)
        LDA VID_CURCOL
        STA zp_result+3     ; [3] expect 1
        LDA VID_CURROW
        STA zp_result+4     ; [4] expect 1
        LDA VID_STATUS
        STA zp_result+5     ; [5] expect 0

phase3_done:
        JMP phase4

; ============================================================
; Phase 4: ADVANCE + WRAP + SCROLL at bottom-right
;   CLEAR. Write 'M' at (0,1) as scroll marker.
;   Cursor at (79,24). Write '!' → wrap to row 25 → scroll → (0,24).
;   Write '@' at (0,24) → advance to (1,24).
;   After scroll: row 0 gets old row 1's data ('M' at col 0).
;   zp_result: [0]=FB[0] [1]=FB[$0780] [2]=col [3]=row [4]=status [5]=FB[$077F]
; ============================================================
phase4:
        LDA #VIDOP_CLEAR
        STA VID_OPER

        LDA #VIDCTRL_ALL
        STA VID_CTRL

        ; Place scroll marker 'M' at (0,1) via VID_DATA
        LDA #0
        STA VID_CURCOL
        LDA #1
        STA VID_CURROW
        LDA #'M'
        STA VID_DATA         ; writes at offset 80, advances to (1,1)

        ; Position at bottom-right corner
        LDA #79
        STA VID_CURCOL
        LDA #24
        STA VID_CURROW

        LDA #'!'
        STA VID_DATA         ; write (79,24) → wrap → row 25 → scroll → (0,24)
        LDA #'@'
        STA VID_DATA         ; write (0,24) → advance to (1,24)

        ; After scroll, old row 1 is now row 0
        LDA FB_BASE+0
        STA zp_result+0     ; [0] expect 'M' ($4D)
        ; Row 24 col 0 = offset 1920 = $0780
        LDA FB_BASE+$0780
        STA zp_result+1     ; [1] expect '@' ($40)
        LDA VID_CURCOL
        STA zp_result+2     ; [2] expect 1
        LDA VID_CURROW
        STA zp_result+3     ; [3] expect 24
        LDA VID_STATUS
        STA zp_result+4     ; [4] expect 0
        ; '!' should be at row 23 col 79 (was row 24 col 79, scrolled up)
        ; row 23 col 79 = 23*80+79 = 1919 = $077F
        LDA FB_BASE+$077F
        STA zp_result+5     ; [5] expect '!' ($21)

phase4_done:
        JMP phase5

; ============================================================
; Phase 5: ADVANCE without WRAP — OVERFLOW at col boundary
;   CLEAR. Cursor (79,0). ADVANCE only. Write 'A'.
;   Col should clamp to 79, OVERFLOW set.
;   Then write VID_CTRL to clear overflow.
;   zp_result: [0]=col [1]=row [2]=status [3]=status_after_clear
; ============================================================
phase5:
        LDA #VIDOP_CLEAR
        STA VID_OPER

        LDA #VIDCTRL_ADVANCE
        STA VID_CTRL

        LDA #79
        STA VID_CURCOL
        LDA #0
        STA VID_CURROW

        LDA #'A'
        STA VID_DATA         ; write (79,0) → advance → clamp col=79, OVERFLOW

        LDA VID_CURCOL
        STA zp_result+0     ; [0] expect 79 ($4F)
        LDA VID_CURROW
        STA zp_result+1     ; [1] expect 0
        LDA VID_STATUS
        STA zp_result+2     ; [2] expect $01 (OVERFLOW)

        ; Writing VID_CTRL clears OVERFLOW
        LDA #VIDCTRL_ALL
        STA VID_CTRL
        LDA VID_STATUS
        STA zp_result+3     ; [3] expect 0

phase5_done:
        JMP phase6

; ============================================================
; Phase 6: VID_DATA read + ADVANCE
;   Write "ABC" directly to FB. HOME. ADVANCE only. Read 3 bytes.
;   zp_result: [0]='A' [1]='B' [2]='C' [3]=col
; ============================================================
phase6:
        LDA #VIDOP_CLEAR
        STA VID_OPER

        ; Write directly to FB (not via VID_DATA, to isolate read test)
        LDA #'A'
        STA FB_BASE+0
        LDA #'B'
        STA FB_BASE+1
        LDA #'C'
        STA FB_BASE+2

        LDA #VIDOP_CURSOR_HOME
        STA VID_OPER

        LDA #VIDCTRL_ADVANCE
        STA VID_CTRL

        LDA VID_DATA         ; read FB[0]='A', advance col to 1
        STA zp_result+0     ; [0] expect 'A' ($41)
        LDA VID_DATA         ; read FB[1]='B', advance col to 2
        STA zp_result+1     ; [1] expect 'B' ($42)
        LDA VID_DATA         ; read FB[2]='C', advance col to 3
        STA zp_result+2     ; [2] expect 'C' ($43)
        LDA VID_CURCOL
        STA zp_result+3     ; [3] expect 3

phase6_done:
        JMP phase7

; ============================================================
; Phase 7: Read never scrolls (SCROLL bit ignored)
;   CLEAR. Put 'K' at FB[0]. Full CTRL ($07). Cursor (79,24).
;   Read VID_DATA — should wrap to row 25, clamp to 24, OVERFLOW.
;   FB[0] must still be 'K' (no scroll happened).
;   zp_result: [0]=read_val [1]=FB[0] [2]=col [3]=row [4]=status
; ============================================================
phase7:
        LDA #VIDOP_CLEAR
        STA VID_OPER

        ; Place marker directly in FB
        LDA #'K'
        STA FB_BASE+0

        LDA #VIDCTRL_ALL
        STA VID_CTRL

        LDA #79
        STA VID_CURCOL
        LDA #24
        STA VID_CURROW

        LDA VID_DATA         ; read (79,24) → advance → wrap → row 25 → clamp 24, OVERFLOW
        STA zp_result+0     ; [0] = byte read (whatever was at (79,24))

        LDA FB_BASE+0
        STA zp_result+1     ; [1] expect 'K' ($4B) — no scroll
        LDA VID_CURCOL
        STA zp_result+2     ; [2] expect 0 (wrapped)
        LDA VID_CURROW
        STA zp_result+3     ; [3] expect 24 (clamped)
        LDA VID_STATUS
        STA zp_result+4     ; [4] expect $01 (OVERFLOW)

phase7_done:
        JMP phase8

; ============================================================
; Phase 8: Cursor movement VID_OPER codes + clamping
;   Tests UP/DOWN/LEFT/RIGHT + boundary clamping + HOME.
;   zp_result: [0]=row_after_up [1]=row_after_down [2]=col_after_left
;     [3]=col_after_right [4]=row_at_0_up [5]=row_at_24_down
;     [6]=col_at_0_left [7]=col_at_79_right [8]=col_home [9]=row_home
; ============================================================
phase8:
        ; Start at (10, 10)
        LDA #10
        STA VID_CURCOL
        STA VID_CURROW

        ; UP → row 9
        LDA #VIDOP_CURSOR_UP
        STA VID_OPER
        LDA VID_CURROW
        STA zp_result+0     ; [0] expect 9

        ; DOWN → row 10
        LDA #VIDOP_CURSOR_DOWN
        STA VID_OPER
        LDA VID_CURROW
        STA zp_result+1     ; [1] expect 10

        ; LEFT → col 9
        LDA #VIDOP_CURSOR_LEFT
        STA VID_OPER
        LDA VID_CURCOL
        STA zp_result+2     ; [2] expect 9

        ; RIGHT → col 10
        LDA #VIDOP_CURSOR_RIGHT
        STA VID_OPER
        LDA VID_CURCOL
        STA zp_result+3     ; [3] expect 10

        ; Clamp: UP from row 0
        LDA #0
        STA VID_CURROW
        LDA #VIDOP_CURSOR_UP
        STA VID_OPER
        LDA VID_CURROW
        STA zp_result+4     ; [4] expect 0

        ; Clamp: DOWN from row 24
        LDA #24
        STA VID_CURROW
        LDA #VIDOP_CURSOR_DOWN
        STA VID_OPER
        LDA VID_CURROW
        STA zp_result+5     ; [5] expect 24

        ; Clamp: LEFT from col 0
        LDA #0
        STA VID_CURCOL
        LDA #VIDOP_CURSOR_LEFT
        STA VID_OPER
        LDA VID_CURCOL
        STA zp_result+6     ; [6] expect 0

        ; Clamp: RIGHT from col 79
        LDA #79
        STA VID_CURCOL
        LDA #VIDOP_CURSOR_RIGHT
        STA VID_OPER
        LDA VID_CURCOL
        STA zp_result+7     ; [7] expect 79

        ; HOME from (40, 12)
        LDA #40
        STA VID_CURCOL
        LDA #12
        STA VID_CURROW
        LDA #VIDOP_CURSOR_HOME
        STA VID_OPER
        LDA VID_CURCOL
        STA zp_result+8     ; [8] expect 0
        LDA VID_CURROW
        STA zp_result+9     ; [9] expect 0

phase8_done:
        JMP phase9

; ============================================================
; Phase 9: Stream write — fill row 0 using OVERFLOW stop
;   HOME. ADVANCE only (no wrap). Write 'A' in loop.
;   Stop when VID_STATUS OVERFLOW. Count chars written.
;   zp_result: [0]=count [1]=col [2]=row [3]=FB[0] [4]=FB[79]
; ============================================================
phase9:
        LDA #VIDOP_CLEAR
        STA VID_OPER
        LDA #VIDOP_CURSOR_HOME
        STA VID_OPER

        LDA #VIDCTRL_ADVANCE
        STA VID_CTRL

        LDX #0               ; char counter
@loop:  LDA #'A'
        STA VID_DATA
        INX
        LDA VID_STATUS
        AND #$01
        BNE @overflow
        JMP @loop
@overflow:
        STX zp_result+0     ; [0] expect 80 ($50)
        LDA VID_CURCOL
        STA zp_result+1     ; [1] expect 79 ($4F)
        LDA VID_CURROW
        STA zp_result+2     ; [2] expect 0
        LDA FB_BASE+0
        STA zp_result+3     ; [3] expect 'A' ($41)
        LDA FB_BASE+79
        STA zp_result+4     ; [4] expect 'A' ($41)

phase9_done:
        JMP phase10

; ============================================================
; Phase 10: Stream read — read back row 0 using OVERFLOW stop
;   HOME. ADVANCE only. Read until OVERFLOW. Verify all 'A'.
;   zp_result: [0]=count [1]=all_match_flag
; ============================================================
phase10:
        LDA #VIDOP_CURSOR_HOME
        STA VID_OPER

        LDA #VIDCTRL_ADVANCE
        STA VID_CTRL

        LDX #0               ; byte counter
        LDY #1               ; "all match" flag (1=yes)
@loop:  LDA VID_DATA
        CMP #'A'
        BEQ @match
        LDY #0               ; mismatch
@match: INX
        LDA VID_STATUS
        AND #$01
        BNE @done
        JMP @loop
@done:
        STX zp_result+0     ; [0] expect 80 ($50)
        STY zp_result+1     ; [1] expect 1

phase10_done:
        JMP all_pass

; ============================================================
; All phases passed — display result using VID_DATA streaming
; ============================================================
all_pass:
        LDA #VIDOP_CLEAR
        STA VID_OPER

        LDA #VIDCTRL_ALL
        STA VID_CTRL

        LDX #0
@loop:  LDA str_pass,X
        BEQ all_done
        STA VID_DATA
        INX
        JMP @loop

all_done:
        JMP all_done

; ============================================================
; RODATA
; ============================================================
.segment "RODATA"

str_pass:
        .byte "VID_DATA TEST: ALL 10 PHASES PASSED", 0
