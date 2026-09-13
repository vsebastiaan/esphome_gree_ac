#pragma once

#include <cstdint>

#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {
namespace gree_replay {

class GreeReplay : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_interval_ms(uint32_t interval_ms) { this->interval_ms_ = interval_ms; }

 protected:
  static constexpr uint8_t RX_BUFFER_SIZE = 64;
  static constexpr uint32_t INITIAL_TX_DELAY_MS = 500;
  static constexpr uint32_t SUMMARY_INTERVAL_MS = 5000;

  void send_stock_request_();
  void consume_rx_byte_(uint8_t value);
  void finish_rx_frame_();
  void reset_rx_parser_();
  uint8_t checksum_(const uint8_t *data, uint8_t size) const;
  void log_frame_(const char *label, const uint8_t *data, uint8_t size) const;
  void report_summary_();

  uint32_t interval_ms_{300};
  uint32_t next_tx_ms_{0};
  uint32_t next_summary_ms_{0};

  uint8_t rx_buffer_[RX_BUFFER_SIZE]{};
  uint8_t rx_pos_{0};
  uint8_t rx_expected_{0};

  uint32_t tx_count_{0};
  uint32_t rx_byte_count_{0};
  uint32_t rx_frame_count_{0};
  uint32_t rx_report_31_count_{0};
  uint32_t rx_bad_checksum_count_{0};
  uint32_t rx_resync_count_{0};
};

}  // namespace gree_replay
}  // namespace esphome
