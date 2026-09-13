#include "gree_sniffer.h"

#include <algorithm>
#include <cstdio>

#include "esphome/core/log.h"

namespace esphome {
namespace gree_sniffer {

static const char *const TAG = "gree_sniffer";
static const char *const VERSION = "sniffer-v1";

void GreeSniffer::setup() {
  ESP_LOGI(TAG, "[%s] Passive RX-only sniffer starting; channel=%s", VERSION, this->channel_.c_str());
  ESP_LOGI(TAG, "[%s] No TX pin is used by this component", VERSION);
  this->set_interval("gree-sniffer-report", REPORT_INTERVAL_MS, [this]() { this->report_(); });
}

void GreeSniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "Gree passive UART sniffer:");
  ESP_LOGCONFIG(TAG, "  Version: %s", VERSION);
  ESP_LOGCONFIG(TAG, "  Channel: %s", this->channel_.c_str());
  ESP_LOGCONFIG(TAG, "  Capture: %u bytes, %u bursts, gap=%ums", static_cast<unsigned>(CAPTURE_SIZE),
                static_cast<unsigned>(MAX_BURSTS), static_cast<unsigned>(BURST_GAP_MS));
  this->check_uart_settings(4800, 1, uart::UART_CONFIG_PARITY_EVEN, 8);
}

void GreeSniffer::loop() {
  while (this->available() > 0) {
    uint8_t value = 0;
    if (!this->read_byte(&value))
      break;
    this->capture_byte_(value, millis());
  }
}

void GreeSniffer::capture_byte_(uint8_t value, uint32_t now) {
  this->rx_total_++;

  if (!this->have_first_rx_) {
    this->have_first_rx_ = true;
    this->first_rx_ms_ = now;
  }

  const bool new_burst = !this->burst_open_ || static_cast<uint32_t>(now - this->last_byte_ms_) > BURST_GAP_MS;
  if (new_burst) {
    this->burst_open_ = false;
    if (this->burst_count_ < MAX_BURSTS && this->stored_ < CAPTURE_SIZE) {
      Burst &burst = this->bursts_[this->burst_count_++];
      burst.offset = this->stored_;
      burst.length = 0;
      burst.start_rel_ms = static_cast<uint32_t>(now - this->first_rx_ms_);
      this->burst_open_ = true;
    } else {
      this->burst_overflow_ = true;
    }
  }

  if (this->burst_open_ && this->stored_ < CAPTURE_SIZE) {
    this->data_[this->stored_++] = value;
    this->bursts_[this->burst_count_ - 1].length++;
  } else {
    this->capture_overflow_ = true;
  }

  this->last_byte_ms_ = now;
}

void GreeSniffer::report_() {
  ESP_LOGI(TAG,
           "[%s] SUMMARY channel=%s rx_total=%u stored=%u/%u bursts=%u/%u data_overflow=%s burst_overflow=%s",
           VERSION, this->channel_.c_str(), static_cast<unsigned>(this->rx_total_),
           static_cast<unsigned>(this->stored_), static_cast<unsigned>(CAPTURE_SIZE),
           static_cast<unsigned>(this->burst_count_), static_cast<unsigned>(MAX_BURSTS),
           this->capture_overflow_ ? "YES" : "no", this->burst_overflow_ ? "YES" : "no");

  if (this->rx_total_ == 0 || this->burst_count_ == 0)
    return;

  this->report_next_chunk_();
}

void GreeSniffer::report_next_chunk_() {
  for (uint8_t attempts = 0; attempts < this->burst_count_; attempts++) {
    if (this->report_burst_ >= this->burst_count_) {
      this->report_burst_ = 0;
      this->report_offset_ = 0;
    }

    const Burst &burst = this->bursts_[this->report_burst_];
    if (burst.length == 0) {
      this->report_burst_++;
      this->report_offset_ = 0;
      continue;
    }

    if (this->report_offset_ >= burst.length) {
      this->report_burst_++;
      this->report_offset_ = 0;
      continue;
    }

    const uint16_t remaining = burst.length - this->report_offset_;
    const uint16_t shown = std::min<uint16_t>(remaining, REPORT_CHUNK);
    char text[3 * REPORT_CHUNK + 1];
    size_t pos = 0;
    static const char hex_digits[] = "0123456789ABCDEF";

    for (uint16_t i = 0; i < shown; i++) {
      const uint8_t value = this->data_[burst.offset + this->report_offset_ + i];
      if (i != 0)
        text[pos++] = ' ';
      text[pos++] = hex_digits[value >> 4];
      text[pos++] = hex_digits[value & 0x0F];
    }
    text[pos] = '\0';

    ESP_LOGI(TAG,
             "[%s] RX channel=%s burst=%u/%u t=+%ums offset=%u/%u shown=%u: %s",
             VERSION, this->channel_.c_str(), static_cast<unsigned>(this->report_burst_ + 1),
             static_cast<unsigned>(this->burst_count_), static_cast<unsigned>(burst.start_rel_ms),
             static_cast<unsigned>(this->report_offset_), static_cast<unsigned>(burst.length),
             static_cast<unsigned>(shown), text);

    this->report_offset_ += shown;
    if (this->report_offset_ >= burst.length) {
      this->report_burst_++;
      this->report_offset_ = 0;
    }
    return;
  }
}

}  // namespace gree_sniffer
}  // namespace esphome
