#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize motor driver (GPIO, PWM timers, channels)
 */
void motor_init(void);

/**
 * @brief Set motor speed (-10 to +10)
 * @param speed Speed command in range [-10, 10]
 */
void set_speed(int speed);

/**
 * @brief Stop all motors
 */
void stop_motors(void);

/**
 * @brief Move forward at given speed
 * @param speed Speed command in range [0, 10]
 */
void forward(int speed);

/**
 * @brief Turn left at given speed
 * @param speed Speed command in range [0, 10]
 */
void turn_left(int speed);

/**
 * @brief Turn right at given speed
 * @param speed Speed command in range [0, 10]
 */
void turn_right(int speed);

/**
 * @brief Set steering command (-10 to +10)
 * @param steer Steering command in range [-10, 10]
 */
void set_steer(int steer);

#ifdef __cplusplus
}
#endif
