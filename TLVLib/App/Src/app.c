#include "app.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "main.h"
#include "msg.h"
#include "tlv.h"

#define RX_RING_SIZE 128u
#define TX_TIMEOUT_MS 20u

extern UART_HandleTypeDef huart4;

static tlv_t tlv;

// RX ring buffer
static uint8_t rx_byte;
static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

// Pong
static bool pong_pending;
static uint32_t pong_deadline_ms;

// Concrete UART and tick method implementations
static bool uart_write(const uint8_t *data, size_t len, void *ctx) {
  UART_HandleTypeDef *huart = (UART_HandleTypeDef *)ctx;
  return HAL_UART_Transmit(huart, (uint8_t *)data, len, TX_TIMEOUT_MS) ==
         HAL_OK;
}

static uint32_t uart_tick_ms(void) { return HAL_GetTick(); }

// The structure with the pointers to implementations
static const tlv_io_t uart_io = {
    .write = uart_write,
    .get_tick_ms = uart_tick_ms,
    // User pointer handled back to the callbacks
    .ctx = &huart4,
};

static void send_ack(uint8_t acked_id, ack_status_t status) {
  const msg_ack_t ack = {
      .ack_msg_id = acked_id,
      .ack_status = (uint8_t)status,
  };
  tlv_send(&tlv, MSG_ACK, (const uint8_t *)&ack, sizeof(ack));
}

// UART interrupt for a single received byte
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == UART4) {
    // Next write position. Since the size is a power of two, the & operation
    // wraps the index
    const uint16_t next = (uint16_t)((rx_head + 1u) & (RX_RING_SIZE - 1u));

    // If next reaches the tail then the buffer is full and the byte is dropped
    if (next != rx_tail) {
      rx_ring[rx_head] = rx_byte;
      rx_head = next;
    }
    // Rearm HAL
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1u);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == UART4) {
    // On UART error start listening again
    HAL_UART_Receive_IT(&huart4, &rx_byte, 1u);
  }
}

// Exactly one of the four operation flags must be set
static bool math_operation_valid(const msg_math_t *msg) {
  const uint8_t operations = msg->operation_raw;

  if ((operations & 0xF0u) != 0u || operations == 0u) {
    return false;
  }

  // This clears the lowest bit, so it is equal to 0 only if one bit was set
  return (operations & (uint8_t)(operations - 1u)) == 0u;
}

static void math_handle(const msg_math_t *msg) {
  msg_math_resp_t resp;

  memset(&resp, 0, sizeof(resp));

  // 32 bit so that int16 operands cant overflow
  const int32_t a = msg->operand_a;
  const int32_t b = msg->operand_b;

  int32_t result = 0;

  if (msg->operation_bits.add) {
    result = a + b;
  } else if (msg->operation_bits.sub) {
    result = a - b;
  } else if (msg->operation_bits.mul) {
    result = a * b;
  } else if (msg->operation_bits.div) {
    if (b == 0) {
      resp.flags_bits.divide_by_zero = 1u;
    } else {
      result = a / b;
    }
  }

  if (result > INT16_MAX || result < INT16_MIN) {
    resp.flags_bits.overflow = 1u;
  }

  resp.result = (int16_t)result;

  tlv_send(&tlv, MSG_MATH_RESP, (const uint8_t *)&resp, sizeof(resp));
}

// Called by the TLV lib for every complete frame
static void on_frame(const tlv_frame_t *frame, void *ctx) {
  switch (frame->type) {
  case MSG_PING: {
    // Wrong payload size
    if (frame->length != sizeof(msg_ping_t)) {
      send_ack(MSG_PING, MSG_STATUS_ERROR);
      break;
    }

    msg_ping_t ping;
    memcpy(&ping, frame->payload, sizeof(ping));

    // Ack now, the PONG is sent later from the loop
    send_ack(MSG_PING, MSG_STATUS_OK);
    pong_deadline_ms = HAL_GetTick() + ping.pong_delay_ms;
    pong_pending = true;
    break;
  }

  case MSG_MATH: {
    if (frame->length != sizeof(msg_math_t)) {
      send_ack(MSG_MATH, MSG_STATUS_ERROR);
      break;
    }

    msg_math_t math;
    memcpy(&math, frame->payload, sizeof(math));

    if (!math_operation_valid(&math)) {
      send_ack(MSG_MATH, MSG_STATUS_ERROR);
      break;
    }

    send_ack(MSG_MATH, MSG_STATUS_OK);
    math_handle(&math);
    break;
  }

  // Do nothing on replies
  case MSG_ACK:
  case MSG_PONG:
  case MSG_MATH_RESP:
    break;

  // Unknown message id
  default:
    send_ack(frame->type, MSG_STATUS_ERROR);
    break;
  }
}

void app_init(void) {
  rx_head = 0u;
  rx_tail = 0u;
  pong_pending = false;

  tlv_init(&tlv, &uart_io, on_frame);

  // Start UART receival one byte at a time
  HAL_UART_Receive_IT(&huart4, &rx_byte, 1u);
}

void app_loop(void) {
  uint8_t chunk[16];
  size_t count = 0u;

  // Move the received bytes from the ring buffer to the TLV parser
  while (rx_tail != rx_head && count < sizeof(chunk)) {
    chunk[count++] = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) & (RX_RING_SIZE - 1u));
  }

  if (count > 0u) {
    tlv_process(&tlv, chunk, count);
  }

  // Send the PONG after its delay
  if (pong_pending && (int32_t)(HAL_GetTick() - pong_deadline_ms) >= 0) {
    const msg_pong_t pong = {.reserved = 0u};
    pong_pending = false;
    tlv_send(&tlv, MSG_PONG, (const uint8_t *)&pong, sizeof(pong));
  }
}