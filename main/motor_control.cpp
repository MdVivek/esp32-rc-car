#include "motor_control.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "motor_ctrl";

/* =====================================================
 *                  TB6612 PIN MAPPING
 * ===================================================== */

// Shared standby
#define STBY GPIO_NUM_19

// LEFT FRONT
#define LF_IN1 GPIO_NUM_23
#define LF_IN2 GPIO_NUM_22
#define LF_PWM GPIO_NUM_21

// LEFT BACK
#define LB_IN1 GPIO_NUM_18
#define LB_IN2 GPIO_NUM_5
#define LB_PWM GPIO_NUM_17

// RIGHT FRONT
#define RF_IN1 GPIO_NUM_27
#define RF_IN2 GPIO_NUM_26
#define RF_PWM GPIO_NUM_25

// RIGHT BACK
#define RB_IN1 GPIO_NUM_33
#define RB_IN2 GPIO_NUM_32
#define RB_PWM GPIO_NUM_14

/* =====================================================
 *                  PWM CONFIG
 * ===================================================== */

#define PWM_FREQ_HZ 10000
#define PWM_RES LEDC_TIMER_8_BIT
#define PWM_MAX_DUTY 255

#define PWM_TIMER LEDC_TIMER_0
#define PWM_MODE LEDC_HIGH_SPEED_MODE

static int speed_cmd = 0;   // -10 .. +10
static int steer_cmd = 0;   // -10 .. +10

/* =====================================================
 *              SPEED → PWM CONVERSION
 * ===================================================== */

static inline int pwm_from_speed(int speed)
{
    int s = abs(speed);
    if (s > 10) s = 10;
    return (s * PWM_MAX_DUTY) / 10;
}

/* =====================================================
 *                  MOTOR INIT
 * ===================================================== */

void motor_init(void)
{
    gpio_config_t io{};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask =
        (1ULL << LF_IN1) | (1ULL << LF_IN2) |
        (1ULL << LB_IN1) | (1ULL << LB_IN2) |
        (1ULL << RF_IN1) | (1ULL << RF_IN2) |
        (1ULL << RB_IN1) | (1ULL << RB_IN2) |
        (1ULL << STBY);

    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(STBY, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    ledc_timer_config_t timer{};
    timer.speed_mode = PWM_MODE;
    timer.timer_num = PWM_TIMER;
    timer.freq_hz = PWM_FREQ_HZ;
    timer.duty_resolution = PWM_RES;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const int pwm_gpios[4] = {LF_PWM, LB_PWM, RF_PWM, RB_PWM};
    const ledc_channel_t chs[4] = {
        LEDC_CHANNEL_0, LEDC_CHANNEL_1,
        LEDC_CHANNEL_2, LEDC_CHANNEL_3
    };

    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t ch{};
        ch.channel = chs[i];
        ch.gpio_num = pwm_gpios[i];
        ch.speed_mode = PWM_MODE;
        ch.timer_sel = PWM_TIMER;
        ch.duty = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&ch));
    }

    ESP_LOGI(TAG, "Motor driver initialized");
}

/* =====================================================
 *              LOW LEVEL HELPERS
 * ===================================================== */

#define DIR_PAIR(in1, in2, fwd) \
    gpio_set_level(in1, fwd);   \
    gpio_set_level(in2, !fwd)

#define SET_DUTY(ch, val)             \
    ledc_set_duty(PWM_MODE, ch, val); \
    ledc_update_duty(PWM_MODE, ch)

static inline void set_all_dirs(bool fwd)
{
    DIR_PAIR(LF_IN1, LF_IN2, fwd);
    DIR_PAIR(LB_IN1, LB_IN2, fwd);
    DIR_PAIR(RF_IN1, RF_IN2, fwd);
    DIR_PAIR(RB_IN1, RB_IN2, fwd);
}

static inline void set_all_pwm(int lf, int lb, int rf, int rb)
{
    SET_DUTY(LEDC_CHANNEL_0, lf);
    SET_DUTY(LEDC_CHANNEL_1, lb);
    SET_DUTY(LEDC_CHANNEL_2, rf);
    SET_DUTY(LEDC_CHANNEL_3, rb);
}

/* =====================================================
 *              CORE DRIVE MODEL
 * ===================================================== */

static void apply_drive(void)
{
    bool forward = speed_cmd >= 0;
    int base = pwm_from_speed(speed_cmd);

    int steer = steer_cmd;
    if (steer > 10) steer = 10;
    if (steer < -10) steer = -10;

    int diff = (abs(steer) * base) / 10;

    int lf = base, lb = base, rf = base, rb = base;

    if (steer < 0) {        // left
        lf -= diff;
        lb -= diff;
    } else if (steer > 0) { // right
        rf -= diff;
        rb -= diff;
    }

    set_all_dirs(forward);
    set_all_pwm(lf, lb, rf, rb);

    ESP_LOGI(TAG, "APPLY speed=%d steer=%d | L=%d R=%d",
             speed_cmd, steer_cmd, lf, rf);
}

/* =====================================================
 *              MOTOR API
 * ===================================================== */

void set_speed(int speed)
{
    speed_cmd = speed;
    apply_drive();
}

void stop_motors(void)
{
    speed_cmd = 0;
    steer_cmd = 0;
    set_all_pwm(0, 0, 0, 0);
}

void forward(int speed)
{
    speed_cmd = speed;
    apply_drive();
}

void turn_left(int speed)
{
    speed_cmd = speed;
    steer_cmd = -10;
    apply_drive();
}

void turn_right(int speed)
{
    speed_cmd = speed;
    steer_cmd = 10;
    apply_drive();
}

void set_steer(int steer)
{
    steer_cmd = steer;
    apply_drive();
}
