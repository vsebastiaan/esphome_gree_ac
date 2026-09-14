#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/components/climate/climate.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {
namespace tosot_ac {

class TosotACSwitch : public switch_::Switch, public Component {
 protected:
  void write_state(bool state) override { this->publish_state(state); }
};

class TosotACSelect : public select::Select, public Component {
 protected:
  void control(const std::string &value) override { this->publish_state(value); }
};

class TosotAC : public Component, public uart::UARTDevice, public climate::Climate {
 public:
  void setup() override;
  void loop() override;
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;

  void set_room_temperature_sensor(sensor::Sensor *value) { this->room_temperature_sensor_ = value; }
  void set_horizontal_swing_select(select::Select *value);
  void set_vertical_swing_select(select::Select *value);
  void set_display_select(select::Select *value);
  void set_display_unit_select(select::Select *value);

  void set_turbo_switch(switch_::Switch *value);
  void set_plasma_switch(switch_::Switch *value);
  void set_beeper_switch(switch_::Switch *value);
  void set_sleep_switch(switch_::Switch *value);
  void set_xfan_switch(switch_::Switch *value);
  void set_save_switch(switch_::Switch *value);

 protected:
  static constexpr uint32_t POLL_INTERVAL_MS = 300;
  static constexpr uint32_t SUMMARY_INTERVAL_MS = 5000;
  static constexpr uint32_t STATE_HEARTBEAT_MS = 30000;
  static constexpr uint8_t RX_BUFFER_SIZE = 64;

  void consume_rx_byte_(uint8_t value);
  void finish_rx_frame_();
  void reset_rx_parser_();
  uint8_t checksum_array_(const uint8_t *data, uint8_t size) const;
  void report_summary_();
  void log_report_delta_(const std::vector<uint8_t> &frame) const;

  void queue_control_(const char *reason);
  void send_next_();
  void send_passive_poll_();
  void send_control_(bool af);
  std::vector<uint8_t> build_control_frame_(bool af) const;
  uint8_t checksum_vector_(const std::vector<uint8_t> &frame) const;
  void decode_report_(const std::vector<uint8_t> &frame);
  void publish_advanced_state_(bool force);
  void log_frame_(const char *prefix, const std::vector<uint8_t> &frame) const;

  uint8_t rx_buffer_[RX_BUFFER_SIZE]{};
  uint8_t rx_pos_{0};
  uint8_t rx_expected_{0};
  std::vector<uint8_t> last_report_{};

  sensor::Sensor *room_temperature_sensor_{nullptr};
  select::Select *horizontal_swing_select_{nullptr};
  select::Select *vertical_swing_select_{nullptr};
  select::Select *display_select_{nullptr};
  select::Select *display_unit_select_{nullptr};

  switch_::Switch *turbo_switch_{nullptr};
  switch_::Switch *plasma_switch_{nullptr};
  switch_::Switch *beeper_switch_{nullptr};
  switch_::Switch *sleep_switch_{nullptr};
  switch_::Switch *xfan_switch_{nullptr};
  switch_::Switch *save_switch_{nullptr};

  uint32_t last_tx_ms_{0};
  uint32_t next_summary_ms_{0};
  uint32_t last_publish_ms_{0};
  uint32_t last_room_temperature_publish_ms_{0};
  uint32_t tx_count_{0};
  uint32_t rx_byte_count_{0};
  uint32_t rx_frame_count_{0};
  uint32_t rx_report_31_count_{0};
  uint32_t rx_other_valid_count_{0};
  uint32_t bad_checksum_count_{0};
  uint32_t rx_resync_count_{0};
  uint32_t publish_count_{0};

  bool ready_{false};
  bool state_published_{false};
  bool advanced_state_initialized_{false};
  bool force_publish_{false};

  uint8_t last_mode_code_{1};
  uint8_t last_fan_code_{0};

  uint8_t actual_horizontal_swing_code_{4};
  uint8_t actual_vertical_swing_code_{9};
  uint8_t actual_display_mode_{0};
  bool actual_display_power_{true};
  bool actual_display_f_{false};
  bool actual_turbo_{false};
  bool actual_plasma_{false};
  bool actual_beeper_{true};
  bool actual_sleep_{false};
  bool actual_xfan_{false};
  bool actual_save_{false};

  uint8_t desired_mode_code_{1};
  uint8_t desired_fan_code_{0};
  uint8_t desired_horizontal_swing_code_{4};
  uint8_t desired_vertical_swing_code_{9};
  uint8_t desired_display_mode_{0};
  float desired_target_temperature_{22.0f};
  bool desired_power_{false};
  bool desired_display_power_{true};
  bool desired_display_f_{false};
  bool desired_turbo_{false};
  bool desired_plasma_{false};
  bool desired_beeper_{true};
  bool desired_sleep_{false};
  bool desired_xfan_{false};
  bool desired_save_{false};

  uint8_t control_stage_{0};
};

class TosotGWH18AC : public TosotAC {
 public:
  climate::ClimateTraits traits() override;
  void loop() override;

  void set_fan_speed_select(select::Select *value);
  void set_vertical_swing_select(select::Select *value);
  void set_display_select(select::Select *value);
  void set_plasma_select(select::Select *value);
  void set_beeper_select(select::Select *value);
  void set_sleep_select(select::Select *value);
  void set_xfan_select(select::Select *value);
  void set_save_select(select::Select *value);

 protected:
  select::Select *gwh18_fan_speed_select_{nullptr};
  select::Select *gwh18_vertical_swing_select_{nullptr};
  select::Select *gwh18_display_select_{nullptr};
  select::Select *gwh18_plasma_select_{nullptr};
  select::Select *gwh18_beeper_select_{nullptr};
  select::Select *gwh18_sleep_select_{nullptr};
  select::Select *gwh18_xfan_select_{nullptr};
  select::Select *gwh18_save_select_{nullptr};

  uint32_t gwh18_last_ui_publish_ms_{0};
  uint8_t gwh18_last_fan_ui_code_{0xFF};
  uint8_t gwh18_last_vertical_ui_code_{0xFF};
  int8_t gwh18_last_display_ui_index_{-1};
  int8_t gwh18_last_plasma_ui_index_{-1};
  int8_t gwh18_last_beeper_ui_index_{-1};
  int8_t gwh18_last_sleep_ui_index_{-1};
  int8_t gwh18_last_xfan_ui_index_{-1};
  int8_t gwh18_last_save_ui_index_{-1};
};

}  // namespace tosot_ac
}  // namespace esphome
