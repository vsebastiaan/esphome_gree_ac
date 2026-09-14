#include "tosot_ac.h"

#include <cmath>

#include "esphome/core/log.h"

namespace esphome {
namespace tosot_ac {

static const char *const TAG_GWH18 = "tosot_ac.gwh18";

static const char *const GWH18_FAN_OPTIONS[] = {
    "Automatisch",
    "Laag",
    "Midden",
    "Hoog",
    "Turbo",
};

static const char *const GWH18_VERTICAL_OPTIONS[] = {
    "Swing",
    "Hoogste",
    "Hoog",
    "Midden",
    "Laag",
    "Laagste",
};

static const char *const GWH18_ON_OFF_OPTIONS[] = {
    "Uit",
    "Aan",
};

climate::ClimateTraits TosotGWH18AC::traits() {
  auto traits = TosotAC::traits();
  traits.set_supported_fan_modes({});
  traits.set_supported_swing_modes({});
  return traits;
}

void TosotGWH18AC::loop() {
  TosotAC::loop();

  if (!this->ready_)
    return;

  const uint32_t now = millis();
  const bool heartbeat_due = this->gwh18_last_ui_publish_ms_ == 0 ||
                             now - this->gwh18_last_ui_publish_ms_ >= STATE_HEARTBEAT_MS;

  // Mirror the decoded room temperature as a normal ESPHome sensor. Homey
  // reliably maps SensorState values even on versions where the native
  // ClimateState current_temperature remains empty.
  if (this->room_temperature_sensor_ != nullptr) {
    const bool changed = !this->room_temperature_sensor_->has_state() ||
                         !std::isfinite(this->room_temperature_sensor_->state) ||
                         std::fabs(this->room_temperature_sensor_->state - this->current_temperature) > 0.01f;
    if (changed || heartbeat_due)
      this->room_temperature_sensor_->publish_state(this->current_temperature);
  }

  // ESPHome Select::publish_state() also invokes the select's state callbacks.
  // Those callbacks are our command handlers, so mark driver-originated state
  // publication to keep the heartbeat strictly read-only. User/Homey writes
  // run with this flag clear and continue to queue real AC commands.
  auto publish_select_state = [this](select::Select *entity, const char *state) {
    if (entity == nullptr)
      return;
    this->gwh18_ui_publish_in_progress_ = true;
    entity->publish_state(state);
    this->gwh18_ui_publish_in_progress_ = false;
  };

  if (this->gwh18_fan_speed_select_ != nullptr && this->last_fan_code_ <= 3) {
    const uint8_t ui_code = this->actual_turbo_ ? 4 : this->last_fan_code_;
    if (ui_code != this->gwh18_last_fan_ui_code_ || heartbeat_due) {
      this->gwh18_last_fan_ui_code_ = ui_code;
      publish_select_state(this->gwh18_fan_speed_select_, GWH18_FAN_OPTIONS[ui_code]);
    }
  }

  if (this->gwh18_vertical_swing_select_ != nullptr) {
    uint8_t ui_code = 0xFF;
    if (this->actual_vertical_swing_code_ == 1 ||
        (this->actual_vertical_swing_code_ >= 7 && this->actual_vertical_swing_code_ <= 11)) {
      ui_code = 1;
    } else if (this->actual_vertical_swing_code_ >= 2 && this->actual_vertical_swing_code_ <= 6) {
      ui_code = this->actual_vertical_swing_code_;
    }

    if (ui_code != 0xFF && (ui_code != this->gwh18_last_vertical_ui_code_ || heartbeat_due)) {
      this->gwh18_last_vertical_ui_code_ = ui_code;
      publish_select_state(this->gwh18_vertical_swing_select_, GWH18_VERTICAL_OPTIONS[ui_code - 1]);
    }
  }

  auto publish_bool_select = [heartbeat_due, &publish_select_state](select::Select *entity, bool state,
                                                                    int8_t &last_index) {
    if (entity == nullptr)
      return;
    const int8_t index = state ? 1 : 0;
    if (index == last_index && !heartbeat_due)
      return;
    last_index = index;
    publish_select_state(entity, GWH18_ON_OFF_OPTIONS[index]);
  };

  publish_bool_select(this->gwh18_display_select_, this->actual_display_power_, this->gwh18_last_display_ui_index_);
  publish_bool_select(this->gwh18_plasma_select_, this->actual_plasma_, this->gwh18_last_plasma_ui_index_);
  publish_bool_select(this->gwh18_beeper_select_, this->actual_beeper_, this->gwh18_last_beeper_ui_index_);
  publish_bool_select(this->gwh18_sleep_select_, this->actual_sleep_, this->gwh18_last_sleep_ui_index_);
  publish_bool_select(this->gwh18_xfan_select_, this->actual_xfan_, this->gwh18_last_xfan_ui_index_);
  publish_bool_select(this->gwh18_save_select_, this->actual_save_, this->gwh18_last_save_ui_index_);

  if (heartbeat_due)
    this->gwh18_last_ui_publish_ms_ = now;
}

void TosotGWH18AC::set_fan_speed_select(select::Select *value) {
  this->gwh18_fan_speed_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (this->gwh18_ui_publish_in_progress_)
      return;
    if (index > 4)
      return;

    if (index == 4) {
      if (this->desired_turbo_)
        return;
      this->desired_fan_code_ = 3;
      this->desired_turbo_ = true;
      ESP_LOGI(TAG_GWH18, "Fan speed request ui=Turbo protocol_fan=3 turbo=on");
      this->queue_control_("gwh18-fan-turbo");
      return;
    }

    uint8_t protocol_code = static_cast<uint8_t>(index);
    if (this->desired_power_ && this->desired_mode_code_ == 2)
      protocol_code = 1;

    if (protocol_code == this->desired_fan_code_ && !this->desired_turbo_)
      return;

    this->desired_fan_code_ = protocol_code;
    this->desired_turbo_ = false;
    ESP_LOGI(TAG_GWH18, "Fan speed request ui=%u protocol=%u turbo=off", static_cast<unsigned>(index),
             static_cast<unsigned>(protocol_code));
    this->queue_control_("gwh18-fan-speed");
  });
}

