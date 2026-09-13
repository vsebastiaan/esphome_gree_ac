#include "tosot_ac.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/log.h"

namespace esphome {
namespace tosot_ac {

static const char *const TAG = "tosot_ac";
static const char *const VERSION = "tosot-gwh18-v5";

// Exact passive 2F/01 request captured from the original CS532AE on BLACK.
static const uint8_t STOCK_POLL[50] = {
    0x7E, 0x7E, 0x2F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x60,
    0x02, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB4};

static const char *const HORIZONTAL_SWING_OPTIONS[] = {
    "0 - OFF",
    "1 - Swing - Full",
    "2 - Constant - Left",
    "3 - Constant - Mid-Left",
    "4 - Constant - Middle",
    "5 - Constant - Mid-Right",
    "6 - Constant - Right",
};

static const char *const VERTICAL_SWING_OPTIONS[] = {
    "00 - OFF",
    "01 - Swing - Full",
    "02 - Swing - Down",
    "03 - Swing - Mid-Down",
    "04 - Swing - Middle",
    "05 - Swing - Mid-Up",
    "06 - Swing - Up",
    "07 - Constant - Down",
    "08 - Constant - Mid-Down",
    "09 - Constant - Middle",
    "10 - Constant - Mid-Up",
    "11 - Constant - Up",
};

static const char *const DISPLAY_OPTIONS[] = {
    "0 - OFF",
    "1 - Auto",
    "2 - Set temperature",
    "3 - Actual temperature",
    "4 - Outside temperature",
};

static const char *const DISPLAY_UNIT_OPTIONS[] = {"C", "F"};

static uint8_t known_report_mask(uint8_t index) {
  switch (index) {
    case 8: return 0xFB;   // power, mode, sleep, fan; bit 2 remains unknown
    case 9: return 0xF0;   // target temperature
    case 10: return 0x0F;  // turbo, display power, health candidate, X-Fan
    case 11: return 0xC2;  // display unit, temperature flag, stock constant bit
    case 12: return 0xF7;  // vertical + horizontal swing; bit 3 remains unknown
    case 13: return 0x30;  // display mode; other bits are still discovery territory
    case 15: return 0x40;  // save/eco candidate
    case 20: return 0x08;  // quiet candidate (observed in related dialects)
    case 22: return 0x0F;  // detailed fan-speed candidate
    case 44: return 0x01;  // beeper/mute candidate
    case 46: return 0xFF;  // indoor temperature
    default: return 0x00;
  }
}

void TosotAC::setup() {
  const uint32_t now = millis();
  this->last_tx_ms_ = now - POLL_INTERVAL_MS;
  this->next_summary_ms_ = now + SUMMARY_INTERVAL_MS;
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = 22.0f;

  ESP_LOGI(TAG, "[%s] Starting exact-stock Tosot climate driver", VERSION);
  ESP_LOGI(TAG, "[%s] Known 2F/31 controls enabled; unmapped byte changes are tagged for discovery", VERSION);
}

climate::ClimateTraits TosotAC::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(30.0f);
  traits.set_visual_temperature_step(1.0f);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO, climate::CLIMATE_MODE_COOL,
                              climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_DRY});
  traits.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
                                  climate::CLIMATE_FAN_HIGH});
  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_BOTH,
                                    climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL});
  return traits;
}

