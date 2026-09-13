#include "tosot_ac.h"

#include "esphome/core/log.h"

namespace esphome {
namespace tosot_ac {

static const char *const TAG_GWH18 = "tosot_ac.gwh18";

static const char *const GWH18_FAN_OPTIONS[] = {
    "Auto",
    "Laag",
    "Midden",
    "Hoog",
};

static const char *const GWH18_VERTICAL_OPTIONS[] = {
    "Swing",
    "Hoogste",
    "Hoog",
    "Midden",
    "Laag",
    "Laagste",
};

static const char *const GWH18_DISPLAY_OPTIONS[] = {
    "Uit",
    "Aan",
};

// Stock CS532AE/Gree startup frames captured/documented for this UART family.
// These do not change climate settings; they are the module initialization
// traffic sent before normal state polling.
static const uint8_t GWH18_STARTUP_10[] = {
    0x7E, 0x7E, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x28, 0x1E, 0x19, 0x23, 0x23, 0x00, 0xB8};
static const uint8_t GWH18_STARTUP_05[] = {
    0x7E, 0x7E, 0x05, 0x04, 0x07, 0x00, 0x00, 0x10};
static const uint8_t GWH18_STARTUP_0E_A[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12};
static const uint8_t GWH18_STARTUP_0E_B[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x90};
static const uint8_t GWH18_STARTUP_0E_NO_DHCP[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x8F};
static const uint8_t GWH18_STARTUP_0E_CONNECTED[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x7E, 0x0F};

climate::ClimateTraits TosotGWH18AC::traits() {
  auto traits = TosotAC::traits();

  // Homey labels the generic climate fan field simply as "Mode". On this GWH18
  // that is confusing next to the real thermostat mode, so expose fan speed as
  // a separate named select instead.
  traits.set_supported_fan_modes({});

  // Hardware test on the GWH18 showed that the generic ESPHome swing modes are
  // misleading for this unit: OFF does not stop the louver and the family-level
  // Vertical/Both mapping includes unsupported horizontal-louver bits. The exact
  // louver select below is therefore the single source of truth.
  traits.set_supported_swing_modes({});
  return traits;
}

void TosotGWH18AC::arm_startup_cycle_() {
  this->gwh18_startup_cycle_++;
  this->gwh18_startup_step_ = 0;
  this->gwh18_startup_poll_count_ = 0;
  this->gwh18_startup_polling_ = false;
  this->gwh18_startup_next_ms_ = millis() + 150;

  ESP_LOGW(TAG_GWH18, "INIT cycle=%u: kick released, starting stock CS532AE handshake",
           static_cast<unsigned>(this->gwh18_startup_cycle_));
}

void TosotGWH18AC::run_startup_sequence_() {
  const uint32_t now = millis();

  if (this->gwh18_startup_next_ms_ == 0) {
    this->arm_startup_cycle_();
    return;
  }

  if (static_cast<int32_t>(now - this->gwh18_startup_next_ms_) < 0)
    return;

  if (!this->gwh18_startup_polling_ && this->gwh18_startup_step_ < 7) {
    const uint8_t *message = nullptr;
    size_t size = 0;
    const char *label = nullptr;

    switch (this->gwh18_startup_step_) {
      case 0:
        message = GWH18_STARTUP_10;
        size = sizeof(GWH18_STARTUP_10);
        label = "0x10";
        break;
      case 1:
      case 2:
        message = GWH18_STARTUP_05;
        size = sizeof(GWH18_STARTUP_05);
        label = "0x05";
        break;
      case 3:
        message = GWH18_STARTUP_0E_A;
        size = sizeof(GWH18_STARTUP_0E_A);
        label = "0x0E-start-A";
        break;
      case 4:
        message = GWH18_STARTUP_0E_B;
        size = sizeof(GWH18_STARTUP_0E_B);
        label = "0x0E-start-B";
        break;
      case 5:
        message = GWH18_STARTUP_0E_NO_DHCP;
        size = sizeof(GWH18_STARTUP_0E_NO_DHCP);
        label = "0x0E-no-dhcp";
        break;
      case 6:
        message = GWH18_STARTUP_0E_CONNECTED;
        size = sizeof(GWH18_STARTUP_0E_CONNECTED);
        label = "0x0E-connected";
        break;
      default:
        break;
    }

    if (message != nullptr) {
      this->write_array(message, size);
      this->tx_count_++;
      ESP_LOGI(TAG_GWH18, "INIT cycle=%u handshake %u/7 %s",
               static_cast<unsigned>(this->gwh18_startup_cycle_),
               static_cast<unsigned>(this->gwh18_startup_step_ + 1), label);
    }

    this->gwh18_startup_step_++;
    this->gwh18_startup_next_ms_ = now + 300;
    return;
  }

  if (!this->gwh18_startup_polling_) {
    this->gwh18_startup_polling_ = true;
    this->gwh18_startup_poll_count_ = 0;
    this->gwh18_startup_next_ms_ = now;
    ESP_LOGI(TAG_GWH18, "INIT cycle=%u handshake complete; polling for 2F/31",
             static_cast<unsigned>(this->gwh18_startup_cycle_));
  }

  if (this->gwh18_startup_poll_count_ < 10) {
    this->send_passive_poll_();
    this->last_tx_ms_ = now;
    this->gwh18_startup_poll_count_++;
    this->gwh18_startup_next_ms_ = now + 300;
    return;
  }

  ESP_LOGW(TAG_GWH18, "INIT cycle=%u: no 2F/31 after handshake + 10 polls; restarting with D5 kick",
           static_cast<unsigned>(this->gwh18_startup_cycle_));
  this->gwh18_startup_next_ms_ = 0;
  this->gwh18_startup_step_ = 0;
  this->gwh18_startup_poll_count_ = 0;
  this->gwh18_startup_polling_ = false;

  if (this->kick_pin_ != nullptr)
    this->start_kick_("gwh18-init-cycle");
  else
    this->arm_startup_cycle_();
}

void TosotGWH18AC::loop() {
  const uint32_t now = millis();

  if (!this->ready_) {
    // While GWH18 initialization is active, suppress the generic driver's
    // every-2-second startup kick and normal 300 ms poll. We still call the base
    // loop so it can parse incoming bytes, release an active kick, and report
    // diagnostics.
    if (!this->kick_active_) {
      this->last_kick_ms_ = now;
      this->last_tx_ms_ = now;
    }

    TosotAC::loop();

    if (!this->ready_) {
      if (!this->kick_active_)
        this->run_startup_sequence_();
      return;
    }

    ESP_LOGI(TAG_GWH18, "INIT success in cycle=%u after kicks=%u; normal polling enabled",
             static_cast<unsigned>(this->gwh18_startup_cycle_),
             static_cast<unsigned>(this->kick_count_));
  } else {
    TosotAC::loop();
  }

  if (this->gwh18_fan_speed_select_ != nullptr && this->last_fan_code_ <= 3 &&
      this->last_fan_code_ != this->gwh18_last_fan_ui_code_) {
    this->gwh18_last_fan_ui_code_ = this->last_fan_code_;
    this->gwh18_fan_speed_select_->publish_state(GWH18_FAN_OPTIONS[this->last_fan_code_]);
  }

  if (this->gwh18_vertical_swing_select_ != nullptr) {
    uint8_t ui_code = 0xFF;
    if (this->actual_vertical_swing_code_ == 1 ||
        (this->actual_vertical_swing_code_ >= 7 && this->actual_vertical_swing_code_ <= 11)) {
      // Codes 7..11 were tested on the physical GWH18 and behave like code 1.
      ui_code = 1;
    } else if (this->actual_vertical_swing_code_ >= 2 && this->actual_vertical_swing_code_ <= 6) {
      ui_code = this->actual_vertical_swing_code_;
    }

    if (ui_code != 0xFF && ui_code != this->gwh18_last_vertical_ui_code_) {
      this->gwh18_last_vertical_ui_code_ = ui_code;
      this->gwh18_vertical_swing_select_->publish_state(GWH18_VERTICAL_OPTIONS[ui_code - 1]);
    }
  }

  if (this->gwh18_display_select_ != nullptr) {
    // Hardware test: the useful states are simply display off or on. When on,
    // this model shows the set temperature.
    const int8_t display_index = this->actual_display_power_ ? 1 : 0;
    if (display_index != this->gwh18_last_display_ui_index_) {
      this->gwh18_last_display_ui_index_ = display_index;
      this->gwh18_display_select_->publish_state(GWH18_DISPLAY_OPTIONS[display_index]);
    }
  }
}

void TosotGWH18AC::set_fan_speed_select(select::Select *value) {
  this->gwh18_fan_speed_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (index > 3)
      return;

    uint8_t protocol_code = static_cast<uint8_t>(index);
    // DRY only accepts low fan on this protocol family.
    if (this->desired_power_ && this->desired_mode_code_ == 2)
      protocol_code = 1;

    if (protocol_code == this->desired_fan_code_ && !this->desired_turbo_)
      return;

    this->desired_fan_code_ = protocol_code;
    // Manual fan selection exits Turbo, same behaviour as the generic control.
    this->desired_turbo_ = false;
    ESP_LOGI(TAG_GWH18, "Fan speed request ui=%u protocol=%u", static_cast<unsigned>(index),
             static_cast<unsigned>(protocol_code));
    this->queue_control_("gwh18-fan-speed");
  });
}

