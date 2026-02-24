; n8_memory_map.inc -- device register addresses for cc65 assembly
; Mirror of src/n8_memory_map.h

.define N8_IRQ_FLAGS      $D800
.define N8_IRQ_BIT_TTY    1
.define N8_IRQ_BIT_KBD    2

.define N8_TTY_OUT_STATUS $D820
.define N8_TTY_OUT_DATA   $D821
.define N8_TTY_IN_STATUS  $D822
.define N8_TTY_IN_DATA    $D823

.define N8_VID_MODE       $D840
.define N8_VID_WIDTH      $D841
.define N8_VID_HEIGHT     $D842
.define N8_VID_STRIDE     $D843
.define N8_VID_OPER       $D844
.define N8_VID_CURSOR     $D845
.define N8_VID_CURCOL     $D846
.define N8_VID_CURROW     $D847
.define N8_VID_VSYNC      $D848

.define N8_KBD_DATA       $D860
.define N8_KBD_STATUS     $D861
.define N8_KBD_ACK        $D861
.define N8_KBD_CTRL       $D862

.define N8_FB_BASE        $C000
