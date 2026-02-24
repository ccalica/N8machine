; test_demo.s - 80s Demoscene Intro for N8 Machine
;
; Cycles through 8 visual effects using the full 256-character font
; (shades, box drawing, block graphics, ASCII art).
;
; Load via n8gdb:
;   load test_demo 0x0400
;   reset
;   run
;
; Effects:
;   1. Starfield      (~4s)   3-layer parallax scrolling stars
;   2. Logo reveal    (~5s)   Box-art "N8 MACHINE" revealed progressively
;   3. Plasma         (~5s)   Distance-based shade cycling
;   4. Fire           (~4s)   Bottom-up heat propagation with cooling LUT
;   5. Matrix rain    (~4s)   Falling characters with shade trail
;   6. Sine scroller  (~4s)   Horizontal text with sinusoidal Y offset
;   7. Tunnel zoom    (~4s)   Concentric box-drawing rectangles
;   8. Credits scroll (~5s)   Hardware-scrolled credit roll

; ================================================================
; Hardware Constants
; ================================================================

FB_BASE      = $C000
VID_OPER     = $D844
VID_CURSOR   = $D845
VID_CURCOL   = $D846
VID_CURROW   = $D847
VID_VSYNC    = $D848

VIDOP_SCROLL_UP = $01

NUM_STAR1    = 20
NUM_STAR2    = 20
NUM_STAR3    = 15
SCREEN_W     = 80
SCREEN_H     = 25

; ================================================================
; Zero Page Variables
; ================================================================

.segment "ZEROPAGE"

last_vsync:      .res 1
scene_timer_lo:  .res 1
scene_timer_hi:  .res 1
rand_seed:       .res 2        ; 16-bit PRNG state (must be non-zero)
row:             .res 1
col:             .res 1
zp_fb:           .res 2        ; frame buffer pointer
fx_ptr:          .res 2        ; general-purpose pointer
fx_tmp1:         .res 1
fx_tmp2:         .res 1
fx_tmp3:         .res 1
fx_tmp4:         .res 1
fx_phase:        .res 1        ; animation phase / time counter
reveal_lo:       .res 1        ; logo reveal position (16-bit)
reveal_hi:       .res 1
scroll_pos:      .res 1        ; sine scroller offset
credits_idx:     .res 1        ; credits line index
credits_delay:   .res 1        ; credits scroll delay counter

; ================================================================
; BSS Variables ($0200-$03FF, not in output binary)
; ================================================================

.segment "BSS"

star1_x:     .res NUM_STAR1
star1_y:     .res NUM_STAR1
star2_x:     .res NUM_STAR2
star2_y:     .res NUM_STAR2
star3_x:     .res NUM_STAR3
star3_y:     .res NUM_STAR3
mat_pos:     .res SCREEN_W    ; matrix rain: Y position per column
mat_timer:   .res SCREEN_W    ; matrix rain: countdown timer per column

; ================================================================
; Vectors
; ================================================================

.segment "VECTORS"

    .addr nmi_handler          ; $FFFA: NMI
    .addr _main                ; $FFFC: RESET
    .addr irq_handler          ; $FFFE: IRQ/BRK

; ================================================================
; CODE
; ================================================================

.segment "CODE"

.export _main

nmi_handler:
irq_handler:
    RTI

; ----------------------------------------------------------------
; Entry point
; ----------------------------------------------------------------

_main:
    LDX #$FF
    TXS
    CLD
    SEI

    ; Seed PRNG with non-zero value
    LDA #$37
    STA rand_seed
    LDA #$A9
    STA rand_seed+1

    ; Cursor off
    LDA #$00
    STA VID_CURSOR

    ; Init vsync tracking
    LDA VID_VSYNC
    STA last_vsync

scene_loop:
    JSR scene_starfield
    JSR scene_logo
    JSR scene_plasma
    JSR scene_fire
    JSR scene_matrix
    JSR scene_sinescroll
    JSR scene_tunnel
    JSR scene_credits
    JMP scene_loop

; ================================================================
; Utility Routines
; ================================================================

; Wait for next VSync (frame boundary)
wait_vsync:
    LDA VID_VSYNC
    CMP last_vsync
    BEQ wait_vsync
    STA last_vsync
    RTS

; Clear screen with space ($20)
clear_screen:
    LDA #$20
    JMP clear_screen_fill

; Clear screen with character in A
clear_screen_char:
clear_screen_fill:
    LDX #<FB_BASE
    STX zp_fb
    LDX #>FB_BASE
    STX zp_fb+1
    LDX #$08               ; 8 pages = 2048 bytes (covers 80x25=2000)
    LDY #$00
@page:
    STA (zp_fb),Y
    INY
    BNE @page
    INC zp_fb+1
    DEX
    BNE @page
    RTS

; Reset 16-bit scene timer to 0
reset_timer:
    LDA #$00
    STA scene_timer_lo
    STA scene_timer_hi
    RTS

; Increment 16-bit scene timer
inc_timer:
    INC scene_timer_lo
    BNE @done
    INC scene_timer_hi
@done:
    RTS

; 8-bit PRNG (Galois LFSR, polynomial $1D, period 255)
; Returns random byte in A
prng:
    LDA rand_seed
    ASL A
    BCC @no_eor
    EOR #$1D
@no_eor:
    STA rand_seed
    ; Also advance high byte for more variety
    INC rand_seed+1
    RTS

; Calculate FB address: zp_fb = FB_BASE + row_offset[row] + col
; Uses: row, col ZP variables
; Preserves: X
calc_fb_addr:
    LDY row
    LDA row_offset_lo,Y
    CLC
    ADC col
    STA zp_fb
    LDA row_offset_hi,Y
    ADC #>FB_BASE
    STA zp_fb+1
    RTS

