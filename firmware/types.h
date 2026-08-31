/*=============================================================================
 * File        : types.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Common, short, portable type aliases used throughout the
 *               project instead of the compiler's built-in keywords. Using
 *               fixed aliases makes the intended bit-width of every variable
 *               explicit and keeps the code consistent across all modules.
 *===========================================================================*/
#ifndef TYPES_H
#define TYPES_H

typedef unsigned char  u8;   /* 8-bit  unsigned : 0 .. 255                */
typedef unsigned short u16;  /* 16-bit unsigned : 0 .. 65535              */
typedef unsigned int   u32;  /* 32-bit unsigned : 0 .. 4294967295         */
typedef signed char    s8;   /* 8-bit  signed   : -128 .. 127             */
typedef signed short   s16;  /* 16-bit signed   : -32768 .. 32767         */
typedef signed int     s32;  /* 32-bit signed   : -2147483648 .. 2147483647 */

#endif
