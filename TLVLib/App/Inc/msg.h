#ifndef MSG_H
#define MSG_H

#include <stdint.h>

#define TLV_MAX_PAYLOAD_SIZE 8

typedef struct __attribute__((__packed__)) {
  uint8_t message_id;
  uint8_t payload_length;
  uint8_t payload[TLV_MAX_PAYLOAD_SIZE];
} packet_t;

typedef enum {
  MSG_ACK = 0x01,
  MSG_PING = 0x02,
  MSG_PONG = 0x03,
  MSG_MATH = 0x10,
  MSG_MATH_RESP = 0x11
} message_id_t;

typedef enum { MSG_STATUS_OK = 0x00, MSG_STATUS_ERROR = 0x01 } ack_status_t;

typedef struct __attribute__((__packed__)) {
  uint8_t ack_msg_id; // message id of recieved message
  uint8_t ack_status;
} msg_ack_t;

typedef struct __attribute__((__packed__)) {
  uint16_t
      pong_delay_ms; // delay in ms after which pong message should be sent back
} msg_ping_t;

typedef struct __attribute__((__packed__)) {
  uint8_t reserved;
} msg_pong_t;

typedef struct __attribute__((__packed__)) {
  union {
    uint8_t operation_raw;
    struct {
      uint8_t add : 1; // a + b
      uint8_t sub : 1; // a - b
      uint8_t mul : 1; // a * b
      uint8_t div : 1; // a / b
      uint8_t reserved : 4;
    } operation_bits; // only one flag should be set
  };
  int16_t operand_a;
  int16_t operand_b;
} msg_math_t;

typedef struct __attribute__((__packed__)) {
  union {
    uint8_t flags;
    struct {
      uint8_t overflow : 1;
      uint8_t divide_by_zero : 1;
      uint8_t reserved : 6;
    } flags_bits;
  };
  int16_t result;
} msg_math_resp_t;

#endif