; Set zp_fb = FB_BASE + row_offset[X]
; Destroys A
set_fb_row:
    LDA row_offset_lo,X
    CLC
    ADC #<FB_BASE
    STA zp_fb
    LDA row_offset_hi,X
    ADC #>FB_BASE
    STA zp_fb+1
    RTS

; ================================================================
; Scene 1: Starfield
; 3 layers, 55 stars, ~4s (240 frames)
; Layer 1 (20 stars, speed 1, '.' dim)
; Layer 2 (20 stars, speed 2, '*' medium)
; Layer 3 (15 stars, speed 4, bullet bright)
; ================================================================

scene_starfield:
    JSR clear_screen
    JSR reset_timer

    ; Init star positions from RODATA
    LDX #NUM_STAR1-1
@init1:
    LDA star1_init_x,X
    STA star1_x,X
    LDA star1_init_y,X
    STA star1_y,X
    DEX
    BPL @init1

    LDX #NUM_STAR2-1
@init2:
    LDA star2_init_x,X
    STA star2_x,X
    LDA star2_init_y,X
    STA star2_y,X
    DEX
    BPL @init2

    LDX #NUM_STAR3-1
@init3:
    LDA star3_init_x,X
    STA star3_x,X
    LDA star3_init_y,X
    STA star3_y,X
    DEX
    BPL @init3

@frame:
    JSR wait_vsync

    ; --- Erase, move, draw Layer 1 (speed 1, every 2nd frame) ---
    LDA scene_timer_lo
    AND #$01
    BNE @L1skip
    LDX #NUM_STAR1-1
@L1:
    STX fx_tmp1
    ; Erase old position
    LDA star1_y,X
    STA row
    LDA star1_x,X
    STA col
    JSR calc_fb_addr
    LDA #$20
    LDY #0
    STA (zp_fb),Y
    ; Move left by 1
    LDX fx_tmp1
    DEC star1_x,X
    BPL @L1draw
    ; Wrap: x=79, random y
    LDA #79
    STA star1_x,X
    JSR prng
@L1mod:
    CMP #SCREEN_H
    BCC @L1sety
    SBC #SCREEN_H
    JMP @L1mod
@L1sety:
    LDX fx_tmp1
    STA star1_y,X
@L1draw:
    LDX fx_tmp1
    LDA star1_y,X
    STA row
    LDA star1_x,X
    STA col
    JSR calc_fb_addr
    LDA #$2E               ; '.'
    LDY #0
    STA (zp_fb),Y
    LDX fx_tmp1
    DEX
    BPL @L1

@L1skip:
    ; --- Erase, move, draw Layer 2 (speed 1, '*') ---
    LDX #NUM_STAR2-1
@L2:
    STX fx_tmp1
    LDA star2_y,X
    STA row
    LDA star2_x,X
    STA col
    JSR calc_fb_addr
    LDA #$20
    LDY #0
    STA (zp_fb),Y
    LDX fx_tmp1
    LDA star2_x,X
    SEC
    SBC #1
    BCS @L2nowrap
    ; Wrap
    CLC
    ADC #SCREEN_W
    STA star2_x,X
    JSR prng
@L2mod:
    CMP #SCREEN_H
    BCC @L2sety
    SBC #SCREEN_H
    JMP @L2mod
@L2sety:
    LDX fx_tmp1
    STA star2_y,X
    JMP @L2draw
@L2nowrap:
    STA star2_x,X
@L2draw:
    LDX fx_tmp1
    LDA star2_y,X
    STA row
    LDA star2_x,X
    STA col
    JSR calc_fb_addr
    LDA #$2A               ; '*'
    LDY #0
    STA (zp_fb),Y
    LDX fx_tmp1
    DEX
    BPL @L2

    ; --- Erase, move, draw Layer 3 (speed 2, bullet $02) ---
    LDX #NUM_STAR3-1
@L3:
    STX fx_tmp1
    LDA star3_y,X
    STA row
    LDA star3_x,X
    STA col
    JSR calc_fb_addr
    LDA #$20
    LDY #0
    STA (zp_fb),Y
    LDX fx_tmp1
    LDA star3_x,X
    SEC
    SBC #2
    BCS @L3nowrap
    CLC
    ADC #SCREEN_W
    STA star3_x,X
    JSR prng
@L3mod:
    CMP #SCREEN_H
    BCC @L3sety
    SBC #SCREEN_H
    JMP @L3mod
@L3sety:
    LDX fx_tmp1
    STA star3_y,X
    JMP @L3draw
@L3nowrap:
    STA star3_x,X
@L3draw:
    LDX fx_tmp1
    LDA star3_y,X
    STA row
    LDA star3_x,X
    STA col
    JSR calc_fb_addr
    LDA #$02               ; bullet
    LDY #0
    STA (zp_fb),Y
    LDX fx_tmp1
    DEX
    BPL @L3

    ; Timer: 240 frames
    JSR inc_timer
    LDA scene_timer_hi
    BNE @done
    LDA scene_timer_lo
    CMP #240
    BCS @done
    JMP @frame
@done:
    RTS

; ================================================================
; Scene 2: Logo Reveal
; Reveal "N8 MACHINE" logo 6 chars per frame, ~5s (300 frames)
; ================================================================

scene_logo:
    JSR clear_screen
    JSR reset_timer
    LDA #$00
    STA reveal_lo
    STA reveal_hi

@frame:
    JSR wait_vsync

    ; Reveal 6 characters from logo_data to frame buffer
    ; logo_data is 40 wide, centered at column 20 on 80-col screen
    ; FB position = row * 80 + 20 + (reveal_pos mod 40)
    ; logo row = reveal_pos / 40

    LDX #6                 ; chars to reveal this frame
@reveal:
    ; Check if reveal_pos >= 520 (40*13)
    LDA reveal_hi
    CMP #>520
    BCC @do_reveal
    BNE @skip_reveal
    LDA reveal_lo
    CMP #<520
    BCS @skip_reveal

