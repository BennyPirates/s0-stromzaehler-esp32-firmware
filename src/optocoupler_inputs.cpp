#include "optocoupler_inputs.h"

#include <Arduino.h>

void OptocouplerInputs::begin() {
  Serial.println("[INPUTS] Optocoupler inputs disabled (reserved for next milestone)");
}

void OptocouplerInputs::update() {
  // Intentionally empty. Do not connect this firmware to the meter yet.
}
