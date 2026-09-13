#include "tosot_ac.h"

#include "esphome/core/log.h"

namespace esphome {
namespace tosot_ac {

static const char *const TAG_GWH18 = "tosot_ac.gwh18";

static const char *const GWH18_VERTICAL_OPTIONS[] = {
    "1 - Volledige swing",
    "2 - Hoogste vaste stand",
    "3 - Hoge vaste stand",
    "4 - Middenstand",
    "5 - Lage vaste stand",
    "6 - Laagste vaste stand",
};

static const char *const GWH18_DISPLAY_OPTIONS[] = {
    "0 - Uit",
    "1 - Settemperatuur",
};

climate::ClimateTraits TosotGWH18AC::traits() {
  auto traits = TosotAC::traits();

  // Hardware test on the GWH18 showed that the generic ESPHome swing modes are
  // misleading for this unit: OFF does not stop the louver and the family-level
  // Vertical/Both mapping includes unsupported horizontal-louver bits. The exact
  // louver select below is therefore the single source of truth.
  traits.set_supported_swing_modes({});
  return traits;
}

void TosotGWH18AC::loop() {
  TosotAC::loop();

  if (!this->ready_)
    return;

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
    // Hardware test: the useful states are simply display off or showing the set
    // temperature. Other family-level display modes are not exposed on this unit.
    const int8_t display_index = this->actual_display_power_ ? 1 : 0;
    if (display_index != this->gwh18_last_display_ui_index_) {
      this->gwh18_last_display_ui_index_ = display_index;
      this->gwh18_display_select_->publish_state(GWH18_DISPLAY_OPTIONS[display_index]);
    }
  }
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

    ESP_LOGI(TAG_GWH18, "Display request=%s", power ? "set-temperature" : "off");
    this->queue_control_("gwh18-display");
  });
}

}  // namespace tosot_ac
}  // namespace esphome