@do_reveal:
    ; Calculate source index
    LDA reveal_lo
    STA fx_ptr
    LDA reveal_hi
    STA fx_ptr+1

    ; Read character from logo_data
    ; logo_data address = logo_data + reveal_pos
    CLC
    LDA #<logo_data
    ADC reveal_lo
    STA fx_ptr
    LDA #>logo_data
    ADC reveal_hi
    STA fx_ptr+1
    LDY #0
    LDA (fx_ptr),Y
    STA fx_tmp2             ; character to write

    ; Calculate FB destination
    ; logo_row = reveal_pos / 40
    ; logo_col = reveal_pos mod 40
    ; Use repeated subtraction for /40
    LDA reveal_lo
    STA fx_tmp3
    LDA reveal_hi
    STA fx_tmp4
    LDA #0
    STA row                 ; row counter

@div40:
    ; Subtract 40 from fx_tmp3:fx_tmp4
    LDA fx_tmp3
    SEC
    SBC #40
    STA fx_tmp3
    LDA fx_tmp4
    SBC #0
    STA fx_tmp4
    BMI @div_done
    INC row
    JMP @div40

@div_done:
    ; Remainder = fx_tmp3 + 40 (since we subtracted one too many)
    LDA fx_tmp3
    CLC
    ADC #40
    STA col                 ; logo_col (0-39)

    ; FB address = FB_BASE + row_offset[logo_row+6] + logo_col + 20
    ; Logo starts at screen row 6 to center vertically
    LDA row
    CLC
    ADC #6                  ; vertical offset
    TAY
    LDA col
    CLC
    ADC #20                 ; horizontal offset (col+20 ≤ 59, no overflow)
    ADC row_offset_lo,Y     ; carry=0 from above, so single carry propagates
    STA zp_fb
    LDA row_offset_hi,Y
    ADC #>FB_BASE
    STA zp_fb+1

    ; Write character
    LDA fx_tmp2
    LDY #0
    STA (zp_fb),Y

    ; Advance reveal position
    INC reveal_lo
    BNE @skip_reveal
    INC reveal_hi

@skip_reveal:
    DEX
    BNE @reveal

    ; Timer: 300 frames ($012C)
    JSR inc_timer
    LDA scene_timer_hi
    CMP #1
    BCS :+
    JMP @frame
:   BNE @logodone
    LDA scene_timer_lo
    CMP #44                 ; 300 - 256 = 44
    BCS @logodone
    JMP @frame
@logodone:
    RTS

; ================================================================
; Scene 3: Plasma
; Distance-based shade cycling, 5 rows per frame batch, ~5s
; ================================================================

scene_plasma:
    JSR clear_screen
    JSR reset_timer
    LDA #0
    STA fx_phase

@frame:
    JSR wait_vsync

    ; Process all 25 rows (batched visually but we do all for smooth look)
    ; For each row: |row-12| precomputed, then for each col: shade_lut[|col-40|+|row-12|+phase]
    LDX #0                  ; row index
@prow:
    STX fx_tmp3             ; save row

    ; Set up FB pointer for this row
    JSR set_fb_row

    ; Compute |row - 12|
    TXA
    SEC
    SBC #12
    BCS @prow_pos
    EOR #$FF
    ADC #1
@prow_pos:
    STA fx_tmp1             ; |row - 12|

    ; Process columns 79 down to 0
    LDY #79
@pcol:
    ; Compute |col - 40|
    TYA
    SEC
    SBC #40
    BCS @pcol_pos
    EOR #$FF
    ADC #1
@pcol_pos:
    ; A = |col - 40|
    CLC
    ADC fx_tmp1             ; + |row - 12|
    CLC
    ADC fx_phase            ; + time
    TAX
    LDA shade_lut,X
    STA (zp_fb),Y

    DEY
    BPL @pcol

    LDX fx_tmp3
    INX
    CPX #SCREEN_H
    BCC @prow

    ; Advance phase
    INC fx_phase

    ; Timer: 300 frames
    JSR inc_timer
    LDA scene_timer_hi
    CMP #1
    BCC @frame
    BNE @pdone
    LDA scene_timer_lo
    CMP #44
    BCC @frame
@pdone:
    RTS

; ================================================================
; Scene 4: Fire
; Bottom-up heat propagation, 12 visible rows, ~4s (240 frames)
; ================================================================

scene_fire:
    JSR clear_screen
    JSR reset_timer

@fframe:
    JSR wait_vsync

    ; Randomize bottom row (row 24)
    LDX #24
    JSR set_fb_row
    LDY #79
@frand:
    JSR prng
    AND #$03               ; 0-3
    TAX
    LDA fire_bottom_chars,X
    LDX #24                ; restore X for set_fb_row result still in zp_fb
    STA (zp_fb),Y
    DEY
    BPL @frand

    ; Propagate fire upward: rows 23 down to 13
    ; For each row R: cell[R] = cool(cell[R+1])
    LDA #23
    STA fx_tmp3            ; current row

@frow:
    ; Set zp_fb to current row
    LDX fx_tmp3
    JSR set_fb_row
    ; Save pointers
    LDA zp_fb
    STA fx_tmp1
    LDA zp_fb+1
    STA fx_tmp2

    ; Set fx_ptr to row below (current + 1)
    LDX fx_tmp3
    INX
    LDA row_offset_lo,X
    CLC
    ADC #<FB_BASE
    STA fx_ptr
    LDA row_offset_hi,X
    ADC #>FB_BASE
    STA fx_ptr+1

    ; Restore zp_fb
    LDA fx_tmp1
    STA zp_fb
    LDA fx_tmp2
    STA zp_fb+1

    ; Process columns
    LDY #79
@fcol:
    LDA (fx_ptr),Y         ; cell below
    TAX                    ; X = original char
    LDA fx_tmp3            ; cool only on even rows
    AND #$01
    BNE @fnocool
    LDA fire_cool_lut,X    ; cooled version
    JMP @fstore
