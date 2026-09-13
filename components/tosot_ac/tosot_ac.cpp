#include "tosot_ac.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/log.h"

namespace esphome {
namespace tosot_ac {

static const char *const TAG = "tosot_ac";
static const char *const VERSION = "tosot-gwh18-v3";

// Exact passive 2F/01 request captured from the original CS532AE on BLACK.
static const uint8_t STOCK_POLL[50] = {
    0x7E, 0x7E, 0x2F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x60,
    0x02, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB4};

void TosotAC::setup() {
  const uint32_t now = millis();
  this->last_tx_ms_ = now - POLL_INTERVAL_MS;
  this->next_summary_ms_ = now + SUMMARY_INTERVAL_MS;
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = 22.0f;
  ESP_LOGI(TAG, "[%s] Starting exact-stock Tosot climate driver", VERSION);
  ESP_LOGI(TAG, "[%s] RX parser is the proven gree_replay parser; state publishing is change-driven", VERSION);
}

climate::ClimateTraits TosotAC::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(30.0f);
  traits.set_visual_temperature_step(1.0f);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO, climate::CLIMATE_MODE_COOL,
                              climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_DRY});
  return traits;
}

void TosotAC::loop() {
  // Keep this receive loop byte-for-byte equivalent in behaviour to gree_replay.
  while (this->available() > 0) {
    uint8_t value = 0;
    if (!this->read_byte(&value))
      break;
    this->rx_byte_count_++;
    this->consume_rx_byte_(value);
  }

  const uint32_t now = millis();
  if (now - this->last_tx_ms_ >= POLL_INTERVAL_MS) {
    this->send_next_();
    this->last_tx_ms_ = now;
  }

  if (static_cast<int32_t>(now - this->next_summary_ms_) >= 0) {
    this->report_summary_();
    this->next_summary_ms_ = now + SUMMARY_INTERVAL_MS;
  }
}

void TosotAC::control(const climate::ClimateCall &call) {
  if (!this->ready_) {
    ESP_LOGW(TAG, "Ignoring control request until first valid 2F/31 report is received");
    return;
  }

  if (call.get_mode().has_value()) {
    const auto requested = *call.get_mode();
    this->mode = requested;
    this->desired_power_ = requested != climate::CLIMATE_MODE_OFF;

    switch (requested) {
      case climate::CLIMATE_MODE_AUTO: this->desired_mode_code_ = 0; break;
      case climate::CLIMATE_MODE_COOL: this->desired_mode_code_ = 1; break;
      case climate::CLIMATE_MODE_DRY: this->desired_mode_code_ = 2; break;
      case climate::CLIMATE_MODE_FAN_ONLY: this->desired_mode_code_ = 3; break;
      case climate::CLIMATE_MODE_HEAT: this->desired_mode_code_ = 4; break;
      case climate::CLIMATE_MODE_OFF:
      default: this->desired_mode_code_ = this->last_mode_code_; break;
    }
  }

  if (call.get_target_temperature().has_value())
    this->desired_target_temperature_ = std::max(16.0f, std::min(30.0f, *call.get_target_temperature()));

  // The next valid report must be published even if the returned values happen to
  // equal our current in-memory values. This makes command acknowledgement visible
  // to API clients without publishing every 300 ms poll response.
  this->force_publish_ = true;
  this->control_stage_ = 2;
  ESP_LOGI(TAG, "[%s] Control queued: power=%s mode=%u target=%.0f", VERSION,
           this->desired_power_ ? "ON" : "OFF", this->desired_mode_code_, this->desired_target_temperature_);
}

void TosotAC::consume_rx_byte_(uint8_t value) {
  if (this->rx_pos_ == 0) {
    if (value == 0x7E) {
      this->rx_buffer_[0] = value;
      this->rx_pos_ = 1;
    }
    return;
  }

  if (this->rx_pos_ == 1) {
    if (value == 0x7E) {
      this->rx_buffer_[1] = value;
      this->rx_pos_ = 2;
    } else {
      this->rx_resync_count_++;
      this->reset_rx_parser_();
    }
    return;
  }

  if (this->rx_pos_ == 2) {
    this->rx_buffer_[2] = value;
    const uint16_t expected = static_cast<uint16_t>(value) + 3U;
    if (expected < 5U || expected > RX_BUFFER_SIZE) {
      this->rx_resync_count_++;
      this->reset_rx_parser_();
      return;
    }
    this->rx_expected_ = static_cast<uint8_t>(expected);
    this->rx_pos_ = 3;
    return;
  }

  if (this->rx_pos_ >= RX_BUFFER_SIZE) {
    this->rx_resync_count_++;
    this->reset_rx_parser_();
    return;
  }

  this->rx_buffer_[this->rx_pos_++] = value;
  if (this->rx_expected_ != 0 && this->rx_pos_ == this->rx_expected_)
    this->finish_rx_frame_();
}

