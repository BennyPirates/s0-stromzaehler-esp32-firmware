#pragma once

// Reserved boundary for the next milestone. No GPIOs are configured or read
// until the optocoupler electrical interface and pin mapping are verified.
class OptocouplerInputs {
 public:
  void begin();
  void update();
};