void TosotGWH18AC::set_vertical_swing_select(select::Select *value) {
  this->gwh18_vertical_swing_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (this->gwh18_ui_publish_in_progress_)
      return;
    if (index >= 6)
      return;

    const uint8_t protocol_code = static_cast<uint8_t>(index + 1);
    if (protocol_code == this->desired_vertical_swing_code_)
      return;

    this->desired_vertical_swing_code_ = protocol_code;
    this->desired_horizontal_swing_code_ = this->actual_horizontal_swing_code_;
    ESP_LOGI(TAG_GWH18, "Vertical louver request ui=%u protocol=%u", static_cast<unsigned>(index),
             static_cast<unsigned>(protocol_code));
    this->queue_control_("gwh18-vertical-louver");
  });
}

void TosotGWH18AC::set_display_select(select::Select *value) {
  this->gwh18_display_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (this->gwh18_ui_publish_in_progress_)
      return;
    if (index > 1)
      return;

    const bool power = index == 1;
    const uint8_t mode = 1;
    if (power == this->desired_display_power_ && (!power || this->desired_display_mode_ == mode))
      return;

    this->desired_display_power_ = power;
    if (power)
      this->desired_display_mode_ = mode;

    ESP_LOGI(TAG_GWH18, "Display request=%s", power ? "on" : "off");
    this->queue_control_("gwh18-display");
  });
}

void TosotGWH18AC::set_plasma_select(select::Select *value) {
  this->gwh18_plasma_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (this->gwh18_ui_publish_in_progress_)
      return;
    if (index > 1)
      return;
    const bool state = index == 1;
    if (state == this->desired_plasma_)
      return;
    this->desired_plasma_ = state;
    ESP_LOGI(TAG_GWH18, "Health/Plasma request=%s", state ? "on" : "off");
    this->queue_control_("gwh18-plasma");
  });
}

void TosotGWH18AC::set_beeper_select(select::Select *value) {
  this->gwh18_beeper_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (this->gwh18_ui_publish_in_progress_)
      return;
    if (index > 1)
      return;
    const bool state = index == 1;
    if (state == this->desired_beeper_)
      return;
    this->desired_beeper_ = state;
    ESP_LOGI(TAG_GWH18, "Beeper request=%s", state ? "on" : "off");
    this->queue_control_("gwh18-beeper");
  });
}

void TosotGWH18AC::set_sleep_select(select::Select *value) {
  this->gwh18_sleep_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (this->gwh18_ui_publish_in_progress_)
      return;
    if (index > 1)
      return;
    const bool state = index == 1;
    if (state == this->desired_sleep_)
      return;
    this->desired_sleep_ = state;
    ESP_LOGI(TAG_GWH18, "Sleep request=%s", state ? "on" : "off");
    this->queue_control_("gwh18-sleep");
  });
}

void TosotGWH18AC::set_xfan_select(select::Select *value) {
  this->gwh18_xfan_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (this->gwh18_ui_publish_in_progress_)
      return;
    if (index > 1)
      return;
    const bool state = index == 1;
    if (state == this->desired_xfan_)
      return;
    this->desired_xfan_ = state;
    ESP_LOGI(TAG_GWH18, "X-Fan request=%s", state ? "on" : "off");
    this->queue_control_("gwh18-xfan");
  });
}

void TosotGWH18AC::set_save_select(select::Select *value) {
  this->gwh18_save_select_ = value;
  value->add_on_state_callback([this](size_t index) {
    if (this->gwh18_ui_publish_in_progress_)
      return;
    if (index > 1)
      return;
    const bool state = index == 1;
    if (state == this->desired_save_)
      return;
    this->desired_save_ = state;
    ESP_LOGI(TAG_GWH18, "Save/Eco request=%s", state ? "on" : "off");
    this->queue_control_("gwh18-save");
  });
}

}  // namespace tosot_ac
}  // namespace esphome