void TosotAC::finish_rx_frame_() {
  const uint8_t size = this->rx_expected_;
  this->rx_frame_count_++;

  const uint8_t expected_checksum = this->checksum_array_(this->rx_buffer_, size);
  const uint8_t actual_checksum = this->rx_buffer_[size - 1];
  const bool checksum_ok = expected_checksum == actual_checksum;
  if (!checksum_ok)
    this->bad_checksum_count_++;

  const uint8_t type = size > 3 ? this->rx_buffer_[3] : 0xFF;
  const bool report31 = checksum_ok && this->rx_buffer_[2] == 0x2F && type == 0x31 && size == 50;

  if (report31) {
    this->rx_report_31_count_++;
    this->last_report_.assign(this->rx_buffer_, this->rx_buffer_ + size);
    this->decode_report_(this->last_report_);
  }

  if (this->rx_frame_count_ <= 10 || (this->rx_frame_count_ % 20) == 0 || !report31) {
    ESP_LOGI(TAG, "[%s] RX frame=%u len=0x%02X type=0x%02X checksum=%s reports31=%u", VERSION,
             static_cast<unsigned>(this->rx_frame_count_), this->rx_buffer_[2], type,
             checksum_ok ? "OK" : "BAD", static_cast<unsigned>(this->rx_report_31_count_));
  }

  this->reset_rx_parser_();
}

void TosotAC::reset_rx_parser_() {
  this->rx_pos_ = 0;
  this->rx_expected_ = 0;
}

uint8_t TosotAC::checksum_array_(const uint8_t *data, uint8_t size) const {
  if (size < 4)
    return 0;
  uint8_t sum = 0;
  for (uint8_t i = 2; i + 1 < size; i++)
    sum += data[i];
  return sum;
}

void TosotAC::send_next_() {
  if (this->control_stage_ == 2) {
    this->send_control_(true);
    this->control_stage_ = 1;
  } else if (this->control_stage_ == 1) {
    this->send_control_(false);
    this->control_stage_ = 0;
  } else {
    this->send_passive_poll_();
  }
}

void TosotAC::send_passive_poll_() {
  std::vector<uint8_t> frame(STOCK_POLL, STOCK_POLL + sizeof(STOCK_POLL));
  this->write_array(frame);
  this->tx_count_++;
  if (this->tx_count_ <= 3 || (this->tx_count_ % 20) == 0)
    this->log_frame_("TX PASSIVE", frame);
}

void TosotAC::send_control_(bool af) {
  auto frame = this->build_control_frame_(af);
  this->write_array(frame);
  this->tx_count_++;
  this->log_frame_(af ? "TX CONTROL AF" : "TX CONTROL CLEAR", frame);
}

std::vector<uint8_t> TosotAC::build_control_frame_(bool af) const {
  std::vector<uint8_t> frame(STOCK_POLL, STOCK_POLL + sizeof(STOCK_POLL));

  if (this->last_report_.size() == 50) {
    frame[10] = this->last_report_[10];
    frame[11] = this->last_report_[11];
    frame[12] = this->last_report_[12];
    frame[13] = this->last_report_[13];
    frame[15] = this->last_report_[15];
    frame[44] = this->last_report_[44];
  }

  frame[7] = af ? 0xAF : 0x00;

  uint8_t status = this->last_report_.size() == 50 ? (this->last_report_[8] & 0x0C) : 0x00;
  if (this->desired_power_)
    status |= 0x80;
  status |= static_cast<uint8_t>((this->desired_mode_code_ & 0x07) << 4);
  status |= static_cast<uint8_t>(this->desired_fan_code_ & 0x03);
  frame[8] = status;

  const int target = static_cast<int>(std::round(this->desired_target_temperature_));
  frame[9] = static_cast<uint8_t>((std::max(16, std::min(30, target)) - 16) << 4);

  frame.back() = this->checksum_vector_(frame);
  return frame;
}

uint8_t TosotAC::checksum_vector_(const std::vector<uint8_t> &frame) const {
  uint8_t sum = 0;
  for (size_t i = 2; i + 1 < frame.size(); i++)
    sum += frame[i];
  return sum;
}

