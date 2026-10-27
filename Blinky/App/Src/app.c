#include "app.h"

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

extern TIM_HandleTypeDef htim2;

#define WS_TIM_ARR 99u
#define WS_TIM_TICKS (WS_TIM_ARR + 1u)

#define WS_0_HIGH (WS_TIM_TICKS * 35u / 100u)
#define WS_1_HIGH (WS_TIM_TICKS * 70u / 100u)
#define WS_RESET 50u

#define WS_LED_COUNT 1u
#define WS_BIT_COUNT 24u
#define WS_BUF_LEN (WS_LED_COUNT * WS_BIT_COUNT + WS_RESET)

#define LED_MAX 64u
#define PERIOD_MS 3000u
#define UPDATE_MS 10u

#define BLUE_BLINK_COUNT 3u
#define BLUE_BLINK_MS 150u
#define BUTTON_DEBOUNCE_MS 30u

static uint32_t dma_buf[WS_BUF_LEN];
static volatile bool dma_sent = true;

static uint32_t next_update_ms = 0u;

static bool blue_blinking = false;
static uint32_t blue_blinking_start_ms = 0u;

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM2) {
    HAL_TIM_PWM_Stop_DMA(&htim2, TIM_CHANNEL_1);
    dma_sent = true;
  }
}

static void ws2812_send(uint8_t r, uint8_t g, uint8_t b) {
  if (!dma_sent) {
    return;
  }

  const uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;
  uint32_t idx = 0u;

  for (int8_t bit = WS_BIT_COUNT - 1; bit >= 0; bit--) {
    dma_buf[idx++] = (grb & (1u << bit)) ? WS_1_HIGH : WS_0_HIGH;
  }

  while (idx < WS_BUF_LEN) {
    dma_buf[idx++] = 0u;
  }

  dma_sent = false;
  if (HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_1, dma_buf, WS_BUF_LEN) !=
      HAL_OK) {
    dma_sent = true;
  }
}

static uint8_t calc_red(uint32_t phase_ms) {
  return (phase_ms < (PERIOD_MS / 2u)) ? (uint8_t)LED_MAX : 0u;
}

static uint8_t calc_green(uint32_t phase_ms) {
  return (uint8_t)((phase_ms * LED_MAX) / PERIOD_MS);
}

static uint8_t calc_blue(uint32_t now_ms) {
  if (!blue_blinking) {
    return 0u;
  }

  const uint32_t step = (now_ms - blue_blinking_start_ms) / BLUE_BLINK_MS;

  if (step >= (2u * BLUE_BLINK_COUNT)) {
    blue_blinking = false;
    return 0u;
  }

  return ((step % 2u) == 0u) ? (uint8_t)LED_MAX : 0u;
}

static void button_task(uint32_t now_ms) {
  static uint8_t last_state = 1u;
  static uint32_t last_change_ms = 0u;

  const uint8_t state =
      (HAL_GPIO_ReadPin(BUTTON_GPIO_Port, BUTTON_Pin) == GPIO_PIN_SET) ? 1u
                                                                       : 0u;

  if ((state != last_state) &&
      ((now_ms - last_change_ms) > BUTTON_DEBOUNCE_MS)) {
    last_change_ms = now_ms;
    last_state = state;

    if ((state == 0u) && !blue_blinking) {
      blue_blinking = true;
      blue_blinking_start_ms = now_ms;
    }
  }
}

void app_init(void) {
  ws2812_send(0u, 0u, 0u);
  next_update_ms = HAL_GetTick();
}

void app_loop(void) {
  const uint32_t now = HAL_GetTick();

  button_task(now);

  if ((int32_t)(now - next_update_ms) >= 0) {
    next_update_ms += UPDATE_MS;

    const uint32_t phase = now % PERIOD_MS;

    ws2812_send(calc_red(phase), calc_green(phase), calc_blue(now));
  }
}