#include "optocoupler_inputs.h"

namespace {
constexpr char kStorageNamespace[] = "s0_meters";
constexpr const char* kEnergyKeys[] = {"meter_1", "meter_2", "meter_3"};
constexpr uint64_t kPersistencePulseInterval = 1000ULL;  // 1 kWh
constexpr uint32_t kPersistenceIntervalMs = 6UL * 60UL * 60UL * 1000UL;
}  // namespace


OptocouplerInputs* OptocouplerInputs::instance_ = nullptr;

void OptocouplerInputs::begin() {
  instance_ = this;
  loadMeterReadings();

  for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
    ChannelState& input = channels_[channel];
    pinMode(input.gpio, INPUT_PULLUP);
    input.rawLevelHigh = digitalRead(input.gpio) == HIGH;
  }

  attachInterrupt(digitalPinToInterrupt(channels_[0].gpio), isr0, CHANGE);
  attachInterrupt(digitalPinToInterrupt(channels_[1].gpio), isr1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(channels_[2].gpio), isr2, CHANGE);

  Serial.printf("[S0] Diagnostic inputs ready: GPIO25/33/27, INPUT_PULLUP, active-%s, debounce=%lu ms\n",
                kActiveLow ? "low" : "high", kDebounceUs / 1000UL);
  for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
    const S0InputSnapshot input = snapshot(channel);
    Serial.printf("[S0] %s: GPIO%u initial=%s\n", input.name, input.gpio,
                  input.active ? "active" : "inactive");
  }
}

void OptocouplerInputs::loadMeterReadings() {
  meterStorageAvailable_ = meterStorage_.begin(kStorageNamespace, false);
  if (!meterStorageAvailable_) {
    Serial.println("[S0] ERROR: persistent meter storage unavailable");
    return;
  }

  for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
    const uint64_t energyWh = meterStorage_.getULong64(kEnergyKeys[channel], 0ULL);
    channels_[channel].meterEnergyWh = energyWh;
    lastPersistedEnergyWh_[channel] = energyWh;
  }
  lastPersistenceMs_ = millis();
  Serial.println("[S0] Restored persistent meter readings");
}

void OptocouplerInputs::persistMeterReadings(bool force) {
  if (!meterStorageAvailable_) {
    return;
  }

  uint64_t energyWh[kChannelCount] = {};
  bool changed = false;
  bool reachedPulseInterval = false;
  portENTER_CRITICAL(&mutex_);
  for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
    energyWh[channel] = channels_[channel].meterEnergyWh;
    changed = changed || energyWh[channel] != lastPersistedEnergyWh_[channel];
    const uint64_t difference = energyWh[channel] >= lastPersistedEnergyWh_[channel]
                                    ? energyWh[channel] - lastPersistedEnergyWh_[channel]
                                    : lastPersistedEnergyWh_[channel] - energyWh[channel];
    reachedPulseInterval = reachedPulseInterval || difference >= kPersistencePulseInterval;
  }
  portEXIT_CRITICAL(&mutex_);

  const bool reachedTimeInterval =
      static_cast<uint32_t>(millis() - lastPersistenceMs_) >= kPersistenceIntervalMs;
  if (!changed || (!force && !reachedPulseInterval && !reachedTimeInterval)) {
    return;
  }

  for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
    if (energyWh[channel] != lastPersistedEnergyWh_[channel]) {
      meterStorage_.putULong64(kEnergyKeys[channel], energyWh[channel]);
      lastPersistedEnergyWh_[channel] = energyWh[channel];
    }
  }
  lastPersistenceMs_ = millis();
  Serial.println("[S0] Persisted meter readings");
}