@fnocool:
    TXA                    ; keep original
@fstore:
    STA (zp_fb),Y
    DEY
    BPL @fcol

    DEC fx_tmp3
    BPL @frow

    ; Timer: 240 frames
    JSR inc_timer
    LDA scene_timer_hi
    BNE @fdone
    LDA scene_timer_lo
    CMP #240
    BCC @fframe
@fdone:
    RTS

; ================================================================
; Scene 5: Matrix Rain
; 80 columns of falling chars with shade trail, ~4s (240 frames)
; ================================================================

scene_matrix:
    JSR clear_screen
    JSR reset_timer

    ; Initialize column positions and timers
    LDX #SCREEN_W-1
@minit:
    STX fx_tmp1
    JSR prng
@mmod:
    CMP #SCREEN_H
    BCC @mset
    SBC #SCREEN_H
    JMP @mmod
@mset:
    LDX fx_tmp1
    STA mat_pos,X
    JSR prng
    AND #$0F
    CLC
    ADC #8                  ; speed 8-23 frames per advance
    STA mat_timer,X
    DEX
    BPL @minit

@mframe:
    JSR wait_vsync

    ; Process all 80 columns
    LDX #SCREEN_W-1
@mcol:
    STX fx_tmp1

    ; Decrement timer
    DEC mat_timer,X
    BEQ :+
    JMP @mnext
:

    ; Timer expired: advance this column
    ; Reset timer (speed 8-23)
    JSR prng
    AND #$0F
    CLC
    ADC #8
    LDX fx_tmp1
    STA mat_timer,X

    ; Draw trail at current head position
    LDA mat_pos,X
    STA row
    STX col                 ; col = X (column index)

    ; Draw random char at head
    JSR prng
    AND #$3F
    CLC
    ADC #$21               ; random printable $21-$60
    PHA                    ; save char

    LDA row
    CMP #SCREEN_H
    BCC :+
    JMP @mskip_draw        ; out of bounds
:
    JSR calc_fb_addr
    PLA
    LDY #0
    STA (zp_fb),Y

    ; Draw fading trail behind head
    ; pos-1: dark shade $91
    LDX fx_tmp1
    LDA mat_pos,X
    SEC
    SBC #1
    BMI @mtrail2
    STA row
    LDA fx_tmp1
    STA col
    JSR calc_fb_addr
    LDA #$91
    LDY #0
    STA (zp_fb),Y

@mtrail2:
    ; pos-2: medium shade $90
    LDX fx_tmp1
    LDA mat_pos,X
    SEC
    SBC #2
    BMI @mtrail3
    STA row
    LDA fx_tmp1
    STA col
    JSR calc_fb_addr
    LDA #$90
    LDY #0
    STA (zp_fb),Y

@mtrail3:
    ; pos-3: light shade $8F
    LDX fx_tmp1
    LDA mat_pos,X
    SEC
    SBC #3
    BMI @mtrail4
    STA row
    LDA fx_tmp1
    STA col
    JSR calc_fb_addr
    LDA #$8F
    LDY #0
    STA (zp_fb),Y

@mtrail4:
    ; pos-4: erase (space)
    LDX fx_tmp1
    LDA mat_pos,X
    SEC
    SBC #4
    BMI @madvance
    STA row
    LDA fx_tmp1
    STA col
    JSR calc_fb_addr
    LDA #$20
    LDY #0
    STA (zp_fb),Y

@madvance:
    ; Advance position
    LDX fx_tmp1
    INC mat_pos,X
    LDA mat_pos,X
    CMP #SCREEN_H + 5      ; let trail fully exit before wrap
    BCC @mnext
    ; Wrap to top
    LDA #0
    STA mat_pos,X
    JMP @mnext

@mskip_draw:
    PLA                    ; discard saved char
@mnext:
    LDX fx_tmp1
    DEX
    BMI :+
    JMP @mcol
:
    ; Timer: 480 frames ($01E0)
    JSR inc_timer
    LDA scene_timer_hi
    CMP #1
    BCC :+
    BNE @mdone
    LDA scene_timer_lo
    CMP #224                ; 480 - 256 = 224
    BCS @mdone
:   JMP @mframe
@mdone:
    RTS

; ================================================================
; Scene 6: Sine Scroller
; Horizontal text with sinusoidal Y offset, ~4s (240 frames)
; ================================================================

scene_sinescroll:
    JSR clear_screen
    JSR reset_timer
    LDA #0
    STA scroll_pos
    STA fx_phase

@sframe:
    JSR wait_vsync

    ; Clear rows 6-18 (sine wave area)
    LDX #6
@sclear_row:
    STX fx_tmp3
    JSR set_fb_row
    LDA #$20
    LDY #79
@sclear:
    STA (zp_fb),Y
    DEY
    BPL @sclear
    LDX fx_tmp3
    INX
    CPX #19
    BCC @sclear_row

    ; Draw 80 characters with sine Y offset
    LDX #0                  ; column counter
@scol:
    STX fx_tmp3

    ; msg_idx = (scroll_pos + col) mod scroll_msg_len
    TXA
    CLC
    ADC scroll_pos
@smod:
    CMP #SCROLL_MSG_LEN
    BCC @sget_char
    SBC #SCROLL_MSG_LEN
    JMP @smod
