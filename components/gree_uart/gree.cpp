#include <cmath>
#include "gree.h"
#include "esphome/core/macros.h"

namespace esphome {
namespace gree_uart {

static const char *const TAG = "gree";

// Stock Gree Wi-Fi-module startup frames documented by bekmansurov/gree-hvac-protocol.
// Sent once after boot as a diagnostic probe before normal 0x2C polling.
static const uint8_t STARTUP_10[] = {
    0x7E, 0x7E, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x28, 0x1E, 0x19, 0x23, 0x23, 0x00, 0xB8};
static const uint8_t STARTUP_05[] = {0x7E, 0x7E, 0x05, 0x04, 0x07, 0x00, 0x00, 0x10};
static const uint8_t STARTUP_0E_A[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12};
static const uint8_t STARTUP_0E_B[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x90};
static const uint8_t STARTUP_0E_NO_DHCP[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x8F};
static const uint8_t STARTUP_0E_CONNECTED[] = {
    0x7E, 0x7E, 0x0E, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x7E, 0x0F};

// block of byte positions in requests/answers
// GrKoR: I recommend change this approach (byte positions) to structures like gree_raw_packet_t.
// You can use pointers (and type casting) for assignment structure to the data buffer.
// It will be much much easier to work with structures instead of array and byte positions
static const uint8_t FORCE_UPDATE = 7;
static const uint8_t MODE = 8;
static const uint8_t MODE_MASK = 0b11110000;
static const uint8_t FAN_MASK = 0b00001111;
static const uint8_t SWING = 12;

static const uint8_t CRC_WRITE = 46;
//CRC_READ moved to read_state_ with last bytes because of different length of incoming packets
//static const uint8_t CRC_READ = 49;

static const uint8_t TEMPERATURE = 9;
static const uint8_t INDOOR_TEMPERATURE = 46;

// component settings
static const uint8_t MIN_VALID_TEMPERATURE = 16;
static const uint8_t MAX_VALID_TEMPERATURE = 30;
static const uint8_t TEMPERATURE_STEP = 1;

// prints user configuration
void GreeClimate::dump_config() {
  ESP_LOGCONFIG(TAG, "Gree:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u", this->get_update_interval());
  this->dump_traits_(TAG);
  this->check_uart_settings(4800, 1, uart::UART_CONFIG_PARITY_EVEN, 8);
}

void GreeClimate::loop() {
  this->run_startup_probe_();

  gree_raw_packet_t *raw_packet = (gree_raw_packet_t *)this->data_read_;

  while (!receiving_packet_ && this->available() >= sizeof(gree_header_t)) {
    if (this->peek() != GREE_START_BYTE) {
      this->read(); // читаем байт "в никуда"
      continue;
    }

    this->read_array(this->data_read_, sizeof(gree_start_bytes_t));
    receiving_packet_ = (raw_packet->header.start_bytes.u8x2[1] == GREE_START_BYTE);
    
    if (receiving_packet_) {
      this->read_byte( &raw_packet->header.data_length );

      if (raw_packet->header.data_length + sizeof(gree_header_t) > GREE_RX_BUFFER_SIZE) {
        ESP_LOGE(TAG, "Incoming packet is too big! header.data_length = %d, maximum is %d", raw_packet->header.data_length, GREE_RX_BUFFER_SIZE - sizeof(gree_header_t));
        receiving_packet_ = false;
        memset(this->data_read_, 0, GREE_RX_BUFFER_SIZE);
      }
    }
  }

  if (receiving_packet_ && this->available() >= raw_packet->header.data_length) {
    this->read_array(raw_packet->data, raw_packet->header.data_length);

    dump_message_("Read array", this->data_read_, raw_packet->header.data_length + sizeof(gree_header_t));
    read_state_(this->data_read_, raw_packet->header.data_length + sizeof(gree_header_t));
    
    receiving_packet_ = false;
    memset(this->data_read_, 0, GREE_RX_BUFFER_SIZE);
  }
}

/*
void GreeClimate::setup() {
  this->set_update_interval(300);
}
*/

void GreeClimate::run_startup_probe_() {
  if (this->startup_probe_done_) return;
  const uint32_t now = millis();
  if (this->startup_probe_next_ms_ == 0) {
    this->startup_probe_next_ms_ = now + 150;
    ESP_LOGI(TAG, "Startup probe armed; normal 0x2C polling paused");
    return;
  }
  if (static_cast<int32_t>(now - this->startup_probe_next_ms_) < 0) return;

  const uint8_t *message = nullptr;
  uint8_t size = 0;
  const char *label = nullptr;
  switch (this->startup_probe_step_) {
    case 0: message = STARTUP_10; size = sizeof(STARTUP_10); label = "0x10"; break;
    case 1:
    case 2: message = STARTUP_05; size = sizeof(STARTUP_05); label = "0x05"; break;
    case 3: message = STARTUP_0E_A; size = sizeof(STARTUP_0E_A); label = "0x0E startup A"; break;
    case 4: message = STARTUP_0E_B; size = sizeof(STARTUP_0E_B); label = "0x0E startup B"; break;
    case 5: message = STARTUP_0E_NO_DHCP; size = sizeof(STARTUP_0E_NO_DHCP); label = "0x0E no-DHCP"; break;
    case 6: message = STARTUP_0E_CONNECTED; size = sizeof(STARTUP_0E_CONNECTED); label = "0x0E connected"; break;
    default:
      this->startup_probe_done_ = true;
      ESP_LOGI(TAG, "Startup probe complete; enabling normal 0x2C polling");
      return;
  }
  ESP_LOGI(TAG, "Startup probe %u/7: %s", this->startup_probe_step_ + 1, label);
  this->send_data_(message, size);
  this->startup_probe_step_++;
  this->startup_probe_next_ms_ = now + (this->startup_probe_step_ >= 7 ? 500 : 300);
}

void GreeClimate::update() {
  if (!this->startup_probe_done_) return;
  data_write_[CRC_WRITE] = get_checksum_(data_write_, sizeof(data_write_));
  send_data_(data_write_, sizeof(data_write_));
}

climate::ClimateTraits GreeClimate::traits() {
  auto traits = climate::ClimateTraits();

  traits.set_visual_min_temperature(MIN_VALID_TEMPERATURE);
  traits.set_visual_max_temperature(MAX_VALID_TEMPERATURE);
  traits.set_visual_temperature_step(TEMPERATURE_STEP);

  traits.set_supported_modes({
    climate::CLIMATE_MODE_OFF,
    climate::CLIMATE_MODE_AUTO,
    climate::CLIMATE_MODE_COOL,
    climate::CLIMATE_MODE_DRY,
    climate::CLIMATE_MODE_FAN_ONLY,
    climate::CLIMATE_MODE_HEAT
  });

  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH
  });

