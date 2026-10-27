#ifndef TLV_H
#define TLV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TLV_MAX_PAYLOAD_SIZE
#define TLV_MAX_PAYLOAD_SIZE 8
#endif

#ifndef TLV_RX_TIMEOUT_MS
#define TLV_RX_TIMEOUT_MS 100
#endif

#define TLV_HEADER_SIZE 2
#define TLV_MAX_FRAME_SIZE (TLV_HEADER_SIZE + TLV_MAX_PAYLOAD_SIZE)

typedef enum {
  TLV_OK = 0,
  TLV_ERR_ARG,
  TLV_ERR_TOO_LONG,
  TLV_ERR_IO
} tlv_status_t;

typedef struct {
  uint8_t type;
  uint8_t length;
  uint8_t payload[TLV_MAX_PAYLOAD_SIZE];
} tlv_frame_t;

typedef struct {
  bool (*write)(const uint8_t *data, size_t len, void *ctx);
  uint32_t (*get_tick_ms)(void);
  void *ctx;
} tlv_io_t;

typedef void (*tlv_frame_cb_t)(const tlv_frame_t *frame, void *ctx);

typedef enum {
  TLV_STATE_TYPE,
  TLV_STATE_LENGTH,
  TLV_STATE_PAYLOAD
} tlv_state_t;

typedef struct {
  tlv_io_t io;
  tlv_frame_cb_t on_frame;
  tlv_state_t state;
  tlv_frame_t frame;
  uint8_t received;
  uint32_t last_rx_ms;
  uint32_t dropped;
} tlv_t;

tlv_status_t tlv_init(tlv_t *tlv, const tlv_io_t *io, tlv_frame_cb_t on_frame);

void tlv_reset(tlv_t *tlv);

void tlv_process(tlv_t *tlv, const uint8_t *data, size_t len);

tlv_status_t tlv_send(tlv_t *tlv, uint8_t type, const uint8_t *payload,
                      uint8_t length);

#ifdef __cplusplus
}
#endif

#endif