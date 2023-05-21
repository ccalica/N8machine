/*
 * Location of devices on memory map
 */

#ifndef __DEVICES__
#define __DEVICES__

#define   TXT_CTRL  $C000    //  Text Display Control Register
#define   TXT_BUFF  $C001    //  Text Display Buffer Window (255 bytes)

#define   TTY_OUT_CTRL $C100    //  TTY Out Control Register
#define   TTY_OUT_DATA $C101    //  TTY Out Data Register
#define   TTY_IN_CTRL  $C102    //  TTY In Control Register
#define   TTY_IN_DATA  $C103    //  TTY In Data Register

#endif