@sget_char:
    TAY
    LDA scroll_msg,Y
    STA fx_tmp2             ; character

    ; y = 12 + (sin_table[(col*4 + phase) & $FF] >> 5) - 4
    ; sin_table values 0-255: >>5 gives 0-7, subtract 4 gives -4..+3
    ; y = 8 + sin_table[...] >> 5 gives y = 8-15
    LDA fx_tmp3             ; col
    ASL A
    ASL A                   ; col * 4
    CLC
    ADC fx_phase            ; + phase
    TAY
    LDA sin_table,Y
    LSR A
    LSR A
    LSR A
    LSR A
    LSR A                   ; /32 = 0-7
    CLC
    ADC #9                  ; y = 9 to 16

    ; Write character at (y, col) in frame buffer
    TAY                     ; Y = row
    LDA row_offset_lo,Y
    CLC
    ADC fx_tmp3             ; + col
    STA zp_fb
    LDA row_offset_hi,Y
    ADC #>FB_BASE
    STA zp_fb+1

    LDA fx_tmp2
    LDY #0
    STA (zp_fb),Y

    LDX fx_tmp3
    INX
    CPX #SCREEN_W
    BCC @scol

    ; Advance scroll and phase
    INC scroll_pos
    LDA scroll_pos
    CMP #SCROLL_MSG_LEN
    BCC @snowrap
    LDA #0
    STA scroll_pos
@snowrap:
    INC fx_phase

    ; Timer: 240 frames
    JSR inc_timer
    LDA scene_timer_hi
    BNE @sdone
    LDA scene_timer_lo
    CMP #240
    BCC @sframe
@sdone:
    RTS

; ================================================================
; Scene 7: Tunnel Zoom
; 6 concentric rectangles, cycling offset, ~4s (240 frames)
; ================================================================

scene_tunnel:
    JSR clear_screen
    JSR reset_timer
    LDA #0
    STA fx_phase

@tframe:
    JSR wait_vsync

    ; Draw 6 rectangles with cycling character styles
    LDX #0                  ; rect index (0-5)
@trect:
    STX fx_tmp3

    ; Choose corner characters based on (rect + phase) & 1
    ; Even: regular corners ($A2-$A5), Odd: rounded ($B6-$B9)
    TXA
    CLC
    ADC fx_phase
    AND #$01
    BNE @tround
    ; Regular corners
    LDA #$A2
    STA fx_tmp1             ; TL corner
    LDA #$A3
    STA fx_tmp2             ; TR corner
    LDA #$A4
    STA fx_tmp4             ; BL corner
    ; BR corner in draw code
    JMP @tdraw
@tround:
    LDA #$B6
    STA fx_tmp1             ; TL round
    LDA #$B7
    STA fx_tmp2             ; TR round
    LDA #$B8
    STA fx_tmp4             ; BL round

@tdraw:
    LDX fx_tmp3
    ; Get rectangle params
    LDA tunnel_top,X
    STA row
    LDA tunnel_left,X
    STA col

    ; --- Draw top edge ---
    JSR calc_fb_addr
    ; TL corner
    LDA fx_tmp1
    LDY #0
    STA (zp_fb),Y
    ; Horizontal line
    LDA #$A0
    LDX fx_tmp3
    LDA tunnel_right,X
    STA fx_tmp2             ; right edge column
    LDY tunnel_left,X
    INY                     ; start after TL corner
@ttop:
    CPY fx_tmp2
    BCS @ttop_end
    ; calc offset from row start
    STY col
    STX fx_tmp1             ; temp save rect idx (reuse fx_tmp1 since corner already drawn)
    LDX row
    JSR set_fb_row          ; zp_fb = row start
    LDA #$A0
    LDY col
    STA (zp_fb),Y
    LDX fx_tmp1
    LDY col
    INY
    JMP @ttop
@ttop_end:
    ; TR corner
    LDX fx_tmp3
    LDA tunnel_right,X
    STA col
    LDA tunnel_top,X
    STA row
    JSR calc_fb_addr
    ; Determine TR corner char
    LDX fx_tmp3
    TXA
    CLC
    ADC fx_phase
    AND #$01
    BNE @ttr_round
    LDA #$A3
    JMP @ttr_write
@ttr_round:
    LDA #$B7
@ttr_write:
    LDY #0
    STA (zp_fb),Y

    ; --- Draw bottom edge ---
    LDX fx_tmp3
    LDA tunnel_bottom,X
    STA row
    LDA tunnel_left,X
    STA col
    JSR calc_fb_addr
    ; BL corner
    LDX fx_tmp3
    TXA
    CLC
    ADC fx_phase
    AND #$01
    BNE @tbl_round
    LDA #$A4
    JMP @tbl_write
@tbl_round:
    LDA #$B8
@tbl_write:
    LDY #0
    STA (zp_fb),Y

    ; Bottom horizontal line
    LDX fx_tmp3
    LDA tunnel_bottom,X
    TAY                     ; Y = bottom row
    STY row
    LDY tunnel_left,X
    INY
@tbot:
    LDX fx_tmp3
    LDA tunnel_right,X
    STA fx_tmp2             ; right edge column
    CPY fx_tmp2
    BCS @tbot_end
    STY col
    JSR calc_fb_addr
    LDA #$A0
    LDY #0
    STA (zp_fb),Y
    LDY col
    INY
    JMP @tbot
@tbot_end:
    ; BR corner
    LDX fx_tmp3
    LDA tunnel_bottom,X
    STA row
    LDA tunnel_right,X
    STA col
    JSR calc_fb_addr
    LDX fx_tmp3
    TXA
    CLC
    ADC fx_phase
    AND #$01
    BNE @tbr_round
    LDA #$A5
    JMP @tbr_write
@tbr_round:
    LDA #$B9
@tbr_write:
    LDY #0
    STA (zp_fb),Y

    ; --- Draw left and right edges ---
    LDX fx_tmp3
    LDA tunnel_top,X
    CLC
    ADC #1
    STA fx_tmp1             ; start row (top+1)
    LDA tunnel_bottom,X
    STA fx_tmp2             ; end row (bottom)

