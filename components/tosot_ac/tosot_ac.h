#pragma once

#include <cstdint>
#include <vector>

#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {
namespace tosot_ac {

class TosotAC : public Component, public uart::UARTDevice, public climate::Climate {
 public:
  void setup() override;
  void loop() override;
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;

 protected:
  static constexpr uint32_t POLL_INTERVAL_MS = 300;
  static constexpr size_t FRAME_MAX = 80;

  void read_uart_();
  void consume_byte_(uint8_t value);
  void finish_frame_();
  bool checksum_ok_(const std::vector<uint8_t> &frame) const;
  uint8_t checksum_(const std::vector<uint8_t> &frame) const;

  void send_next_();
  void send_passive_poll_();
  void send_control_(bool af);
  std::vector<uint8_t> build_control_frame_(bool af) const;
  void decode_report_(const std::vector<uint8_t> &frame);
  void log_frame_(const char *prefix, const std::vector<uint8_t> &frame) const;

  std::vector<uint8_t> rx_frame_{};
  size_t rx_expected_{0};
  std::vector<uint8_t> last_report_{};

  uint32_t last_tx_ms_{0};
  uint32_t tx_count_{0};
  uint32_t rx_count_{0};
  uint32_t bad_checksum_count_{0};

  bool ready_{false};
  uint8_t last_mode_code_{1};
  uint8_t last_fan_code_{0};
  uint8_t desired_mode_code_{1};
  uint8_t desired_fan_code_{0};
  float desired_target_temperature_{22.0f};
  bool desired_power_{false};

  // 0 = passive polling, 2 = AF control pending, 1 = clear-control pending.
  uint8_t control_stage_{0};
};

}  // namespace tosot_ac
}  // namespace esphome
