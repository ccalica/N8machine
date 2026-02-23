; ---------------------------------------------------------------------------
; devices.inc
; Device locations on memory map
; ---------------------------------------------------------------------------

.include "n8_memory_map.inc"

; Legacy aliases for transitional compatibility
.define TXT_CTRL         N8_FB_BASE
.define TXT_BUFF         N8_FB_BASE+1

.define TTY_OUT_CTRL     N8_TTY_OUT_STATUS
.define TTY_OUT_DATA     N8_TTY_OUT_DATA
.define TTY_IN_CTRL      N8_TTY_IN_STATUS
.define TTY_IN_DATA      N8_TTY_IN_DATA
