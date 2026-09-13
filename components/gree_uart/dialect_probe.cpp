#include "gree.h"
#include "esphome/core/helpers.h"

#include <cstring>

namespace esphome {
namespace gree_uart {

static const char *const DIALECT_TAG = "gree.dialect";

// Documented stock-module frames from bekmansurov/gree-hvac-protocol.
static const uint8_t PROBE_STARTUP_10[] = {
    0x7E, 0x7E, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x28, 0x1E, 0x19, 0x23, 0x23, 0x00, 0xB8};
static const uint8_t PROBE_SHORT_04[] = {0x7E, 0x7E, 0x05, 0x04, 0x07, 0x00, 0x00, 0x10};
static const uint8_t PROBE_0E_A[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12};
static const uint8_t PROBE_0E_B[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x90};
static const uint8_t PROBE_0E_NO_DHCP[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x8F};
static const uint8_t PROBE_0E_CONNECTED[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x7E, 0x0F};

static constexpr uint32_t PROBE_INITIAL_DELAY_MS = 150;
static constexpr uint32_t PROBE_RESPONSE_WINDOW_MS = 800;
static constexpr uint32_t PROBE_INTER_STEP_MS = 120;
static constexpr uint8_t PROBE_STANDARD_LAST_STEP = 7;

const char *GreeClimate::dialect_probe_label_(uint8_t step) const {
  switch (step) {
    case 0: return "BKM len10/type02 startup";
    case 1: return "BKM len05/type04 short #1";
    case 2: return "BKM len05/type04 short #2";
    case 3: return "BKM len0E/type03 startup-A";
    case 4: return "BKM len0E/type03 startup-B";
    case 5: return "BKM len0E/type03 no-DHCP";
    case 6: return "BKM len0E/type03 connected";
    case 7: return "BKM len2C/type01 passive";
    case 8: return "GKH len0D/type04 MAC";
    case 9: return "GKH len0D/type04 MAC reversed";
    case 10: return "BKM len2C passive wifi-tail";
    case 11: return "GKH len2F/type01 passive";
    default: return "unknown";
  }
}

void GreeClimate::capture_dialect_probe_rx_() {
  while (this->available() > 0) {
    uint8_t value = 0;
    if (!this->read_byte(&value))
      break;

    this->dialect_probe_rx_total_++;
    if (this->dialect_probe_last_sent_step_ < DIALECT_PROBE_TOTAL_STEPS) {
      const uint8_t step = this->dialect_probe_last_sent_step_;
      if (this->dialect_probe_step_rx_[step] == 0)
        this->dialect_probe_step_offset_[step] = this->dialect_probe_rx_stored_;
      this->dialect_probe_step_rx_[step]++;
      if (this->dialect_probe_first_hit_step_ == 0xFF)
        this->dialect_probe_first_hit_step_ = step;
    }

    if (this->dialect_probe_rx_stored_ < DIALECT_PROBE_RX_MAX)
      this->dialect_probe_rx_[this->dialect_probe_rx_stored_++] = value;
  }
}

void GreeClimate::finish_dialect_probe_(bool found_rx) {
  this->dialect_probe_found_rx_ = found_rx;
  this->dialect_probe_done_ = true;
  this->dialect_probe_active_ = false;
  this->dialect_probe_waiting_ = false;

  // Do not run the older seven-step startup probe afterwards: this sweep already
  // contains that documented sequence plus the alternative CS532AE-style frames.
  this->startup_probe_done_ = true;
  this->startup_capture_open_ = false;

  ESP_LOGI(DIALECT_TAG,
           "DIALECT PROBE COMPLETE state=%s sent=%u/%u rx_total=%u stored=%u first_hit=%s",
           found_rx ? "FOUND_RX" : "NO_RX", static_cast<unsigned>(this->dialect_probe_sent_),
           static_cast<unsigned>(DIALECT_PROBE_TOTAL_STEPS), static_cast<unsigned>(this->dialect_probe_rx_total_),
           static_cast<unsigned>(this->dialect_probe_rx_stored_),
           this->dialect_probe_first_hit_step_ == 0xFF ? "none" : this->dialect_probe_label_(this->dialect_probe_first_hit_step_));
}

void GreeClimate::run_dialect_probe_() {
  if (!this->dialect_probe_active_)
    return;

  const uint32_t now = millis();
  if (this->dialect_probe_next_ms_ == 0) {
    for (uint8_t i = 0; i < DIALECT_PROBE_TOTAL_STEPS; i++)
      this->dialect_probe_step_offset_[i] = 0xFFFF;
    this->dialect_probe_next_ms_ = now + PROBE_INITIAL_DELAY_MS;
    ESP_LOGI(DIALECT_TAG, "Dialect probe armed: 4800 8E1, %u documented/passive variants",
             static_cast<unsigned>(DIALECT_PROBE_TOTAL_STEPS));
    return;
  }

  if (this->dialect_probe_waiting_) {
    if (static_cast<int32_t>(now - this->dialect_probe_deadline_ms_) < 0)
      return;

    // Steps 0..7 deliberately preserve Bekmansurov's normal startup order.
    // If that sequence produced RX, finish after its normal 0x2C poll so we do
    // not disturb a successful handshake with fallback dialects.
    if (this->dialect_probe_last_sent_step_ == PROBE_STANDARD_LAST_STEP && this->dialect_probe_rx_total_ > 0) {
      this->finish_dialect_probe_(true);
      return;
    }

    // For fallback probes (8..11), any RX is enough evidence to stop safely.
    if (this->dialect_probe_last_sent_step_ > PROBE_STANDARD_LAST_STEP &&
        this->dialect_probe_last_sent_step_ < DIALECT_PROBE_TOTAL_STEPS &&
        this->dialect_probe_step_rx_[this->dialect_probe_last_sent_step_] > 0) {
      this->finish_dialect_probe_(true);
      return;
    }

    this->dialect_probe_waiting_ = false;
    this->dialect_probe_next_ms_ = now + PROBE_INTER_STEP_MS;
    return;
  }

  if (static_cast<int32_t>(now - this->dialect_probe_next_ms_) < 0)
    return;

  if (this->dialect_probe_step_ >= DIALECT_PROBE_TOTAL_STEPS) {
    this->finish_dialect_probe_(this->dialect_probe_rx_total_ > 0);
    return;
  }

  const uint8_t step = this->dialect_probe_step_++;
  this->send_dialect_probe_step_(step);
  this->dialect_probe_last_sent_step_ = step;
  this->dialect_probe_sent_ = this->dialect_probe_step_;
  this->dialect_probe_waiting_ = true;
  this->dialect_probe_deadline_ms_ = now + PROBE_RESPONSE_WINDOW_MS;
}

void GreeClimate::send_dialect_probe_step_(uint8_t step) {
  const uint8_t *message = nullptr;
  uint8_t size = 0;
  uint8_t dynamic[64]{};

  switch (step) {
    case 0:
      message = PROBE_STARTUP_10; size = sizeof(PROBE_STARTUP_10); break;
    case 1:
    case 2:
      message = PROBE_SHORT_04; size = sizeof(PROBE_SHORT_04); break;
    case 3:
      message = PROBE_0E_A; size = sizeof(PROBE_0E_A); break;
    case 4:
      message = PROBE_0E_B; size = sizeof(PROBE_0E_B); break;
    case 5:
      message = PROBE_0E_NO_DHCP; size = sizeof(PROBE_0E_NO_DHCP); break;
    case 6:
      message = PROBE_0E_CONNECTED; size = sizeof(PROBE_0E_CONNECTED); break;
    case 7: {
      memcpy(dynamic, this->data_write_, sizeof(this->data_write_));
      dynamic[7] = 0x00;  // passive/read request, never AF
      dynamic[46] = this->get_checksum_(dynamic, 47);
      message = dynamic; size = 47;
      break;
    }
    case 8:
    case 9: {
      // Gekkehenkie documents: 7E 7E 0D 04 04 00 00 00 <MAC6> 00 + checksum.
      // Length 0x0D means command+11 payload bytes+checksum = 13 bytes after LEN.
      dynamic[0] = 0x7E; dynamic[1] = 0x7E; dynamic[2] = 0x0D; dynamic[3] = 0x04;
      dynamic[4] = 0x04; dynamic[5] = 0x00; dynamic[6] = 0x00; dynamic[7] = 0x00;
      uint8_t mac[6]{};
      get_mac_address_raw(mac);
      for (uint8_t i = 0; i < 6; i++)
        dynamic[8 + i] = step == 8 ? mac[i] : mac[5 - i];
      dynamic[14] = 0x00;
      dynamic[15] = this->get_checksum_(dynamic, 16);
      message = dynamic; size = 16;
      break;
    }
    case 10: {
      memcpy(dynamic, this->data_write_, sizeof(this->data_write_));
      dynamic[7] = 0x00;  // passive
      // Values documented by Bekmansurov as seen from a connected stock module.
      dynamic[41] = 0x0C;
      dynamic[43] = 0x02;
      dynamic[46] = this->get_checksum_(dynamic, 47);
      message = dynamic; size = 47;
      break;
    }
    case 11: {
      // Piotrva/Gekkehenkie variant: LEN=0x2F, TYPE=0x01, 45-byte payload.
      // Only documented constant/no-change bits are set, so this is a passive poll.
      dynamic[0] = 0x7E; dynamic[1] = 0x7E; dynamic[2] = 0x2F; dynamic[3] = 0x01;
      dynamic[4 + 7] = 0x02;
      dynamic[4 + 11] = 0x08;
      dynamic[4 + 39] = 0x02;
      dynamic[49] = this->get_checksum_(dynamic, 50);
      message = dynamic; size = 50;
      break;
    }
    default:
      return;
  }

  ESP_LOGI(DIALECT_TAG, "TX probe %u/%u: %s", static_cast<unsigned>(step + 1),
           static_cast<unsigned>(DIALECT_PROBE_TOTAL_STEPS), this->dialect_probe_label_(step));
  this->write_array(message, size);
  this->dump_message_("Dialect TX", message, size);
}

}  // namespace gree_uart
}  // namespace esphome