void TosotGWH18AC::set_vertical_swing_select(select::Select *value) {
  this->gwh18_vertical_swing_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (index >= 6)
      return;

    const uint8_t protocol_code = static_cast<uint8_t>(index + 1);
    if (protocol_code == this->desired_vertical_swing_code_)
      return;

    this->desired_vertical_swing_code_ = protocol_code;
    // Preserve the horizontal bits exactly as reported by the unit. The GWH18
    // has no separately useful horizontal-louver control in our hardware test.
    this->desired_horizontal_swing_code_ = this->actual_horizontal_swing_code_;
    ESP_LOGI(TAG_GWH18, "Vertical louver request ui=%u protocol=%u", static_cast<unsigned>(index),
             static_cast<unsigned>(protocol_code));
    this->queue_control_("gwh18-vertical-louver");
  });
}

void TosotGWH18AC::set_display_select(select::Select *value) {
  this->gwh18_display_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (index > 1)
      return;

    const bool power = index == 1;
    const uint8_t mode = 1;  // Set-temperature display mode in this protocol family.
    if (power == this->desired_display_power_ && (!power || this->desired_display_mode_ == mode))
      return;

    this->desired_display_power_ = power;
    if (power)
      this->desired_display_mode_ = mode;

    ESP_LOGI(TAG_GWH18, "Display request=%s", power ? "on" : "off");
    this->queue_control_("gwh18-display");
  });
}

}  // namespace tosot_ac
}  // namespace esphome
