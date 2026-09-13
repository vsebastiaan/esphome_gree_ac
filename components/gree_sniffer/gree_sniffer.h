#pragma once

#include <cstdint>
#include <string>

#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {
namespace gree_sniffer {

class GreeSniffer : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_channel(const std::string &channel) { this->channel_ = channel; }

 protected:
  static constexpr uint16_t CAPTURE_SIZE = 6144;
  static constexpr uint8_t MAX_BURSTS = 96;
  static constexpr uint16_t REPORT_CHUNK = 96;
  static constexpr uint32_t BURST_GAP_MS = 30;
  static constexpr uint32_t REPORT_INTERVAL_MS = 5000;

  struct Burst {
    uint16_t offset{0};
    uint16_t length{0};
    uint32_t start_rel_ms{0};
  };

  void capture_byte_(uint8_t value, uint32_t now);
  void report_();
  void report_next_chunk_();

  std::string channel_{"unknown"};
  uint8_t data_[CAPTURE_SIZE]{};
  Burst bursts_[MAX_BURSTS]{};

  uint16_t stored_{0};
  uint32_t rx_total_{0};
  uint8_t burst_count_{0};
  bool burst_open_{false};
  bool capture_overflow_{false};
  bool burst_overflow_{false};
  bool have_first_rx_{false};
  uint32_t first_rx_ms_{0};
  uint32_t last_byte_ms_{0};

  uint8_t report_burst_{0};
  uint16_t report_offset_{0};
};

}  // namespace gree_sniffer
}  // namespace esphome
