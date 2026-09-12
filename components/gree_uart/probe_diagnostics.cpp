#include "gree.h"

namespace esphome {
namespace gree_uart {

static const char *const PROBE_DIAGNOSTICS_VERSION = "probe-diag-v2";
static const uint32_t PROBE_REPORT_INTERVAL_MS = 10000;

void GreeClimate::setup() {
  ESP_LOGI("gree", "Firmware diagnostics: %s; probe results repeat every 10s", PROBE_DIAGNOSTICS_VERSION);
  // Independent of the old one-shot report and of Wi-Fi/API connection timing.
  // PollingComponent still schedules update() normally. This is a separate,
  // named reporting timer: it never restarts the probe or sends UART commands.
  this->set_interval("gree-probe-report", PROBE_REPORT_INTERVAL_MS, [this]() { this->log_probe_result(); });
}

void GreeClimate::log_probe_result() {
  ESP_LOGI("gree", "[%s] PROBE_RESULT state=%s sent=%u/7 captured_frames=%u (stored, max 8)",
           PROBE_DIAGNOSTICS_VERSION, this->startup_probe_done_ ? "DONE" : "RUNNING",
           static_cast<unsigned>(this->startup_probe_step_), static_cast<unsigned>(this->startup_capture_count_));

  // Only complete frames retained by the existing parser are counted above.
  // A zero count is NOT proof that no bytes arrived or that the wire is quiet.
  // Replay one retained frame per report, keeping bursts small on ESP8266/API.
  // Continue cycling while powered, so a late logger can see every saved frame.
  if (this->startup_capture_count_ == 0)
    return;
  if (this->startup_capture_count_ > 8) {
    ESP_LOGW("gree", "[%s] Invalid capture count; not reading capture storage", PROBE_DIAGNOSTICS_VERSION);
    return;
  }
  if (this->startup_report_frame_index_ >= this->startup_capture_count_)
    this->startup_report_frame_index_ = 0;

  const uint8_t index = this->startup_report_frame_index_++;
  const uint8_t size = this->startup_capture_size_[index];
  if (size == 0 || size > GREE_RX_BUFFER_SIZE) {
    ESP_LOGW("gree", "[%s] Invalid stored frame size: %u", PROBE_DIAGNOSTICS_VERSION, static_cast<unsigned>(size));
    return;
  }

  static const char HEX[] = "0123456789ABCDEF";
  char text[3 * GREE_RX_BUFFER_SIZE + 1];
  size_t pos = 0;
  for (uint8_t i = 0; i < size; i++) {
    const uint8_t value = this->startup_capture_[index][i];
    if (i != 0)
      text[pos++] = ' ';
    text[pos++] = HEX[value >> 4];
    text[pos++] = HEX[value & 0x0F];
  }
  text[pos] = '\0';
  ESP_LOGI("gree", "[%s] CAPTURED_RX %u/%u len=%u: %s", PROBE_DIAGNOSTICS_VERSION,
           static_cast<unsigned>(index + 1), static_cast<unsigned>(this->startup_capture_count_),
           static_cast<unsigned>(size), text);
}

}  // namespace gree_uart
}  // namespace esphome