@tvert:
    LDA fx_tmp1
    CMP fx_tmp2
    BCS @tvert_end

    ; Left edge
    STA row
    LDX fx_tmp3
    LDA tunnel_left,X
    STA col
    JSR calc_fb_addr
    LDA #$A1
    LDY #0
    STA (zp_fb),Y

    ; Right edge
    LDX fx_tmp3
    LDA tunnel_right,X
    STA col
    JSR calc_fb_addr
    LDA #$A1
    LDY #0
    STA (zp_fb),Y

    INC fx_tmp1
    JMP @tvert
@tvert_end:

    LDX fx_tmp3
    INX
    CPX #6
    BCC @trect_jmp
    JMP @tframe_timer
@trect_jmp:
    JMP @trect

@tframe_timer:
    ; Advance phase every 4 frames
    LDA scene_timer_lo
    AND #$03
    BNE @tnophase
    INC fx_phase
@tnophase:

    ; Timer: 240 frames
    JSR inc_timer
    LDA scene_timer_hi
    BNE @tdone
    LDA scene_timer_lo
    CMP #240
    BCS @tdone
    JMP @tframe
@tdone:
    RTS

; ================================================================
; Scene 8: Credits Scroll
; Hardware scroll up every 4 frames, ~5s (300 frames)
; ================================================================

scene_credits:
    JSR clear_screen
    JSR reset_timer
    LDA #0
    STA credits_idx
    LDA #4
    STA credits_delay

@cframe:
    JSR wait_vsync

    ; Decrement delay counter
    DEC credits_delay
    BNE @ctimer
    LDA #4
    STA credits_delay

    ; Hardware scroll up
    LDA #VIDOP_SCROLL_UP
    STA VID_OPER

    ; Write next credit line at row 24
    LDX #24
    JSR set_fb_row

    ; Clear row 24 first
    LDA #$20
    LDY #79
@cclear:
    STA (zp_fb),Y
    DEY
    BPL @cclear

    ; Get pointer to current credit string
    LDX credits_idx
    CPX #NUM_CREDITS
    BCS @ctimer             ; no more credits

    LDA credits_ptrs_lo,X
    STA fx_ptr
    LDA credits_ptrs_hi,X
    STA fx_ptr+1

    ; Calculate string length for centering
    LDY #0
@clen:
    LDA (fx_ptr),Y
    BEQ @clen_done
    INY
    BNE @clen
@clen_done:
    ; Y = string length
    STY fx_tmp1
    ; Starting column = (80 - length) / 2
    LDA #SCREEN_W
    SEC
    SBC fx_tmp1
    LSR A
    STA fx_tmp2             ; start column

    ; Write string to row 24
    LDX #24
    JSR set_fb_row
    ; Adjust zp_fb by start column
    CLC
    LDA zp_fb
    ADC fx_tmp2
    STA zp_fb
    LDA zp_fb+1
    ADC #0
    STA zp_fb+1

    LDY #0
@cwrite:
    LDA (fx_ptr),Y
    BEQ @cadv
    STA (zp_fb),Y
    INY
    BNE @cwrite

@cadv:
    INC credits_idx

@ctimer:
    ; Timer: 300 frames
    JSR inc_timer
    LDA scene_timer_hi
    CMP #1
    BCC @cframe
    BNE @cdone
    LDA scene_timer_lo
    CMP #44
    BCC @cframe
@cdone:
    RTS

; ================================================================
; RODATA Segment
; ================================================================

.segment "RODATA"

; ----------------------------------------------------------------
; Row offset lookup table (row * 80, 25 entries)
; Used by calc_fb_addr and set_fb_row
; ----------------------------------------------------------------

row_offset_lo:
    .byte $00, $50, $A0, $F0, $40, $90, $E0, $30
    .byte $80, $D0, $20, $70, $C0, $10, $60, $B0
    .byte $00, $50, $A0, $F0, $40, $90, $E0, $30
    .byte $80

row_offset_hi:
    .byte $00, $00, $00, $00, $01, $01, $01, $02
    .byte $02, $02, $03, $03, $03, $04, $04, $04
    .byte $05, $05, $05, $05, $06, $06, $06, $07
    .byte $07

; ----------------------------------------------------------------
; Sine table (256 entries: 128 + 127*sin(2*pi*i/256))
; ----------------------------------------------------------------

sin_table:
    .byte $80, $83, $86, $89, $8C, $90, $93, $96, $99, $9C, $9F, $A2, $A5, $A8, $AB, $AE
    .byte $B1, $B3, $B6, $B9, $BC, $BF, $C1, $C4, $C7, $C9, $CC, $CE, $D1, $D3, $D5, $D8
    .byte $DA, $DC, $DE, $E0, $E2, $E4, $E6, $E8, $EA, $EB, $ED, $EF, $F0, $F1, $F3, $F4
    .byte $F5, $F6, $F8, $F9, $FA, $FA, $FB, $FC, $FD, $FD, $FE, $FE, $FE, $FF, $FF, $FF
    .byte $FF, $FF, $FF, $FF, $FE, $FE, $FE, $FD, $FD, $FC, $FB, $FA, $FA, $F9, $F8, $F6
    .byte $F5, $F4, $F3, $F1, $F0, $EF, $ED, $EB, $EA, $E8, $E6, $E4, $E2, $E0, $DE, $DC
    .byte $DA, $D8, $D5, $D3, $D1, $CE, $CC, $C9, $C7, $C4, $C1, $BF, $BC, $B9, $B6, $B3
    .byte $B1, $AE, $AB, $A8, $A5, $A2, $9F, $9C, $99, $96, $93, $90, $8C, $89, $86, $83
    .byte $80, $7D, $7A, $77, $74, $70, $6D, $6A, $67, $64, $61, $5E, $5B, $58, $55, $52
    .byte $4F, $4D, $4A, $47, $44, $41, $3F, $3C, $39, $37, $34, $32, $2F, $2D, $2B, $28
    .byte $26, $24, $22, $20, $1E, $1C, $1A, $18, $16, $15, $13, $11, $10, $0F, $0D, $0C
    .byte $0B, $0A, $08, $07, $06, $06, $05, $04, $03, $03, $02, $02, $02, $01, $01, $01
    .byte $01, $01, $01, $01, $02, $02, $02, $03, $03, $04, $05, $06, $06, $07, $08, $0A
    .byte $0B, $0C, $0D, $0F, $10, $11, $13, $15, $16, $18, $1A, $1C, $1E, $20, $22, $24
    .byte $26, $28, $2B, $2D, $2F, $32, $34, $37, $39, $3C, $3F, $41, $44, $47, $4A, $4D
    .byte $4F, $52, $55, $58, $5B, $5E, $61, $64, $67, $6A, $6D, $70, $74, $77, $7A, $7D

