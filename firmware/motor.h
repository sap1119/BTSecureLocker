/*=============================================================================
 * File        : motor.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for the DC motor driver (locker latch actuator),
 *               driven through an L293D H-bridge.
 *===========================================================================*/
#ifndef MOTOR_H
#define MOTOR_H

void motor_init(void);     /* Configure motor control GPIO pins as outputs */
void motor_forward(void);  /* Drive the motor forward (open the locker)    */
void motor_reverse(void);  /* Drive the motor in reverse (close the locker) */
void motor_stop(void);     /* Stop/coast the motor                          */

#endif
