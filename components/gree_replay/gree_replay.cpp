#include "gree_replay.h"

#include <cstdio>

#include "esphome/core/log.h"

namespace esphome {
namespace gree_replay {

static const char *const TAG = "gree_replay";
static const char *const VERSION = "stock-replay-v1";

// Exact 50-byte frame captured from the original CS532AE on the BLACK wire
// (module TX -> AC RX). LEN=0x2F, TYPE=0x01, checksum=0xB4.
static const uint8_t STOCK_REQUEST[] = {
    0x7E, 0x7E, 0x2F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x60,
    0x02, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB4};

void GreeReplay::setup() {
  const uint32_t now = millis();
  this->next_tx_ms_ = now + INITIAL_TX_DELAY_MS;
  this->next_summary_ms_ = now + SUMMARY_INTERVAL_MS;

  ESP_LOGI(TAG, "[%s] Exact stock-frame replay test starting", VERSION);
  ESP_LOGI(TAG, "[%s] Expected wiring: GPIO1/TX -> BLACK; ORANGE -> divider -> GPIO3/RX", VERSION);
  ESP_LOGI(TAG, "[%s] Replaying captured 2F/01 every %ums; looking for 2F/31 responses", VERSION,
           static_cast<unsigned>(this->interval_ms_));
  this->log_frame_("STOCK_TX_TEMPLATE", STOCK_REQUEST, sizeof(STOCK_REQUEST));
}

void GreeReplay::dump_config() {
  ESP_LOGCONFIG(TAG, "Gree exact stock-frame replay:");
  ESP_LOGCONFIG(TAG, "  Version: %s", VERSION);
  ESP_LOGCONFIG(TAG, "  Interval: %ums", static_cast<unsigned>(this->interval_ms_));
  this->check_uart_settings(4800, 1, uart::UART_CONFIG_PARITY_EVEN, 8);
}

void GreeReplay::loop() {
  while (this->available() > 0) {
    uint8_t value = 0;
    if (!this->read_byte(&value))
      break;
    this->rx_byte_count_++;
    this->consume_rx_byte_(value);
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - this->next_tx_ms_) >= 0) {
    this->send_stock_request_();
    this->next_tx_ms_ = now + this->interval_ms_;
  }

  if (static_cast<int32_t>(now - this->next_summary_ms_) >= 0) {
    this->report_summary_();
    this->next_summary_ms_ = now + SUMMARY_INTERVAL_MS;
  }
}

void GreeReplay::send_stock_request_() {
  this->write_array(STOCK_REQUEST, sizeof(STOCK_REQUEST));
  this->tx_count_++;
  if (this->tx_count_ <= 3 || (this->tx_count_ % 10) == 0) {
    ESP_LOGI(TAG, "[%s] TX stock 2F/01 count=%u", VERSION, static_cast<unsigned>(this->tx_count_));
  }
}

void GreeReplay::consume_rx_byte_(uint8_t value) {
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

void GreeReplay::finish_rx_frame_() {
  const uint8_t size = this->rx_expected_;
  this->rx_frame_count_++;

  const uint8_t expected_checksum = this->checksum_(this->rx_buffer_, size);
  const uint8_t actual_checksum = this->rx_buffer_[size - 1];
  const bool checksum_ok = expected_checksum == actual_checksum;
  if (!checksum_ok)
    this->rx_bad_checksum_count_++;

  const uint8_t type = size > 3 ? this->rx_buffer_[3] : 0xFF;
  if (checksum_ok && this->rx_buffer_[2] == 0x2F && type == 0x31)
    this->rx_report_31_count_++;

  if (this->rx_frame_count_ <= 10 || (this->rx_frame_count_ % 10) == 0 || type != 0x31 || !checksum_ok) {
    ESP_LOGI(TAG,
             "[%s] RX frame=%u len=0x%02X type=0x%02X checksum=%s reports31=%u",
             VERSION, static_cast<unsigned>(this->rx_frame_count_), this->rx_buffer_[2], type,
             checksum_ok ? "OK" : "BAD", static_cast<unsigned>(this->rx_report_31_count_));
    this->log_frame_("RX", this->rx_buffer_, size);
  }

  this->reset_rx_parser_();
}

void GreeReplay::reset_rx_parser_() {
  this->rx_pos_ = 0;
  this->rx_expected_ = 0;
}

uint8_t GreeReplay::checksum_(const uint8_t *data, uint8_t size) const {
  if (size < 4)
    return 0;
  uint8_t sum = 0;
  for (uint8_t i = 2; i + 1 < size; i++)
    sum += data[i];
  return sum;
}

void GreeReplay::log_frame_(const char *label, const uint8_t *data, uint8_t size) const {
  static const char hex_digits[] = "0123456789ABCDEF";
  char text[3 * RX_BUFFER_SIZE + 1];
  size_t pos = 0;
  for (uint8_t i = 0; i < size && i < RX_BUFFER_SIZE; i++) {
    if (i != 0)
      text[pos++] = ' ';
    text[pos++] = hex_digits[data[i] >> 4];
    text[pos++] = hex_digits[data[i] & 0x0F];
  }
  text[pos] = '\0';
  ESP_LOGI(TAG, "[%s] %s %s", VERSION, label, text);
}

void GreeReplay::report_summary_() {
  ESP_LOGI(TAG,
           "[%s] SUMMARY tx=%u rx_bytes=%u frames=%u reports_2F31=%u bad_checksum=%u resync=%u",
           VERSION, static_cast<unsigned>(this->tx_count_), static_cast<unsigned>(this->rx_byte_count_),
           static_cast<unsigned>(this->rx_frame_count_), static_cast<unsigned>(this->rx_report_31_count_),
           static_cast<unsigned>(this->rx_bad_checksum_count_), static_cast<unsigned>(this->rx_resync_count_));
}

}  // namespace gree_replay
}  // namespace esphome
