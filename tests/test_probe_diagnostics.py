#!/usr/bin/env python3
"""Compile the actual diagnostics translation unit against a fake timer/logger.

This tests reporting with late/disconnected log consumers, not a physical UART
or the ESPHome scheduler implementation. CI additionally compiles a real D1 image.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "components/gree_uart/probe_diagnostics.cpp"
MOCK_HEADER = r'''
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#define GREE_RX_BUFFER_SIZE 52
inline bool logger_open = false;
inline std::vector<std::string> output;
template<typename... Args> void test_log(const char *, const char *fmt, Args... args) {
  if (!logger_open) return;
  char text[512];
  std::snprintf(text, sizeof(text), fmt, args...);
  output.emplace_back(text);
}
#define ESP_LOGI test_log
#define ESP_LOGW test_log
namespace esphome { namespace gree_uart {
class GreeClimate {
 public:
  void setup();
  void log_probe_result();
  void set_interval(const char *name, uint32_t ms, std::function<void()> cb) {
    timer_name = name; interval_ms = ms; timer = cb; ++timer_registrations;
  }
  std::string timer_name;
  uint32_t interval_ms = 0;
  unsigned timer_registrations = 0;
  std::function<void()> timer;
  bool startup_probe_done_ = false;
  uint8_t startup_probe_step_ = 0;
  uint8_t startup_capture_count_ = 0;
  uint8_t startup_capture_size_[8]{};
  uint8_t startup_capture_[8][GREE_RX_BUFFER_SIZE]{};
  uint8_t startup_report_frame_index_ = 0;
};
}}
'''
HARNESS = r'''
#include "gree.h"
#include <cassert>
using esphome::gree_uart::GreeClimate;
static bool has(const char *s) {
  for (const auto &line : output) if (line.find(s) != std::string::npos) return true;
  return false;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  GreeClimate c;
  c.setup();
  assert(c.timer_registrations == 1);
  assert(c.timer_name == "gree-probe-report");
  assert(c.interval_ms == 10000);
  if (scenario == "late") {
    c.startup_probe_done_ = true; c.startup_probe_step_ = 7;
    for (int i = 0; i < 12; i++) c.timer(); // logger closed for two minutes
    assert(output.empty());
    logger_open = true;
    c.timer();
    assert(has("[probe-diag-v2] PROBE_RESULT state=DONE sent=7/7 captured_frames=0"));
    assert(output.size() == 1);
  } else if (scenario == "reconnect") {
    c.startup_probe_done_ = true; c.startup_probe_step_ = 7;
    logger_open = true; c.timer(); assert(output.size() == 1);
    logger_open = false; output.clear();
    for (int i = 0; i < 60; i++) c.timer();
    assert(output.empty());
    logger_open = true; c.timer(); assert(has("state=DONE"));
    assert(c.startup_probe_step_ == 7 && c.startup_probe_done_);
  } else if (scenario == "pending") {
    logger_open = true; c.startup_probe_step_ = 3; c.timer();
    assert(has("state=RUNNING sent=3/7"));
  } else if (scenario == "rotate") {
    c.startup_probe_done_ = true; c.startup_probe_step_ = 7;
    c.startup_capture_count_ = 8;
    for (unsigned i = 0; i < 8; i++) {
      c.startup_capture_size_[i] = GREE_RX_BUFFER_SIZE;
      for (unsigned j = 0; j < GREE_RX_BUFFER_SIZE; j++) c.startup_capture_[i][j] = i;
    }
    logger_open = true;
    for (unsigned i = 0; i < 16; i++) {
      output.clear(); c.timer();
      assert(output.size() == 2); // bounded log burst
      assert(has(("CAPTURED_RX " + std::to_string(i % 8 + 1) + "/8 len=52").c_str()));
      assert(output[1].size() < 256);
    }
    assert(c.startup_capture_count_ == 8 && c.startup_capture_[7][51] == 7);
  } else if (scenario == "raw") {
    c.startup_probe_done_ = true; c.startup_probe_step_ = 7;
    c.startup_capture_count_ = 1; c.startup_capture_size_[0] = 6;
    const uint8_t frame[] = {0x7E, 0x7E, 0x03, 0x32, 0x00, 0x35};
    for (unsigned i = 0; i < sizeof(frame); i++) c.startup_capture_[0][i] = frame[i];
    logger_open = true; c.log_probe_result();
    assert(has("7E 7E 03 32 00 35"));
  } else if (scenario == "invalid_size") {
    c.startup_capture_count_ = 1; c.startup_capture_size_[0] = 255;
    logger_open = true; c.timer(); assert(has("Invalid stored frame size: 255"));
  } else if (scenario == "invalid_count") {
    c.startup_capture_count_ = 9;
    logger_open = true; c.timer(); assert(has("Invalid capture count"));
  } else if (scenario == "instances") {
    GreeClimate other; other.setup();
    logger_open = true; c.startup_probe_done_ = true; c.startup_probe_step_ = 7;
    c.timer(); assert(has("state=DONE"));
    output.clear(); other.timer(); assert(has("state=RUNNING sent=0/7"));
  } else return 2;
  assert(c.timer_registrations == 1); // reporting cannot create extra timers
  return 0;
}
'''

class DiagnosticsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        path = Path(cls.work.name)
        (path / "gree.h").write_text(MOCK_HEADER)
        shutil.copyfile(SOURCE, path / "probe_diagnostics.cpp")
        (path / "test.cpp").write_text(HARNESS)
        cls.binary = path / "test"
        subprocess.run([
            os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie", "-g",
            str(path / "probe_diagnostics.cpp"), str(path / "test.cpp"), "-o", str(cls.binary),
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_scenario(self, name):
        subprocess.run([str(self.binary), name], check=True, timeout=10)

    def test_late_logger(self): self.run_scenario("late")
    def test_reconnected_logger(self): self.run_scenario("reconnect")
    def test_pending_probe(self): self.run_scenario("pending")
    def test_bounded_replay_and_rotation(self): self.run_scenario("rotate")
    def test_raw_frame_preserved(self): self.run_scenario("raw")
    def test_invalid_size(self): self.run_scenario("invalid_size")
    def test_invalid_count(self): self.run_scenario("invalid_count")
    def test_instances_are_independent(self): self.run_scenario("instances")

if __name__ == "__main__":
    unittest.main(verbosity=2)
