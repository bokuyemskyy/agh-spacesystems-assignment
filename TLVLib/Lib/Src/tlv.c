#include "tlv.h"

#include <string.h>

tlv_status_t tlv_init(tlv_t *tlv, const tlv_io_t *io, tlv_frame_cb_t on_frame) {
  if (tlv == NULL || io == NULL || on_frame == NULL) {
    return TLV_ERR_ARG;
  }

  // Zeroing also resets the state to TLV_STATE_TYPE
  memset(tlv, 0, sizeof(*tlv));
  tlv->io = *io; // The struct is copied and does not depend on the initial one
  tlv->on_frame = on_frame;
  return TLV_OK;
}

// Resets the partially received frame and waits for the next one
void tlv_reset(tlv_t *tlv) {
  if (tlv == NULL) {
    return;
  }
  tlv->state = TLV_STATE_TYPE;
  tlv->received = 0;
}

// Consumes the received bytes and parses them
void tlv_process(tlv_t *tlv, const uint8_t *data, size_t len) {
  if (tlv == NULL || data == NULL) {
    return;
  }

  // Discard stalled frames
#if TLV_RX_TIMEOUT_MS > 0
  if (tlv->io.get_tick_ms != NULL) {
    uint32_t now = tlv->io.get_tick_ms();

    if (tlv->state != TLV_STATE_TYPE &&
        (now - tlv->last_rx_ms) > TLV_RX_TIMEOUT_MS) {
      tlv->dropped++;
      tlv_reset(tlv);
    }
    tlv->last_rx_ms = now;
  }
#endif

  // Consume one byte at a time to process frames that are split across calls or
  // multiple frames in one call
  for (size_t i = 0; i < len; i++) {
    uint8_t byte = data[i];

    switch (tlv->state) {
    case TLV_STATE_TYPE:
      tlv->frame.type = byte;
      // Advance the state machine
      tlv->state = TLV_STATE_LENGTH;
      break;

    case TLV_STATE_LENGTH:
      // Drop the frames longer than the buffer
      if (byte > TLV_MAX_PAYLOAD_SIZE) {
        tlv->dropped++;
        tlv_reset(tlv);
        break;
      }

      tlv->frame.length = byte;
      tlv->received = 0;

      // The frame with no payload is already complete
      if (byte == 0) {
        tlv->on_frame(&tlv->frame, tlv->io.ctx);
        tlv_reset(tlv);
      } else {
        tlv->state = TLV_STATE_PAYLOAD;
      }
      break;

    case TLV_STATE_PAYLOAD:
      tlv->frame.payload[tlv->received++] = byte;
      if (tlv->received == tlv->frame.length) {
        tlv->on_frame(&tlv->frame, tlv->io.ctx);
        tlv_reset(tlv);
      }
      break;

    default:
      tlv_reset(tlv);
      break;
    }
  }
}

// Builds the frame and sends it using io.write()
tlv_status_t tlv_send(tlv_t *tlv, uint8_t type, const uint8_t *payload,
                      uint8_t length) {
  uint8_t buf[TLV_MAX_FRAME_SIZE];

  if (tlv == NULL || tlv->io.write == NULL) {
    return TLV_ERR_ARG;
  }
  if (length > TLV_MAX_PAYLOAD_SIZE) {
    return TLV_ERR_TOO_LONG;
  }
  if (length > 0 && payload == NULL) {
    return TLV_ERR_ARG;
  }

  buf[0] = type;
  buf[1] = length;
  if (length > 0) {
    memcpy(&buf[TLV_HEADER_SIZE], payload, length);
  }

  if (!tlv->io.write(buf, TLV_HEADER_SIZE + length, tlv->io.ctx)) {
    return TLV_ERR_IO;
  }
  return TLV_OK;
}