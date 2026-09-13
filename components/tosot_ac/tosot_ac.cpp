#include "tosot_ac.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/log.h"

namespace esphome {
namespace tosot_ac {

static const char *const TAG = "tosot_ac";
static const char *const VERSION = "tosot-gwh18-v1";

// Exact passive 2F/01 request captured from the original CS532AE on BLACK.
// This is known to make the GWH18AAD-K6DNA1B/I answer with a 2F/31 report.
static const uint8_t STOCK_POLL[50] = {
    0x7E, 0x7E, 0x2F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x60,
    0x02, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB4};

void TosotAC::setup() {
  this->last_tx_ms_ = millis() - POLL_INTERVAL_MS;
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = 22.0f;
  ESP_LOGI(TAG, "[%s] Starting exact-stock Tosot climate driver", VERSION);
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
  this->read_uart_();

  const uint32_t now = millis();
  if (now - this->last_tx_ms_ >= POLL_INTERVAL_MS) {
    this->send_next_();
    this->last_tx_ms_ = now;
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
      case climate::CLIMATE_MODE_AUTO:
        this->desired_mode_code_ = 0;
        break;
      case climate::CLIMATE_MODE_COOL:
        this->desired_mode_code_ = 1;
        break;
      case climate::CLIMATE_MODE_DRY:
        this->desired_mode_code_ = 2;
        break;
      case climate::CLIMATE_MODE_FAN_ONLY:
        this->desired_mode_code_ = 3;
        break;
      case climate::CLIMATE_MODE_HEAT:
        this->desired_mode_code_ = 4;
        break;
      case climate::CLIMATE_MODE_OFF:
      default:
        // Keep the last actual mode code; only clear the power bit.
        this->desired_mode_code_ = this->last_mode_code_;
        break;
    }
  }

  if (call.get_target_temperature().has_value()) {
    this->desired_target_temperature_ = std::max(16.0f, std::min(30.0f, *call.get_target_temperature()));
  }

  this->control_stage_ = 2;
  ESP_LOGI(TAG, "[%s] Control queued: power=%s mode=%u target=%.0f", VERSION,
           this->desired_power_ ? "ON" : "OFF", this->desired_mode_code_, this->desired_target_temperature_);
}

void TosotAC::read_uart_() {
  while (this->available() > 0) {
    uint8_t value = 0;
    if (!this->read_byte(&value))
      break;
    this->consume_byte_(value);
  }
}

void TosotAC::consume_byte_(uint8_t value) {
  if (this->rx_frame_.empty()) {
    if (value == 0x7E)
      this->rx_frame_.push_back(value);
    return;
  }

  if (this->rx_frame_.size() == 1) {
    if (value == 0x7E) {
      this->rx_frame_.push_back(value);
    } else {
      this->rx_frame_.clear();
    }
    return;
  }

  this->rx_frame_.push_back(value);

  if (this->rx_frame_.size() == 3) {
    const size_t expected = static_cast<size_t>(this->rx_frame_[2]) + 3U;
    if (expected < 5 || expected > FRAME_MAX) {
      this->rx_frame_.clear();
      this->rx_expected_ = 0;
      return;
    }
    this->rx_expected_ = expected;
  }

  if (this->rx_expected_ != 0 && this->rx_frame_.size() == this->rx_expected_) {
    this->finish_frame_();
    this->rx_frame_.clear();
    this->rx_expected_ = 0;
  } else if (this->rx_frame_.size() >= FRAME_MAX) {
    this->rx_frame_.clear();
    this->rx_expected_ = 0;
  }
}

void TosotAC::finish_frame_() {
  if (!this->checksum_ok_(this->rx_frame_)) {
    this->bad_checksum_count_++;
    ESP_LOGW(TAG, "[%s] Bad RX checksum (%u total)", VERSION, this->bad_checksum_count_);
    return;
  }

  if (this->rx_frame_.size() != 50 || this->rx_frame_[2] != 0x2F || this->rx_frame_[3] != 0x31) {
    ESP_LOGV(TAG, "[%s] Ignoring RX len=0x%02X type=0x%02X", VERSION, this->rx_frame_[2], this->rx_frame_[3]);
    return;
  }

  this->rx_count_++;
  this->last_report_ = this->rx_frame_;
  this->decode_report_(this->rx_frame_);

  if (this->rx_count_ <= 3 || (this->rx_count_ % 20) == 0)
    this->log_frame_("RX 2F/31", this->rx_frame_);
}

bool TosotAC::checksum_ok_(const std::vector<uint8_t> &frame) const {
  if (frame.size() < 5)
    return false;
  return this->checksum_(frame) == frame.back();
}

uint8_t TosotAC::checksum_(const std::vector<uint8_t> &frame) const {
  uint8_t sum = 0;
  for (size_t i = 2; i + 1 < frame.size(); i++)
    sum += frame[i];
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

  // Preserve feature/swing/display fields from the most recent unit report where
  // the Gree/Sinclair mapping is shared by SET and REPORT packets.
  if (this->last_report_.size() == 50) {
    frame[10] = this->last_report_[10];
    frame[11] = this->last_report_[11];
    frame[12] = this->last_report_[12];
    frame[13] = this->last_report_[13];
    frame[15] = this->last_report_[15];
    frame[44] = this->last_report_[44];
  }

  frame[7] = af ? 0xAF : 0x00;

  // Byte 8: power (bit 7), mode (bits 6..4), sleep/other preserved bits (3..2), fan (1..0).
  uint8_t status = this->last_report_.size() == 50 ? (this->last_report_[8] & 0x0C) : 0x00;
  if (this->desired_power_)
    status |= 0x80;
  status |= static_cast<uint8_t>((this->desired_mode_code_ & 0x07) << 4);
  status |= static_cast<uint8_t>(this->desired_fan_code_ & 0x03);
  frame[8] = status;

  const int target = static_cast<int>(std::round(this->desired_target_temperature_));
  frame[9] = static_cast<uint8_t>((std::max(16, std::min(30, target)) - 16) << 4);

  frame.back() = this->checksum_(frame);
  return frame;
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
      case 0:
        decoded_mode = climate::CLIMATE_MODE_AUTO;
        break;
      case 1:
        decoded_mode = climate::CLIMATE_MODE_COOL;
        break;
      case 2:
        decoded_mode = climate::CLIMATE_MODE_DRY;
        break;
      case 3:
        decoded_mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case 4:
        decoded_mode = climate::CLIMATE_MODE_HEAT;
        break;
      default:
        decoded_mode = climate::CLIMATE_MODE_OFF;
        break;
    }
  }

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

  this->publish_state();

  if (this->rx_count_ <= 3 || (this->rx_count_ % 20) == 0) {
    ESP_LOGI(TAG, "[%s] State: power=%s mode=%u target=%.0f current=%.0f fan=%u rx=%u bad=%u", VERSION,
             power ? "ON" : "OFF", mode_code, target, current, fan_code, this->rx_count_, this->bad_checksum_count_);
  }
}

void TosotAC::log_frame_(const char *prefix, const std::vector<uint8_t> &frame) const {
  ESP_LOGV(TAG, "[%s] %s: %s", VERSION, prefix, format_hex_pretty(frame).c_str());
}

}  // namespace tosot_ac
}  // namespace esphome
