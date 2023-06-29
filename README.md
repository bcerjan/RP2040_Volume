# RP2040_Volume
Adds support for volume-controllable tones from the RP2040 for simple PWM audio.

Improves upon the default Arduino `tone()` function in several ways

- Allows for volume control via ultrasonic pwm at 62.5 kHz
- Works for either single-pin or differential pin pairs
- Non-blocking -- all tones are generated using hardware alarms and functions and are accurate to ~1 us


See the example `main.cpp` for how to use to library.
