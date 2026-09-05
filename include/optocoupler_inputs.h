#pragma once

#include <Arduino.h>
#include <Preferences.h>

// Hardware verified for the diagnostic firmware:
//   PC817 U1 -> GPIO25, U2 -> GPIO33, U3 -> GPIO27.
// The HY-M154 outputs are wired as active-low inputs with the ESP32 internal
// pull-ups enabled. Change kActiveLow only after verifying a wiring change.
struct S0InputSnapshot {
  const char* name;
  uint8_t gpio;
  bool rawLevelHigh;
  bool active;
  uint32_t risingEdges;
  uint32_t fallingEdges;
  uint32_t pulses;
  uint64_t meterEnergyWh;
  uint32_t lastPulseAgeMs;
  uint32_t lastPulseIntervalMs;
  uint32_t estimatedPowerW;
  bool hasPulse;
  bool hasPowerEstimate;
};

class OptocouplerInputs {
 public:
  void begin();
  void update();
  S0InputSnapshot snapshot(uint8_t channel) const;
  bool setMeterEnergyWh(uint8_t channel, uint64_t energyWh);

  static constexpr uint8_t kChannelCount = 3;
  static constexpr bool kActiveLow = true;
  static constexpr uint32_t kDebounceUs = 10000;

 private:
  struct ChannelState {
    const char* name;
    uint8_t gpio;
    volatile bool rawLevelHigh;
    volatile uint32_t risingEdges;
    volatile uint32_t fallingEdges;
    volatile uint32_t pulses;
    volatile uint64_t meterEnergyWh;
    volatile uint32_t lastAcceptedEdgeUs;
    volatile uint32_t lastPulseUs;
    volatile uint32_t lastPulseIntervalUs;
  };

  static void IRAM_ATTR handleInterrupt(uint8_t channel);
  static void IRAM_ATTR isr0();
  static void IRAM_ATTR isr1();
  static void IRAM_ATTR isr2();

  static OptocouplerInputs* instance_;
  mutable portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
  ChannelState channels_[kChannelCount] = {
      {"Wärmepumpe", 25, true, 0, 0, 0, 0, 0, 0, 0},
      {"Ferienwohnung", 33, true, 0, 0, 0, 0, 0, 0, 0},
      {"Hauptwohnung", 27, true, 0, 0, 0, 0, 0, 0, 0},
  };
  uint32_t lastReportedPulses_[kChannelCount] = {};
  uint32_t lastReportedRisingEdges_[kChannelCount] = {};
  uint32_t lastReportedFallingEdges_[kChannelCount] = {};
  Preferences meterStorage_;
  bool meterStorageAvailable_ = false;
  uint64_t lastPersistedEnergyWh_[kChannelCount] = {};
  uint32_t lastPersistenceMs_ = 0;

  void loadMeterReadings();
  void persistMeterReadings(bool force = false);
};