void TosotAC::loop() {
  uint32_t now = millis();

  // Keep this receive loop byte-for-byte equivalent in behaviour to gree_replay.
  while (this->available() > 0) {
    uint8_t value = 0;
    if (!this->read_byte(&value))
      break;
    this->rx_byte_count_++;
    this->consume_rx_byte_(value);
  }

  now = millis();

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

  bool changed = false;

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
    changed = true;
  }

  if (call.get_target_temperature().has_value()) {
    this->desired_target_temperature_ =
        std::max(16.0f, std::min(30.0f, *call.get_target_temperature()));
    changed = true;
  }

  if (call.get_fan_mode().has_value()) {
    const auto requested = *call.get_fan_mode();
    this->fan_mode = requested;
    switch (requested) {
      case climate::CLIMATE_FAN_LOW: this->desired_fan_code_ = 1; break;
      case climate::CLIMATE_FAN_MEDIUM: this->desired_fan_code_ = 2; break;
      case climate::CLIMATE_FAN_HIGH: this->desired_fan_code_ = 3; break;
      case climate::CLIMATE_FAN_AUTO:
      default: this->desired_fan_code_ = 0; break;
    }
    // A normal fan selection exits Turbo, matching the stock/Sinclair mapping.
    this->desired_turbo_ = false;
    changed = true;
  }

  if (call.get_swing_mode().has_value()) {
    const auto requested = *call.get_swing_mode();
    this->swing_mode = requested;
    switch (requested) {
      case climate::CLIMATE_SWING_BOTH:
        this->desired_vertical_swing_code_ = 1;
        this->desired_horizontal_swing_code_ = 1;
        break;
      case climate::CLIMATE_SWING_VERTICAL:
        this->desired_vertical_swing_code_ = 1;
        this->desired_horizontal_swing_code_ = 4;
        break;
      case climate::CLIMATE_SWING_HORIZONTAL:
        this->desired_vertical_swing_code_ = 9;
        this->desired_horizontal_swing_code_ = 1;
        break;
      case climate::CLIMATE_SWING_OFF:
      default:
        // Keep the legacy behaviour: "off" parks both vanes in the middle.
        this->desired_vertical_swing_code_ = 9;
        this->desired_horizontal_swing_code_ = 4;
        break;
    }
    changed = true;
  }

  // DRY has only the low fan setting on this protocol family.
  if (this->desired_power_ && this->desired_mode_code_ == 2)
    this->desired_fan_code_ = 1;

  if (changed)
    this->queue_control_("climate");
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

    std::vector<uint8_t> frame(this->rx_buffer_, this->rx_buffer_ + size);
    this->log_report_delta_(frame);
    this->last_report_ = frame;
    this->decode_report_(this->last_report_);
  } else if (checksum_ok) {
    this->rx_other_valid_count_++;
    std::vector<uint8_t> frame(this->rx_buffer_, this->rx_buffer_ + size);
    ESP_LOGD(TAG, "[%s] Valid non-state frame type=0x%02X len=0x%02X other_valid=%u", VERSION, type,
             this->rx_buffer_[2], static_cast<unsigned>(this->rx_other_valid_count_));
    this->log_frame_("RX OTHER", frame);
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

void TosotAC::log_report_delta_(const std::vector<uint8_t> &frame) const {
  if (frame.size() != 50 || this->last_report_.size() != 50)
    return;

  for (uint8_t i = 4; i + 1 < frame.size(); i++) {
    if (frame[i] == this->last_report_[i])
      continue;
    const uint8_t changed_bits = static_cast<uint8_t>(this->last_report_[i] ^ frame[i]);
    const uint8_t unknown_bits = static_cast<uint8_t>(changed_bits & static_cast<uint8_t>(~known_report_mask(i)));
    ESP_LOGD(TAG, "[%s] RX DELTA byte=%u 0x%02X->0x%02X known_mask=0x%02X unknown_bits=0x%02X", VERSION, i,
             this->last_report_[i], frame[i], known_report_mask(i), unknown_bits);
  }
}

void TosotAC::queue_control_(const char *reason) {
  if (!this->ready_)
    return;

  this->force_publish_ = true;
  this->control_stage_ = 2;
  ESP_LOGI(TAG,
           "[%s] Control queued reason=%s power=%s mode=%u fan=%u target=%.0f h=%u v=%u turbo=%s sleep=%s",
           VERSION, reason, this->desired_power_ ? "ON" : "OFF", this->desired_mode_code_,
           this->desired_fan_code_, this->desired_target_temperature_, this->desired_horizontal_swing_code_,
           this->desired_vertical_swing_code_, this->desired_turbo_ ? "ON" : "off",
           this->desired_sleep_ ? "ON" : "off");
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

  // Only copy bytes that v4 already proved safe as writable state carriers.
  // Do not mirror report-only/telemetry bytes into a set packet.
  if (this->last_report_.size() == 50) {
    for (const uint8_t index : {10, 11, 12, 13, 15, 44})
      frame[index] = this->last_report_[index];
  }

  frame[7] = af ? 0xAF : 0x00;

  // frame[8]: PWR(7), MODE(6..4), SLEEP(3), unknown/preserved(2), FAN(1..0)
  uint8_t status = this->last_report_.size() == 50 ? (this->last_report_[8] & 0x04) : 0x00;
  if (this->desired_power_)
    status |= 0x80;
  status |= static_cast<uint8_t>((this->desired_mode_code_ & 0x07) << 4);
  if (this->desired_sleep_)
    status |= 0x08;
  status |= static_cast<uint8_t>(this->desired_fan_code_ & 0x03);
  frame[8] = status;

  const int target = static_cast<int>(std::round(this->desired_target_temperature_));
  const uint8_t target_nibble = static_cast<uint8_t>((std::max(16, std::min(30, target)) - 16) << 4);
  // Keep the proven v4 behaviour: low nibble is cleared in outbound set packets.
  frame[9] = target_nibble;

  // frame[10]: TURBO(0), DISPLAY(1), HEALTH candidate(2), X-FAN(3)
  frame[10] &= 0xF0;
  if (this->desired_turbo_)
    frame[10] |= 0x01;
  if (this->desired_display_power_)
    frame[10] |= 0x02;
  if (this->desired_plasma_)
    frame[10] |= 0x04;
  if (this->desired_xfan_)
    frame[10] |= 0x08;

  // frame[11] bit 1 is part of the stock set packet; bit 7 selects Fahrenheit.
  frame[11] |= 0x02;
  frame[11] &= static_cast<uint8_t>(~0x80);
  if (this->desired_display_f_)
    frame[11] |= 0x80;

  // frame[12]: V-swing in high nibble, H-swing in low 3 bits; preserve bit 3.
  frame[12] = static_cast<uint8_t>((frame[12] & 0x08) |
                                   ((this->desired_vertical_swing_code_ & 0x0F) << 4) |
                                   (this->desired_horizontal_swing_code_ & 0x07));

  frame[13] &= static_cast<uint8_t>(~0x30);
  frame[13] |= static_cast<uint8_t>((this->desired_display_mode_ & 0x03) << 4);

  frame[15] &= static_cast<uint8_t>(~0x40);
  if (this->desired_save_)
    frame[15] |= 0x40;

  // Beeper is inverted on the related protocol dialect: bit set means muted/off.
  frame[44] &= static_cast<uint8_t>(~0x01);
  if (!this->desired_beeper_)
    frame[44] |= 0x01;

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

  const bool sleep = (status & 0x08) != 0;
  const bool turbo = (frame[10] & 0x01) != 0;
  const bool display_power = (frame[10] & 0x02) != 0;
  const bool plasma = (frame[10] & 0x04) != 0;
  const bool xfan = (frame[10] & 0x08) != 0;
  const bool display_f = (frame[11] & 0x80) != 0;
  uint8_t horizontal_swing = static_cast<uint8_t>(frame[12] & 0x07);
  uint8_t vertical_swing = static_cast<uint8_t>((frame[12] >> 4) & 0x0F);
  const uint8_t display_mode = static_cast<uint8_t>((frame[13] >> 4) & 0x03);
  const bool save = (frame[15] & 0x40) != 0;
  const bool beeper = (frame[44] & 0x01) == 0;

  if (horizontal_swing > 6)
    horizontal_swing = 0;
  if (vertical_swing > 11)
    vertical_swing = 0;

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

  climate::ClimateFanMode decoded_fan = climate::CLIMATE_FAN_AUTO;
  switch (fan_code) {
    case 1: decoded_fan = climate::CLIMATE_FAN_LOW; break;
    case 2: decoded_fan = climate::CLIMATE_FAN_MEDIUM; break;
    case 3: decoded_fan = climate::CLIMATE_FAN_HIGH; break;
    case 0:
    default: decoded_fan = climate::CLIMATE_FAN_AUTO; break;
  }

  climate::ClimateSwingMode decoded_swing = climate::CLIMATE_SWING_OFF;
  const bool vertical_full = vertical_swing == 1;
  const bool horizontal_full = horizontal_swing == 1;
  if (vertical_full && horizontal_full)
    decoded_swing = climate::CLIMATE_SWING_BOTH;
  else if (vertical_full)
    decoded_swing = climate::CLIMATE_SWING_VERTICAL;
  else if (horizontal_full)
    decoded_swing = climate::CLIMATE_SWING_HORIZONTAL;

  const auto previous_mode = this->mode;
  const float previous_target = this->target_temperature;
  const float previous_current = this->current_temperature;
  const uint8_t previous_fan = this->last_fan_code_;
  const auto previous_swing = this->swing_mode;

  const bool advanced_changed =
      !this->advanced_state_initialized_ ||
      this->actual_horizontal_swing_code_ != horizontal_swing ||
      this->actual_vertical_swing_code_ != vertical_swing ||
      this->actual_display_mode_ != display_mode ||
      this->actual_display_power_ != display_power ||
      this->actual_display_f_ != display_f ||
      this->actual_turbo_ != turbo ||
      this->actual_plasma_ != plasma ||
      this->actual_beeper_ != beeper ||
      this->actual_sleep_ != sleep ||
      this->actual_xfan_ != xfan ||
      this->actual_save_ != save;

  this->ready_ = true;
  this->last_mode_code_ = mode_code <= 4 ? mode_code : this->last_mode_code_;
  this->last_fan_code_ = fan_code;
  this->mode = decoded_mode;
  this->fan_mode = decoded_fan;
  this->swing_mode = decoded_swing;
  this->target_temperature = target;
  this->current_temperature = current;

  this->actual_horizontal_swing_code_ = horizontal_swing;
  this->actual_vertical_swing_code_ = vertical_swing;
  this->actual_display_mode_ = display_mode;
  this->actual_display_power_ = display_power;
  this->actual_display_f_ = display_f;
  this->actual_turbo_ = turbo;
  this->actual_plasma_ = plasma;
  this->actual_beeper_ = beeper;
  this->actual_sleep_ = sleep;
  this->actual_xfan_ = xfan;
  this->actual_save_ = save;

  if (this->control_stage_ == 0) {
    this->desired_power_ = power;
    this->desired_mode_code_ = this->last_mode_code_;
    this->desired_fan_code_ = this->last_fan_code_;
    this->desired_target_temperature_ = target;
    this->desired_horizontal_swing_code_ = horizontal_swing;
    this->desired_vertical_swing_code_ = vertical_swing;
    this->desired_display_mode_ = display_mode;
    this->desired_display_power_ = display_power;
    this->desired_display_f_ = display_f;
    this->desired_turbo_ = turbo;
    this->desired_plasma_ = plasma;
    this->desired_beeper_ = beeper;
    this->desired_sleep_ = sleep;
    this->desired_xfan_ = xfan;
    this->desired_save_ = save;
  }

  if (advanced_changed)
    this->publish_advanced_state_(!this->advanced_state_initialized_);
  this->advanced_state_initialized_ = true;

  const bool mode_changed = !this->state_published_ || previous_mode != decoded_mode;
  const bool target_changed = !this->state_published_ || !std::isfinite(previous_target) ||
                              std::fabs(previous_target - target) > 0.01f;
  const bool current_changed = !this->state_published_ || !std::isfinite(previous_current) ||
                               std::fabs(previous_current - current) > 0.01f;
  const bool fan_changed = !this->state_published_ || previous_fan != fan_code;
  const bool swing_changed = !this->state_published_ || previous_swing != decoded_swing;
  const bool state_changed = mode_changed || target_changed || current_changed || fan_changed || swing_changed;

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
    ESP_LOGI(TAG,
             "[%s] State: power=%s mode=%u target=%.0f current=%.0f fan=%u h=%u v=%u turbo=%s reports31=%u",
             VERSION, power ? "ON" : "OFF", mode_code, target, current, fan_code, horizontal_swing, vertical_swing,
             turbo ? "ON" : "off", static_cast<unsigned>(this->rx_report_31_count_));
  }
}

