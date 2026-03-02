; -------------------------------------------------------
; kentry.inc -- Kernel entry point addresses for monitor
; -------------------------------------------------------

.define K_TTY_PUTC    $FE00
.define K_TTY_GETC    $FE03
.define K_TTY_PEEKC   $FE06

.define K_CON_GETKEY    $FE09
.define K_CON_SETMODE   $FE0C
.define K_CON_GETSTATUS $FE0F
.define K_CON_PUTCHAR   $FE12
.define K_CON_NEWLINE   $FE15
.define K_CON_CLEAR     $FE18
.define K_CON_SCROLL    $FE1B
.define K_CON_MOVCURSOR $FE1E
.define K_CON_SETCURSOR $FE21