void TosotAC::decode_report_(const std::vector<uint8_t> &frame) {
  const uint8_t status = frame[8];
  const bool power = (status & 0x80) != 0;
  const uint8_t mode_code = static_cast<uint8_t>((status >> 4) & 0x07);
  const uint8_t fan_code = static_cast<uint8_t>(status & 0x03);
  const float target = static_cast<float>(16 + ((frame[9] >> 4) & 0x0F));
  const float current = static_cast<float>(static_cast<int>(frame[46]) - 40);

  climate::ClimateMode decoded_mode = climate::CLIMATE_MODE_OFF;
  if (power) {
    switch (mode_code) {
      case 0: decoded_mode = climate::CLIMATE_MODE_AUTO; break;
      case 1: decoded_mode = climate::CLIMATE_MODE_COOL; break;
      case 2: decoded_mode = climate::CLIMATE_MODE_DRY; break;
      case 3: decoded_mode = climate::CLIMATE_MODE_FAN_ONLY; break;
      case 4: decoded_mode = climate::CLIMATE_MODE_HEAT; break;
      default: decoded_mode = climate::CLIMATE_MODE_OFF; break;
    }
  }

  const auto previous_mode = this->mode;
  const float previous_target = this->target_temperature;
  const float previous_current = this->current_temperature;
  const uint8_t previous_fan = this->last_fan_code_;

  this->ready_ = true;
  this->last_mode_code_ = mode_code <= 4 ? mode_code : this->last_mode_code_;
  this->last_fan_code_ = fan_code;
  this->mode = decoded_mode;
  this->target_temperature = target;
  this->current_temperature = current;

  if (this->control_stage_ == 0) {
    this->desired_power_ = power;
    this->desired_mode_code_ = this->last_mode_code_;
    this->desired_fan_code_ = this->last_fan_code_;
    this->desired_target_temperature_ = target;
  }

  const bool mode_changed = !this->state_published_ || previous_mode != decoded_mode;
  const bool target_changed = !this->state_published_ || !std::isfinite(previous_target) ||
                              std::fabs(previous_target - target) > 0.01f;
  const bool current_changed = !this->state_published_ || !std::isfinite(previous_current) ||
                               std::fabs(previous_current - current) > 0.01f;
  const bool fan_changed = !this->state_published_ || previous_fan != fan_code;
  const bool state_changed = mode_changed || target_changed || current_changed || fan_changed;

  const uint32_t now = millis();
  const bool heartbeat_due = this->state_published_ && (now - this->last_publish_ms_ >= STATE_HEARTBEAT_MS);
  if (state_changed || this->force_publish_ || heartbeat_due) {
    this->publish_state();
    this->state_published_ = true;
    this->force_publish_ = false;
    this->last_publish_ms_ = now;
    this->publish_count_++;

    ESP_LOGD(TAG, "[%s] Published climate state (%s%s%s) publishes=%u", VERSION,
             state_changed ? "changed" : "",
             heartbeat_due ? (state_changed ? "+heartbeat" : "heartbeat") : "",
             (!state_changed && !heartbeat_due) ? "command-ack" : "",
             static_cast<unsigned>(this->publish_count_));
  }

  if (this->rx_report_31_count_ <= 3 || (this->rx_report_31_count_ % 20) == 0) {
    ESP_LOGI(TAG, "[%s] State: power=%s mode=%u target=%.0f current=%.0f fan=%u reports31=%u", VERSION,
             power ? "ON" : "OFF", mode_code, target, current, fan_code,
             static_cast<unsigned>(this->rx_report_31_count_));
  }
}

void TosotAC::report_summary_() {
  ESP_LOGI(TAG,
           "[%s] SUMMARY tx=%u rx_bytes=%u frames=%u reports_2F31=%u bad_checksum=%u resync=%u publishes=%u ready=%s",
           VERSION, static_cast<unsigned>(this->tx_count_), static_cast<unsigned>(this->rx_byte_count_),
           static_cast<unsigned>(this->rx_frame_count_), static_cast<unsigned>(this->rx_report_31_count_),
           static_cast<unsigned>(this->bad_checksum_count_), static_cast<unsigned>(this->rx_resync_count_),
           static_cast<unsigned>(this->publish_count_), this->ready_ ? "YES" : "no");
}

void TosotAC::log_frame_(const char *prefix, const std::vector<uint8_t> &frame) const {
  ESP_LOGV(TAG, "[%s] %s: %s", VERSION, prefix, format_hex_pretty(frame).c_str());
}

}  // namespace tosot_ac
}  // namespace esphome