void TosotAC::publish_advanced_state_(bool force) {
  (void) force;

  if (this->horizontal_swing_select_ != nullptr && this->actual_horizontal_swing_code_ <= 6)
    this->horizontal_swing_select_->publish_state(HORIZONTAL_SWING_OPTIONS[this->actual_horizontal_swing_code_]);

  if (this->vertical_swing_select_ != nullptr && this->actual_vertical_swing_code_ <= 11)
    this->vertical_swing_select_->publish_state(VERTICAL_SWING_OPTIONS[this->actual_vertical_swing_code_]);

  if (this->display_select_ != nullptr) {
    const uint8_t display_index = this->actual_display_power_ ? this->actual_display_mode_ + 1 : 0;
    this->display_select_->publish_state(DISPLAY_OPTIONS[display_index]);
  }

  if (this->display_unit_select_ != nullptr)
    this->display_unit_select_->publish_state(DISPLAY_UNIT_OPTIONS[this->actual_display_f_ ? 1 : 0]);

  if (this->turbo_switch_ != nullptr)
    this->turbo_switch_->publish_state(this->actual_turbo_);
  if (this->plasma_switch_ != nullptr)
    this->plasma_switch_->publish_state(this->actual_plasma_);
  if (this->beeper_switch_ != nullptr)
    this->beeper_switch_->publish_state(this->actual_beeper_);
  if (this->sleep_switch_ != nullptr)
    this->sleep_switch_->publish_state(this->actual_sleep_);
  if (this->xfan_switch_ != nullptr)
    this->xfan_switch_->publish_state(this->actual_xfan_);
  if (this->save_switch_ != nullptr)
    this->save_switch_->publish_state(this->actual_save_);
}