; ----------------------------------------------------------------
; Shade LUT (256 entries: maps 0-255 to shade characters)
; $20=space, $8F=light, $90=medium, $91=dark, $80=full block
; ----------------------------------------------------------------

shade_lut:
    .byte $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20
    .byte $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20
    .byte $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20
    .byte $20, $20, $20, $20, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F
    .byte $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F
    .byte $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F, $8F
    .byte $8F, $8F, $8F, $8F, $8F, $8F, $8F, $90, $90, $90, $90, $90, $90, $90, $90, $90
    .byte $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90
    .byte $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $90
    .byte $90, $90, $90, $90, $90, $90, $90, $90, $90, $90, $91, $91, $91, $91, $91, $91
    .byte $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91
    .byte $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91
    .byte $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $91, $80, $80, $80
    .byte $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
    .byte $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80
    .byte $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80, $80

; ----------------------------------------------------------------
; Fire cooling LUT (256 entries)
; Maps each possible character to its cooled version.
; Only the 5 fire characters have non-space mappings.
; ----------------------------------------------------------------

fire_cool_lut:
    ; Default: everything maps to space
    .res 32, $20           ; $00-$1F -> space
    .byte $20              ; $20 (space) -> space
    .res 95, $20           ; $21-$7F -> space
    .byte $91              ; $80 (full block) -> dark shade
    .res 14, $20           ; $81-$8E -> space
    .byte $20              ; $8F (light shade) -> space
    .byte $8F              ; $90 (medium shade) -> light shade
    .byte $90              ; $91 (dark shade) -> medium shade
    .res 108, $20          ; $92-$FD -> space
    .byte $20, $20         ; $FE-$FF -> space

; Characters for random fire bottom row
fire_bottom_chars:
    .byte $80, $80, $91, $80

; ----------------------------------------------------------------
; Tunnel rectangle parameters (6 concentric rectangles)
; ----------------------------------------------------------------

tunnel_top:
    .byte 11, 9, 7, 5, 3, 1
tunnel_left:
    .byte 38, 32, 24, 16, 8, 2
tunnel_right:
    .byte 41, 47, 55, 63, 71, 77
tunnel_bottom:
    .byte 13, 15, 17, 19, 21, 23

; ----------------------------------------------------------------
; Star initial positions
; ----------------------------------------------------------------

; Layer 1 (20 stars, slow)
star1_init_x:
    .byte 5, 15, 23, 31, 42, 50, 58, 67, 74, 10
    .byte 20, 35, 48, 62, 70, 3, 28, 55, 40, 18
star1_init_y:
    .byte 2, 8, 14, 20, 5, 11, 17, 23, 1, 7
    .byte 13, 19, 3, 9, 15, 21, 6, 12, 18, 24

; Layer 2 (20 stars, medium)
star2_init_x:
    .byte 7, 19, 26, 38, 45, 53, 61, 72, 12, 33
    .byte 47, 59, 68, 76, 1, 22, 39, 56, 64, 14
star2_init_y:
    .byte 0, 4, 10, 16, 22, 3, 9, 15, 21, 6
    .byte 12, 18, 24, 1, 7, 13, 19, 2, 8, 14

; Layer 3 (15 stars, fast)
star3_init_x:
    .byte 9, 24, 37, 51, 66, 4, 29, 44, 58, 73
    .byte 16, 34, 49, 63, 78
star3_init_y:
    .byte 1, 6, 11, 16, 21, 3, 8, 13, 18, 23
    .byte 4, 9, 14, 19, 24

; ----------------------------------------------------------------
; Logo data (40 columns x 13 rows = 520 bytes)
; Displayed centered at screen column 20, row 6
; ----------------------------------------------------------------

logo_data:
    .byte $A2, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A3
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $80, $80, $20, $20, $20, $20, $80, $80, $20, $20, $20, $80, $80, $80, $80, $80, $80, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $80, $80, $80, $20, $20, $20, $80, $80, $20, $20, $80, $80, $20, $20, $20, $20, $80, $80, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $80, $80, $20, $80, $20, $20, $80, $80, $20, $20, $80, $80, $20, $20, $20, $20, $80, $80, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $80, $80, $20, $20, $80, $20, $80, $80, $20, $20, $20, $80, $80, $80, $80, $80, $80, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $80, $80, $20, $20, $20, $80, $80, $80, $20, $20, $80, $80, $20, $20, $20, $20, $80, $80, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $80, $80, $20, $20, $20, $20, $80, $80, $20, $20, $80, $80, $20, $20, $20, $20, $80, $80, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $80, $80, $20, $20, $20, $20, $80, $80, $20, $20, $20, $80, $80, $80, $80, $80, $80, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $4D, $20, $41, $20, $43, $20, $48, $20, $49, $20, $4E, $20, $45, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A1, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $20, $A1
    .byte $A4, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A0, $A5

; ----------------------------------------------------------------
; Scroll message for sine scroller
; ----------------------------------------------------------------

SCROLL_MSG_LEN = 80

scroll_msg:
    .byte "    N8 MACHINE  --  6502 HOMEBREW COMPUTER EMULATOR  --  GREETS TO ALL!      "
    ; exactly 80 characters

