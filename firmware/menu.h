/*=============================================================================
 * File        : menu.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for the admin menu, entered via an external
 *               push-button interrupt (EINT2). Merged in from the
 *               "EnviroTime" project: adds a unified CLK Setting
 *               (Time/Date/Day) sub-menu and an Alarm sub-menu on top of
 *               the existing Password-change function, all re-written
 *               to use SecureLocker's own lcd/keypad/buzzer/rtc/eeprom
 *               drivers (no other SecureLocker source file was changed).
 *===========================================================================*/
#ifndef MENU_H
#define MENU_H

#include "types.h"

void admin_int_init(void);   /* Configure P0.7 as EINT2 and enable the admin-button interrupt */
void admin_menu(void);       /* Run the interactive admin menu (blocking) until the user exits */
void check_alarm(void);      /* Poll the RTC against the stored alarm time; ring the buzzer on match */

/* Set to 1 by the EINT2 ISR when the admin button is pressed; the main
 * loop checks this flag and calls admin_menu() when it is set. */
extern volatile unsigned char admin_flag;

/*-------------- Alarm state, defined in menu.c ------------------------*/
extern s32 alarm_hour;
extern s32 alarm_min;
extern s32 alarm_sec;
extern u8  alarm_enabled;
extern u8  alarm_triggered;

#endif