  // traits.set_supported_swing_modes(this->supported_swing_modes_);
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);

  traits.set_supported_presets(this->supported_presets_);

  traits.add_supported_preset(climate::CLIMATE_PRESET_NONE);
  traits.add_supported_preset(climate::CLIMATE_PRESET_BOOST);
  // traits.add_supported_preset(climate::CLIMATE_PRESET_SLEEP);

  return traits;
}

void GreeClimate::read_state_(const uint8_t *data, uint8_t size) {
  // get checksum byte from received data (using the last byte)
  uint8_t data_crc = data[size-1];
  // get checksum byte based on received data (calculating)
  uint8_t get_crc = get_checksum_(data, size);

  if (data_crc != get_crc) {
    ESP_LOGW(TAG, "Invalid checksum.");
    return;
  }

// now we are using only packets with 0x31 as first data byte
  if (data[3] != 49) {
    ESP_LOGW(TAG, "Invalid packet type.");
    return;
  }

  this->target_temperature = data[TEMPERATURE] / 16 + MIN_VALID_TEMPERATURE;
  this->current_temperature = data[INDOOR_TEMPERATURE] - 40; // check later?

  // partially saving current state to previous request
  data_write_[MODE] = data[MODE];
  // add target temperature state too? ok
  data_write_[TEMPERATURE] = data[TEMPERATURE];

  // update CLIMATE state according AC response
  switch (data[MODE] & MODE_MASK) {
    case AC_MODE_OFF:
      this->mode = climate::CLIMATE_MODE_OFF;
      break;
    case AC_MODE_AUTO:
      this->mode = climate::CLIMATE_MODE_AUTO;
      break;
    case AC_MODE_COOL:
      this->mode = climate::CLIMATE_MODE_COOL;
      break;
    case AC_MODE_DRY:
      this->mode = climate::CLIMATE_MODE_DRY;
      break;
    case AC_MODE_FANONLY:
      this->mode = climate::CLIMATE_MODE_FAN_ONLY;
      break;
    case AC_MODE_HEAT:
      this->mode = climate::CLIMATE_MODE_HEAT;
      break;
    default:
      ESP_LOGW(TAG, "Unknown AC MODE&fan: %s", data[MODE]);
  }

  // get current AC FAN SPEED from its response
  switch (data[MODE] & FAN_MASK) {
    case AC_FAN_AUTO:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
    case AC_FAN_LOW:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    case AC_FAN_MEDIUM:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case AC_FAN_HIGH:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    default:
      ESP_LOGW(TAG, "Unknown AC mode&FAN: %s", data[MODE]);
  }

  /*
  switch (data[SWING]) {
    case AC_SWING_OFF:
      this->swing_mode = climate::CLIMATE_SWING_OFF;
      break;

    case AC_SWING_VERTICAL:
      this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
      break;

    case AC_SWING_HORIZONTAL:
      this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
      break;

    case AC_SWING_BOTH:
      this->swing_mode = climate::CLIMATE_SWING_BOTH;
      break;
  }
  */

  switch (data[10]) {
    case 7:
      // when COOL TURBO
      this->preset = climate::CLIMATE_PRESET_BOOST;
      break;
    case 15:
      // when HEAT TURBO
      this->preset = climate::CLIMATE_PRESET_BOOST;
      break;
    default:
      this->preset = climate::CLIMATE_PRESET_NONE;
      break;
  }

  this->publish_state();
}