void OptocouplerInputs::update() {
  persistMeterReadings();
  for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
    const S0InputSnapshot input = snapshot(channel);
    if (input.pulses == lastReportedPulses_[channel] &&
        input.risingEdges == lastReportedRisingEdges_[channel] &&
        input.fallingEdges == lastReportedFallingEdges_[channel]) {
      continue;
    }
    lastReportedPulses_[channel] = input.pulses;
    lastReportedRisingEdges_[channel] = input.risingEdges;
    lastReportedFallingEdges_[channel] = input.fallingEdges;
    Serial.printf("[S0] %s: pulses=%lu, energy=%.3f kWh, power=%lu W, rising=%lu, falling=%lu, "
                  "state=%s\n",
                  input.name, input.pulses, static_cast<double>(input.meterEnergyWh) / 1000.0,
                  input.estimatedPowerW, input.risingEdges, input.fallingEdges,
                  input.active ? "active" : "inactive");
  }
}

S0InputSnapshot OptocouplerInputs::snapshot(uint8_t channel) const {
  S0InputSnapshot result{};
  if (channel >= kChannelCount) {
    return result;
  }

  portENTER_CRITICAL(&mutex_);
  const ChannelState& input = channels_[channel];
  result.name = input.name;
  result.gpio = input.gpio;
  result.rawLevelHigh = input.rawLevelHigh;
  result.risingEdges = input.risingEdges;
  result.fallingEdges = input.fallingEdges;
  result.pulses = input.pulses;
  result.meterEnergyWh = input.meterEnergyWh;
  const uint32_t lastPulseUs = input.lastPulseUs;
  const uint32_t lastPulseIntervalUs = input.lastPulseIntervalUs;
  portEXIT_CRITICAL(&mutex_);

  result.active = kActiveLow ? !result.rawLevelHigh : result.rawLevelHigh;
  result.hasPulse = lastPulseUs != 0;
  result.lastPulseAgeMs = result.hasPulse ? (micros() - lastPulseUs) / 1000UL : 0;
  result.hasPowerEstimate = lastPulseIntervalUs != 0;
  result.lastPulseIntervalMs = result.hasPowerEstimate ? lastPulseIntervalUs / 1000UL : 0;
  result.estimatedPowerW = result.hasPowerEstimate
                               ? static_cast<uint32_t>(3600000000ULL / lastPulseIntervalUs)
                               : 0;
  return result;
}

void IRAM_ATTR OptocouplerInputs::handleInterrupt(uint8_t channel) {
  if (instance_ == nullptr || channel >= kChannelCount) {
    return;
  }

  OptocouplerInputs& inputs = *instance_;
  ChannelState& input = inputs.channels_[channel];
  const bool rawLevelHigh = digitalRead(input.gpio) == HIGH;
  const uint32_t nowUs = micros();

  portENTER_CRITICAL_ISR(&inputs.mutex_);
  if (rawLevelHigh != input.rawLevelHigh) {
    input.rawLevelHigh = rawLevelHigh;
    if (static_cast<uint32_t>(nowUs - input.lastAcceptedEdgeUs) >= kDebounceUs) {
      input.lastAcceptedEdgeUs = nowUs;
      if (rawLevelHigh) {
        ++input.risingEdges;
      } else {
        ++input.fallingEdges;
      }

      const bool active = kActiveLow ? !rawLevelHigh : rawLevelHigh;
      if (active) {
        if (input.lastPulseUs != 0) {
          input.lastPulseIntervalUs = nowUs - input.lastPulseUs;
        }
        ++input.pulses;
        ++input.meterEnergyWh;
        input.lastPulseUs = nowUs;
      }
    }
  }
  portEXIT_CRITICAL_ISR(&inputs.mutex_);
}

void IRAM_ATTR OptocouplerInputs::isr0() { handleInterrupt(0); }

void IRAM_ATTR OptocouplerInputs::isr1() { handleInterrupt(1); }

void IRAM_ATTR OptocouplerInputs::isr2() { handleInterrupt(2); }

bool OptocouplerInputs::setMeterEnergyWh(uint8_t channel, uint64_t energyWh) {
  if (channel >= kChannelCount || !meterStorageAvailable_) {
    return false;
  }

  portENTER_CRITICAL(&mutex_);
  channels_[channel].meterEnergyWh = energyWh;
  portEXIT_CRITICAL(&mutex_);
  persistMeterReadings(true);
  return true;
}