void TosotAC::set_horizontal_swing_select(select::Select *value) {
  this->horizontal_swing_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (index > 6 || index == this->desired_horizontal_swing_code_)
      return;
    this->desired_horizontal_swing_code_ = static_cast<uint8_t>(index);
    this->queue_control_("horizontal-swing");
  });
}

void TosotAC::set_vertical_swing_select(select::Select *value) {
  this->vertical_swing_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (index > 11 || index == this->desired_vertical_swing_code_)
      return;
    this->desired_vertical_swing_code_ = static_cast<uint8_t>(index);
    this->queue_control_("vertical-swing");
  });
}

void TosotAC::set_display_select(select::Select *value) {
  this->display_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (index > 4)
      return;

    bool new_power = index != 0;
    uint8_t new_mode = this->desired_display_mode_;
    if (index > 0)
      new_mode = static_cast<uint8_t>(index - 1);

    if (new_power == this->desired_display_power_ && new_mode == this->desired_display_mode_)
      return;

    this->desired_display_power_ = new_power;
    this->desired_display_mode_ = new_mode;
    this->queue_control_("display");
  });
}

void TosotAC::set_display_unit_select(select::Select *value) {
  this->display_unit_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (index > 1)
      return;
    const bool fahrenheit = index == 1;
    if (fahrenheit == this->desired_display_f_)
      return;
    this->desired_display_f_ = fahrenheit;
    this->queue_control_("display-unit");
  });
}