void GreeClimate::control(const climate::ClimateCall &call) {
  if (!this->startup_probe_done_) {
    ESP_LOGW(TAG, "Ignoring control request while startup probe is active");
    return;
  }
  data_write_[FORCE_UPDATE] = 175;
  // show current temperature on display every time when sending new command. TEST!
  data_write_[13] = 0x20;
  
/*
  // logging of saved mode&fan vars
  char str[250] = {0};
  char *pstr = str;
  for (int i = 0; i < sizeof(data_save_); i++) {
    pstr += sprintf(pstr, "%02X ", data_save_[i]);
  }
  ESP_LOGV(TAG, "SAVED: %s", str);
*/

  // saving mode&fan values from previous 
  uint8_t new_mode = data_write_[MODE] & MODE_MASK;
  uint8_t new_fan_speed = data_write_[MODE] & FAN_MASK;

  if (call.get_mode().has_value()) {
    switch (call.get_mode().value()) {
      case climate::CLIMATE_MODE_OFF:
        new_mode = AC_MODE_OFF;
        break;
      case climate::CLIMATE_MODE_AUTO:
        new_mode = AC_MODE_AUTO;
        break;
      case climate::CLIMATE_MODE_COOL:
        new_mode = AC_MODE_COOL;
        break;
      case climate::CLIMATE_MODE_DRY:
        new_mode = AC_MODE_DRY;
        new_fan_speed = AC_FAN_LOW;
        break;
      case climate::CLIMATE_MODE_FAN_ONLY:
        new_mode = AC_MODE_FANONLY;
        break;
      case climate::CLIMATE_MODE_HEAT:
        new_mode = AC_MODE_HEAT;
        break;
      default:
        ESP_LOGW(TAG, "Setting of unsupported MODE: %s", call.get_mode().value());
        break;
    }
  }

  // set fan speed only if MODE != DRY (only LOW available)
  if (call.get_fan_mode().has_value()) {
    switch (call.get_fan_mode().value()) {
      case climate::CLIMATE_FAN_AUTO:
        new_fan_speed = AC_FAN_AUTO;
        break;
      case climate::CLIMATE_FAN_LOW:
        new_fan_speed = AC_FAN_LOW;
        break;
      case climate::CLIMATE_FAN_MEDIUM:
        new_fan_speed = AC_FAN_MEDIUM;
        break;
      case climate::CLIMATE_FAN_HIGH:
        new_fan_speed = AC_FAN_HIGH;
        break;
      default:
        ESP_LOGW(TAG, "Setting of unsupported FANSPEED: %s", call.get_fan_mode().value());
        break;
    }
  }
  
  // set low speed when DRY mode because other speeds are not available
  if (new_mode == AC_MODE_DRY && new_fan_speed != AC_FAN_LOW) {
    // ESP_LOGW(TAG, "DRY mode exception: %s", new_fan_speed);
    new_fan_speed = AC_FAN_LOW;
  }

  if (call.get_preset().has_value()) {
    switch (call.get_preset().value()) {
      case climate::CLIMATE_PRESET_NONE:
        if (new_mode == AC_MODE_COOL) {
          data_write_[10] = 6;
        } else if (new_mode == AC_MODE_HEAT) {
          data_write_[10] = 14;
        }
        break;
      case climate::CLIMATE_PRESET_BOOST:
        if (new_mode == AC_MODE_COOL) {
          data_write_[10] = 7;
        } else if (new_mode == AC_MODE_HEAT) {
          data_write_[10] = 15;
        }
        // skip preset when not COOL or HEAT mode
        break;
      case climate::CLIMATE_PRESET_SLEEP:
        // something
        break;
      default:
        // something?
        break;
    }
  }

/*
  if (call.get_preset().value() == climate::CLIMATE_PRESET_NONE) {
      data_[POWER] |= COMFORT_PRESET_MASK;
    } else {
      data_[POWER] &= ~COMFORT_PRESET_MASK;
    }
  }
*/

  if (call.get_target_temperature().has_value()) {
    // check if temperature set in valid limits
    if (call.get_target_temperature().value() >= MIN_VALID_TEMPERATURE && call.get_target_temperature().value() <= MAX_VALID_TEMPERATURE)
      data_write_[TEMPERATURE] = (call.get_target_temperature().value() - MIN_VALID_TEMPERATURE) * 16;
  }

  // temporary disabled
  if (call.get_swing_mode().has_value()) {
    switch (call.get_swing_mode().value()) {
      case climate::CLIMATE_SWING_OFF:
        // data_[SWING] = SWING_OFF;
        break;
      case climate::CLIMATE_SWING_VERTICAL:
        // data_[SWING] = SWING_VERTICAL;
        break;
      case climate::CLIMATE_SWING_HORIZONTAL:
        // data_[SWING] = SWING_HORIZONTAL;
        break;
      case climate::CLIMATE_SWING_BOTH:
        // data_[SWING] = SWING_BOTH;
        break;
    }
  }

  data_write_[MODE] = new_mode + new_fan_speed;

  // compute checksum & send data
  data_write_[CRC_WRITE] = get_checksum_(data_write_, sizeof(data_write_));
  send_data_(data_write_, sizeof(data_write_));

  // change of force_update byte to "passive" state
  data_write_[FORCE_UPDATE] = 0;
}

void GreeClimate::send_data_(const uint8_t *message, uint8_t size) {
  this->write_array(message, size);
  dump_message_("Sent message", message, size);
}

void GreeClimate::dump_message_(const char *title, const uint8_t *message, uint8_t size) {
  ESP_LOGV(TAG, "%s:", title);
  char str[250] = {0};
  char *pstr = str;
  if (size * 2 > sizeof(str)) ESP_LOGE(TAG, "too long byte data");
  for (int i = 0; i < size; i++) {
    pstr += sprintf(pstr, "%02X ", message[i]);
  }
  ESP_LOGV(TAG, "%s", str);
}

uint8_t GreeClimate::get_checksum_(const uint8_t *message, size_t size) {
  // position of crc in packet
  uint8_t position = size - 1;
  uint8_t sum = 0;
  // ignore first 2 bytes & last one
  for (int i = 2; i < position; i++)
    sum += message[i];
  uint8_t crc = sum % 256;
  return crc;
}

}  // namespace gree_uart
}  // namespace esphome
