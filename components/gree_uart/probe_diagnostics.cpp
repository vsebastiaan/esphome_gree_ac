#include "gree.h"

#include <algorithm>

namespace esphome {
namespace gree_uart {

static const char *const PROBE_DIAGNOSTICS_VERSION = "dialect-probe-v3";
static const uint32_t PROBE_REPORT_INTERVAL_MS = 10000;

void GreeClimate::setup() {
  ESP_LOGI("gree", "Firmware diagnostics: %s; results repeat every 10s", PROBE_DIAGNOSTICS_VERSION);
  this->set_interval("gree-probe-report", PROBE_REPORT_INTERVAL_MS, [this]() { this->log_probe_result(); });
}

void GreeClimate::log_dialect_probe_result() {
  const char *state = this->dialect_probe_active_ ? "RUNNING" :
                      (this->dialect_probe_found_rx_ ? "FOUND_RX" : "NO_RX");
  const char *hit = this->dialect_probe_first_hit_step_ == 0xFF ? "none" :
                    this->dialect_probe_label_(this->dialect_probe_first_hit_step_);

  ESP_LOGI("gree", "[%s] DIALECT_RESULT state=%s sent=%u/%u rx_total=%u stored=%u first_hit=%s",
           PROBE_DIAGNOSTICS_VERSION, state, static_cast<unsigned>(this->dialect_probe_sent_),
           static_cast<unsigned>(DIALECT_PROBE_TOTAL_STEPS), static_cast<unsigned>(this->dialect_probe_rx_total_),
           static_cast<unsigned>(this->dialect_probe_rx_stored_), hit);

  if (this->dialect_probe_rx_total_ == 0)
    return;

  // Rotate through probe steps that actually received bytes. This makes late or
  // reconnected log viewers eventually see every retained response.
  for (uint8_t scan = 0; scan < DIALECT_PROBE_TOTAL_STEPS; scan++) {
    const uint8_t step = (this->dialect_probe_report_step_cursor_ + scan) % DIALECT_PROBE_TOTAL_STEPS;
    const uint16_t count = this->dialect_probe_step_rx_[step];
    const uint16_t offset = this->dialect_probe_step_offset_[step];
    if (count == 0 || offset == 0xFFFF || offset >= this->dialect_probe_rx_stored_)
      continue;

    const uint16_t available = this->dialect_probe_rx_stored_ - offset;
    const uint16_t shown = std::min<uint16_t>(std::min<uint16_t>(count, available), 96);
    static const char hex_digits[] = "0123456789ABCDEF";
    char text[3 * 96 + 1];
    size_t pos = 0;
    for (uint16_t i = 0; i < shown; i++) {
      const uint8_t value = this->dialect_probe_rx_[offset + i];
      if (i != 0)
        text[pos++] = ' ';
      text[pos++] = hex_digits[value >> 4];
      text[pos++] = hex_digits[value & 0x0F];
    }
    text[pos] = '\0';

    ESP_LOGI("gree", "[%s] DIALECT_RX step=%u %s bytes=%u shown=%u: %s",
             PROBE_DIAGNOSTICS_VERSION, static_cast<unsigned>(step + 1), this->dialect_probe_label_(step),
             static_cast<unsigned>(count), static_cast<unsigned>(shown), text);
    this->dialect_probe_report_step_cursor_ = (step + 1) % DIALECT_PROBE_TOTAL_STEPS;
    break;
  }
}

void GreeClimate::log_probe_result() {
  this->log_dialect_probe_result();
}

}  // namespace gree_uart
}  // namespace esphome
