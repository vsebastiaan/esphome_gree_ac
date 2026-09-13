#!/usr/bin/env python3
"""Native regression tests for persistent dialect-probe diagnostics.

The physical UART state machine is covered by the real ESP8266 compile in CI.
These tests focus on late log viewers, reconnects, raw byte preservation and
bounded rotation through responding probe steps.
"""
from pathlib import Path
import os, shutil, subprocess, tempfile, unittest

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
#define HEX 16
inline bool logger_open = false;
inline std::vector<std::string> output;
template<typename... Args> void test_log(const char *, const char *fmt, Args... args) {
  if (!logger_open) return;
  char text[768];
  std::snprintf(text, sizeof(text), fmt, args...);
  output.emplace_back(text);
}
#define ESP_LOGI test_log
#define ESP_LOGW test_log
namespace esphome { namespace gree_uart {
class GreeClimate {
 public:
  static constexpr uint8_t DIALECT_PROBE_TOTAL_STEPS = 12;
  static constexpr uint16_t DIALECT_PROBE_RX_MAX = 512;
  void setup();
  void log_probe_result();
  void log_dialect_probe_result();
  void set_interval(const char *name, uint32_t ms, std::function<void()> cb) {
    timer_name = name; interval_ms = ms; timer = cb; ++timer_registrations;
  }
  const char *dialect_probe_label_(uint8_t step) const {
    static const char *labels[DIALECT_PROBE_TOTAL_STEPS] = {
      "step0", "step1", "step2", "step3", "step4", "step5",
      "step6", "step7", "step8", "step9", "step10", "step11"};
    return step < DIALECT_PROBE_TOTAL_STEPS ? labels[step] : "unknown";
  }
  std::string timer_name;
  uint32_t interval_ms = 0;
  unsigned timer_registrations = 0;
  std::function<void()> timer;
  bool dialect_probe_active_ = true;
  bool dialect_probe_found_rx_ = false;
  uint8_t dialect_probe_sent_ = 0;
  uint8_t dialect_probe_first_hit_step_ = 0xFF;
  uint8_t dialect_probe_report_step_cursor_ = 0;
  uint16_t dialect_probe_rx_total_ = 0;
  uint16_t dialect_probe_rx_stored_ = 0;
  uint8_t dialect_probe_rx_[DIALECT_PROBE_RX_MAX]{};
  uint16_t dialect_probe_step_rx_[DIALECT_PROBE_TOTAL_STEPS]{};
  uint16_t dialect_probe_step_offset_[DIALECT_PROBE_TOTAL_STEPS]{};
};
}}
'''
HARNESS = r'''
#include "gree.h"
#include <cassert>
using esphome::gree_uart::GreeClimate;
static bool has(const char *s) { for (const auto &x : output) if (x.find(s) != std::string::npos) return true; return false; }
static void hit(GreeClimate &c, uint8_t step, const uint8_t *b, uint16_t n) {
  uint16_t off = c.dialect_probe_rx_stored_; assert(off + n <= GreeClimate::DIALECT_PROBE_RX_MAX);
  c.dialect_probe_step_offset_[step] = off; c.dialect_probe_step_rx_[step] = n;
  for (uint16_t i=0;i<n;i++) c.dialect_probe_rx_[off+i]=b[i];
  c.dialect_probe_rx_stored_ += n; c.dialect_probe_rx_total_ += n;
  if (c.dialect_probe_first_hit_step_ == 0xFF) c.dialect_probe_first_hit_step_ = step;
}
int main(int argc, char **argv) {
  assert(argc==2); std::string sc=argv[1]; GreeClimate c;
  for(uint8_t i=0;i<GreeClimate::DIALECT_PROBE_TOTAL_STEPS;i++) c.dialect_probe_step_offset_[i]=0xFFFF;
  c.setup(); assert(c.timer_registrations==1 && c.timer_name=="gree-probe-report" && c.interval_ms==10000);
  if(sc=="late") {
    c.dialect_probe_active_=false; c.dialect_probe_sent_=12; for(int i=0;i<12;i++) c.timer(); assert(output.empty());
    logger_open=true; c.timer(); assert(has("[dialect-probe-v3] DIALECT_RESULT state=NO_RX sent=12/12 rx_total=0 stored=0 first_hit=none"));
  } else if(sc=="running") {
    logger_open=true; c.dialect_probe_sent_=5; c.timer(); assert(has("state=RUNNING sent=5/12"));
  } else if(sc=="found") {
    c.dialect_probe_active_=false; c.dialect_probe_found_rx_=true; c.dialect_probe_sent_=9;
    const uint8_t f[]={0x7E,0x7E,0x03,0x32,0x00,0x35}; hit(c,8,f,sizeof(f)); logger_open=true; c.timer();
    assert(has("state=FOUND_RX sent=9/12 rx_total=6 stored=6 first_hit=step8"));
    assert(has("DIALECT_RX step=9 step8 bytes=6 shown=6: 7E 7E 03 32 00 35")); assert(output.size()==2);
  } else if(sc=="rotate") {
    c.dialect_probe_active_=false; c.dialect_probe_found_rx_=true; c.dialect_probe_sent_=12;
    const uint8_t a[]={0x7E,0x7E,0x03,0x01}, b[]={0x7E,0x7E,0x2F,0x31,0xAA}; hit(c,0,a,sizeof(a)); hit(c,10,b,sizeof(b)); logger_open=true;
    output.clear(); c.timer(); assert(has("DIALECT_RX step=1 step0")); output.clear(); c.timer(); assert(has("DIALECT_RX step=11 step10")); output.clear(); c.timer(); assert(has("DIALECT_RX step=1 step0"));
  } else if(sc=="reconnect") {
    c.dialect_probe_active_=false; c.dialect_probe_found_rx_=true; c.dialect_probe_sent_=8; const uint8_t f[]={0x7E,0x7E,0x31}; hit(c,7,f,sizeof(f));
    logger_open=true; c.timer(); assert(has("FOUND_RX")); logger_open=false; output.clear(); for(int i=0;i<60;i++) c.timer(); assert(output.empty()); logger_open=true; c.timer(); assert(has("DIALECT_RX"));
  } else if(sc=="bounded") {
    c.dialect_probe_active_=false; c.dialect_probe_found_rx_=true; c.dialect_probe_sent_=12; uint8_t b[120]; for(unsigned i=0;i<sizeof(b);i++) b[i]=i; hit(c,11,b,sizeof(b));
    logger_open=true; c.timer(); assert(has("bytes=120 shown=96")); assert(output.size()==2 && output[1].size()<420);
  } else if(sc=="bad_offset") {
    c.dialect_probe_active_=false; c.dialect_probe_found_rx_=true; c.dialect_probe_sent_=12; c.dialect_probe_rx_total_=5; c.dialect_probe_rx_stored_=5; c.dialect_probe_first_hit_step_=3; c.dialect_probe_step_rx_[3]=5; c.dialect_probe_step_offset_[3]=600;
    logger_open=true; c.timer(); assert(has("FOUND_RX") && output.size()==1);
  } else return 2;
  assert(c.timer_registrations==1); return 0;
}
'''

class DiagnosticsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work=tempfile.TemporaryDirectory(); p=Path(cls.work.name)
        (p/"gree.h").write_text(MOCK_HEADER); shutil.copyfile(SOURCE,p/"probe_diagnostics.cpp"); (p/"test.cpp").write_text(HARNESS); cls.binary=p/"test"
        subprocess.run([os.environ.get("CXX","g++"),"-std=c++17","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer","-no-pie","-g",str(p/"probe_diagnostics.cpp"),str(p/"test.cpp"),"-o",str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.work.cleanup()
    def run_scenario(self,name): subprocess.run([str(self.binary),name],check=True,timeout=10)
    def test_late_logger(self): self.run_scenario("late")
    def test_running_probe(self): self.run_scenario("running")
    def test_found_rx_and_raw_bytes(self): self.run_scenario("found")
    def test_rotation(self): self.run_scenario("rotate")
    def test_reconnected_logger(self): self.run_scenario("reconnect")
    def test_bounded_output(self): self.run_scenario("bounded")
    def test_invalid_offset_is_safe(self): self.run_scenario("bad_offset")

if __name__=="__main__": unittest.main(verbosity=2)