void TosotAC::set_turbo_switch(switch_::Switch *value) {
  this->turbo_switch_ = value;
  value->add_on_state_callback([this](bool state) {
    if (state == this->desired_turbo_)
      return;
    this->desired_turbo_ = state;
    if (state)
      this->desired_fan_code_ = 3;
    this->queue_control_("turbo");
  });
}

void TosotAC::set_plasma_switch(switch_::Switch *value) {
  this->plasma_switch_ = value;
  value->add_on_state_callback([this](bool state) {
    if (state == this->desired_plasma_)
      return;
    this->desired_plasma_ = state;
    this->queue_control_("plasma");
  });
}

void TosotAC::set_beeper_switch(switch_::Switch *value) {
  this->beeper_switch_ = value;
  value->add_on_state_callback([this](bool state) {
    if (state == this->desired_beeper_)
      return;
    this->desired_beeper_ = state;
    this->queue_control_("beeper");
  });
}

void TosotAC::set_sleep_switch(switch_::Switch *value) {
  this->sleep_switch_ = value;
  value->add_on_state_callback([this](bool state) {
    if (state == this->desired_sleep_)
      return;
    this->desired_sleep_ = state;
    this->queue_control_("sleep");
  });
}

void TosotAC::set_xfan_switch(switch_::Switch *value) {
  this->xfan_switch_ = value;
  value->add_on_state_callback([this](bool state) {
    if (state == this->desired_xfan_)
      return;
    this->desired_xfan_ = state;
    this->queue_control_("xfan");
  });
}

void TosotAC::set_save_switch(switch_::Switch *value) {
  this->save_switch_ = value;
  value->add_on_state_callback([this](bool state) {
    if (state == this->desired_save_)
      return;
    this->desired_save_ = state;
    this->queue_control_("save");
  });
}

void TosotAC::report_summary_() {
  ESP_LOGI(TAG,
           "[%s] SUMMARY tx=%u rx_bytes=%u frames=%u reports_2F31=%u other_valid=%u bad_checksum=%u resync=%u "
           "publishes=%u ready=%s",
           VERSION, static_cast<unsigned>(this->tx_count_), static_cast<unsigned>(this->rx_byte_count_),
           static_cast<unsigned>(this->rx_frame_count_), static_cast<unsigned>(this->rx_report_31_count_),
           static_cast<unsigned>(this->rx_other_valid_count_), static_cast<unsigned>(this->bad_checksum_count_),
           static_cast<unsigned>(this->rx_resync_count_), static_cast<unsigned>(this->publish_count_),
           this->ready_ ? "YES" : "no");
}

void TosotAC::log_frame_(const char *prefix, const std::vector<uint8_t> &frame) const {
  ESP_LOGV(TAG, "[%s] %s: %s", VERSION, prefix, format_hex_pretty(frame).c_str());
}

}  // namespace tosot_ac
}  // namespace esphome
