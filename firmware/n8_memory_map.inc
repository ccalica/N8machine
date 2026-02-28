; n8_memory_map.inc -- device register addresses for cc65 assembly
; Mirror of src/n8_memory_map.h

; --- System / IRQ (slot 0) ---
.define N8_IRQ_FLAGS      $D800
.define N8_IRQ_BIT_TTY    1
.define N8_IRQ_BIT_KBD    2

; --- TTY (slot 1) ---
.define N8_TTY_OUT_STATUS $D820
.define N8_TTY_OUT_DATA   $D821
.define N8_TTY_IN_STATUS  $D822
.define N8_TTY_IN_DATA    $D823

; --- Video Control (slot 2) ---
.define N8_VID_MODE       $D840
.define N8_VID_WIDTH      $D841
.define N8_VID_HEIGHT     $D842
.define N8_VID_STRIDE     $D843
.define N8_VID_OPER       $D844
.define N8_VID_CURSOR     $D845
.define N8_VID_CURCOL     $D846
.define N8_VID_CURROW     $D847
.define N8_VID_VSYNC      $D848

; VID_MODE values
.define N8_VIDMODE_TEXT_DEFAULT  $00
.define N8_VIDMODE_TEXT_CUSTOM   $01

; VID_OPER values (write-once trigger, does not latch)
.define N8_VIDOP_NOP            $00
.define N8_VIDOP_SCROLL_UP      $01
.define N8_VIDOP_SCROLL_DOWN    $02
.define N8_VIDOP_SCROLL_LEFT    $03
.define N8_VIDOP_SCROLL_RIGHT   $04

; VID_CURSOR bit fields
;   bits 0-1: mode (0=off, 1=steady, 2=flash)
;   bits 2-3: shape (0=underline, 1=block)
;   bits 4-7: flash rate (frames per toggle; 0=cursor not displayed)
.define N8_VID_CURSOR_MODE_MASK   $03
.define N8_VID_CURSOR_SHAPE_MASK  $0C
.define N8_VID_CURSOR_RATE_MASK   $F0
.define N8_VID_CURSOR_OFF         $00
.define N8_VID_CURSOR_STEADY      $01
.define N8_VID_CURSOR_FLASH       $02
.define N8_VID_CURSOR_UNDERLINE   $00
.define N8_VID_CURSOR_BLOCK       $04

; Default text mode dimensions
.define N8_VID_DEFAULT_WIDTH    80
.define N8_VID_DEFAULT_HEIGHT   25

; --- Keyboard (slot 3) ---
.define N8_KBD_DATA       $D860
.define N8_KBD_STATUS     $D861
.define N8_KBD_ACK        $D861
.define N8_KBD_CTRL       $D862

; KBD_STATUS bits
.define N8_KBD_STAT_AVAIL     $01
.define N8_KBD_STAT_OVERFLOW  $02
.define N8_KBD_STAT_SHIFT     $04
.define N8_KBD_STAT_CTRL      $08
.define N8_KBD_STAT_ALT       $10
.define N8_KBD_STAT_CAPS      $20

; KBD_CTRL bits
.define N8_KBD_CTRL_IRQ_EN    $01

; --- Frame Buffer ---
.define N8_FB_BASE        $C000
.define N8_FB_SIZE        $1000
.define N8_FB_END         $CFFF

; --- Dev Bank ---
.define N8_DEVBANK_BASE   $D000
.define N8_DEVBANK_SIZE   $0800
.define N8_DEVBANK_END    $D7FF

; --- ROM ---
.define N8_ROM_BASE       $E000
.define N8_ROM_SIZE       $2000
.define N8_ROM_END        $FFFF

; --- Vectors ---
.define N8_VEC_NMI        $FFFA
.define N8_VEC_RESET      $FFFC
.define N8_VEC_IRQ        $FFFE
