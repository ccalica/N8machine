Kernel Routines for Console (Keyboard and Text Video)

Entry points in KENTRY jump table ($FE00+):

| Address | Routine        | Params / Returns |
|---------|----------------|------------------|
| $FE09   | CON_GETKEY     | Non-blocking. Returns A=keycode ($00 if none), X=modifier bits ($3C mask) |
| $FE0C   | CON_SETMODE    | A=VID_MODE, X=VID_CTRL |
| $FE0F   | CON_GETSTATUS  | Returns A=VID_STATUS, X=cursor col, Y=cursor row |
| $FE12   | CON_PUTCHAR    | A=character code. Writes to VID_DATA at cursor. X=attribute (ignored, TBD) |
| $FE15   | CON_NEWLINE    | Col=0, advance row. Scrolls up if at bottom (reads VID_HEIGHT register) |
| $FE18   | CON_CLEAR      | Clears screen (VIDOP_CLEAR). Cursor reset to (0,0) |
| $FE1B   | CON_SCROLL     | X=signed horizontal (+=right), Y=signed vertical (+=down) |
| $FE1E   | CON_MOVCURSOR  | X=signed horizontal (+=right), Y=signed vertical (+=down) |
| $FE21   | CON_SETCURSOR  | X=col, Y=row |

Constants defined in `firmware/kentry.inc` (K_CON_GETKEY, K_CON_SETMODE, etc.)

Implementation: `firmware/console.s`