; ----------------------------------------------------------------
; Credits strings and pointer table
; ----------------------------------------------------------------

NUM_CREDITS = 25

credits_ptrs_lo:
    .byte <cr0,  <cr1,  <cr2,  <cr3,  <cr4
    .byte <cr5,  <cr6,  <cr7,  <cr8,  <cr9
    .byte <cr10, <cr11, <cr12, <cr13, <cr14
    .byte <cr15, <cr16, <cr17, <cr18, <cr19
    .byte <cr20, <cr21, <cr22, <cr23, <cr24
credits_ptrs_hi:
    .byte >cr0,  >cr1,  >cr2,  >cr3,  >cr4
    .byte >cr5,  >cr6,  >cr7,  >cr8,  >cr9
    .byte >cr10, >cr11, >cr12, >cr13, >cr14
    .byte >cr15, >cr16, >cr17, >cr18, >cr19
    .byte >cr20, >cr21, >cr22, >cr23, >cr24

cr0:  .asciiz ""
cr1:  .asciiz "N8 MACHINE DEMO"
cr2:  .asciiz ""
cr3:  .asciiz ""
cr4:  .asciiz "CODE & DESIGN"
cr5:  .asciiz "CARLO CALICA"
cr6:  .asciiz ""
cr7:  .asciiz ""
cr8:  .asciiz "HARDWARE EMULATION"
cr9:  .asciiz "6502 CORE BY FLOOOH"
cr10: .asciiz ""
cr11: .asciiz ""
cr12: .asciiz "FONT DESIGN"
cr13: .asciiz "PCF MODERN DOS HYBRID"
cr14: .asciiz ""
cr15: .asciiz ""
cr16: .asciiz "TOOLS"
cr17: .asciiz "CC65 TOOLCHAIN"
cr18: .asciiz "GDB REMOTE STUB"
cr19: .asciiz ""
cr20: .asciiz ""
cr21: .asciiz "BUILT WITH"
cr22: .asciiz "SDL2 + DEAR IMGUI + OPENGL"
cr23: .asciiz ""
cr24: .asciiz "THANKS FOR WATCHING!"

; ----------------------------------------------------------------
; Transition/pattern data for visual richness
; These pre-drawn patterns use the full charset range.
; Used as filler and available for future transition effects.
; ----------------------------------------------------------------

; Pattern 1: Shade wave (2000 bytes)
; Repeating 16-byte gradient: space -> light -> medium -> dark -> full -> dark -> ...
pattern_shade_wave:
.repeat 125
    .byte $20, $20, $8F, $8F, $90, $90, $91, $91, $80, $80, $91, $91, $90, $90, $8F, $8F
.endrepeat

; Pattern 2: Checkerboard (2000 bytes)
; Alternating full blocks and spaces
pattern_checkerboard:
.repeat 125
    .byte $80, $20, $80, $20, $80, $20, $80, $20, $20, $80, $20, $80, $20, $80, $20, $80
.endrepeat

; Pattern 3: Box drawing maze (2000 bytes)
; Repeating box segments
pattern_maze:
.repeat 100
    .byte $A2, $A0, $A0, $A3, $A2, $A0, $A0, $A3, $A1, $20, $20, $A1, $A1, $20, $20, $A1
    .byte $A4, $A0, $A0, $A5
.endrepeat

; Pattern 4: Block mosaic (2000 bytes)
; All block graphic characters cycling
pattern_mosaic:
.repeat 125
    .byte $80, $81, $82, $83, $84, $85, $86, $87, $88, $89, $8A, $8B, $8C, $8D, $8E, $80
.endrepeat

; Pattern 5: Diamond field (2000 bytes)
; Scattered diamond/bullet chars on space background
pattern_diamonds:
.repeat 125
    .byte $20, $20, $20, $04, $20, $20, $20, $20, $20, $02, $20, $20, $20, $20, $04, $20
.endrepeat

; Pattern 6: Heavy box grid (2000 bytes)
; Grid using heavy box-drawing characters
pattern_heavy_grid:
.repeat 100
    .byte $AD, $AB, $AB, $AB, $AE, $AD, $AB, $AB, $AB, $AE, $AC, $20, $20, $20, $AC, $AC
    .byte $20, $20, $20, $AC
.endrepeat

; Pattern 7: Starfield snapshot (2000 bytes)
; Static star pattern
pattern_starfield:
.repeat 125
    .byte $20, $2E, $20, $20, $2A, $20, $20, $20, $02, $20, $2E, $20, $20, $20, $2A, $20
.endrepeat

; Pattern 8: Full charset showcase (2048 bytes)
; Every character 0-255, repeated 8 times
pattern_charset:
.repeat 8
    .repeat 256, i
        .byte i
    .endrepeat
.endrepeat

; Pattern 9: Rounded box frames (2000 bytes)
pattern_rounded:
.repeat 100
    .byte $B6, $A0, $A0, $B7, $B6, $A0, $A0, $B7, $A1, $20, $20, $A1, $A1, $20, $20, $A1
    .byte $B8, $A0, $A0, $B9
.endrepeat

; Pattern 10: Card suits (2000 bytes)
pattern_suits:
.repeat 125
    .byte $03, $20, $04, $20, $05, $20, $06, $20, $01, $20, $03, $20, $04, $20, $05, $20
.endrepeat

; Pattern 11: Musical symbols (2000 bytes)
pattern_music:
.repeat 125
    .byte $1C, $20, $1D, $20, $0A, $20, $20, $20, $1C, $20, $1D, $20, $0A, $20, $20, $20
.endrepeat

; Pattern 12: Arrows and pointers (2000 bytes)
pattern_arrows:
.repeat 125
    .byte $0C, $20, $0D, $20, $0E, $20, $0F, $20, $15, $20, $16, $20, $17, $20, $18, $20
.endrepeat

; ----------------------------------------------------------------
; End of RODATA
; ----------------------------------------------------------------
