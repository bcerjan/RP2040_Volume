/* 
This code is heavily based on RP2040_PWM by Khoi Hoang -- if you need more /
better PWM control, I suggest you look there
(https://github.com/khoih-prog/RP2040_PWM).

This code is designed to allow for the generation of arbitrary audible tones of 
a single frequency at a given volume for a specified duration (with ~microsecond
accuracy). Supports frequencies above ~7.5 Hz. Uses ultrasonic changes to the
PWM duty cycle to create the illusion of volume. The ultrasonic PWM operates
at 62.5kHz (on a stock clock-frequency RP2040). The PWM update functions are
passed to a hardware timer with ~microsecond accuracy.

Handles both single-ended and differential inputs for audio (declared at 
initialization). If using differential inputs, you must use pins on the same
PWM Slice (e.g. PWM_1A and PWM_1B).
*/

#ifndef RP2040_VOLUME
#define RP2040_VOLUME
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <math.h>


#define TIME_MS     0
#define TIME_US     1

#define TOP 1000

// Allow overrides if the default timer is already in use
#ifndef TONE_ALARM_POOL_HARDWARE_ALARM_NUM
#define TONE_ALARM_POOL_HARDWARE_ALARM_NUM 3
#endif


struct timer_data {
    public:
      uint32_t   numRepeats;
      uint32_t   repeats;
      bool       high;
      uint8_t    pinPlus;
      uint8_t    pinMinus;
      uint8_t    sliceNum;
      bool       diff;
      uint16_t   level;
};

class RP2040_Volume{
    public:
      /// @brief Initialize pins for use in tone generation. In differential
      /// mode, you must use pins on the same slice of the RP2040 PWM. You can
      /// initialize as many of these as you like for different pins to drive
      /// multiple outputs. Will hard fail an assert if pins are not on same
      /// PWM slice.
      /// @param pin_plus GPIO Pin number for + lead
      /// @param pin_minus GPIO Pin number for - lead

      RP2040_Volume(uint8_t pin_plus, uint8_t pin_minus = 255) {

          if (pin_minus != 255) {
            _diff = true;
          } else {
            _diff = false;
          }
          
          _sliceNum = pwm_gpio_to_slice_num(pin_plus);

          if (_diff) {
            _pinPlus = pin_plus;
            _pinMinus = pin_minus;

            assert (pwm_gpio_to_slice_num(_pinMinus) == _sliceNum);

            gpio_set_function(_pinPlus, GPIO_FUNC_PWM);
            gpio_set_function(_pinMinus, GPIO_FUNC_PWM);

          } else {
            _pinPlus = pin_plus;
            _pinMinus = 255;
            gpio_set_function(_pinPlus, GPIO_FUNC_PWM);
          }
      }

      /////////////////
      ~RP2040_Volume() {
        cancel_repeating_timer(&_timer);
        delete _timerData;
        alarm_pool_destroy(_alarmPool);
      }
      /////////////////

      /// @brief Non-blocking tone generation using hardware PWM and timer.
      /// Wait until the tone has completed before starting a new one or the
      /// previous tone will get overwritten. Error in frequency increases with
      /// target frequency exponentially, becoming approximately ~200 Hz at
      /// 20 kHz.
      /// @param freq (Hz)
      /// @param volume (0-100) Only accurate to the tenths position (95.11 = 95.1)
      /// @param duration (in units of time)
      /// @param time Timebase, TIME_MS or TIME_US for specifying duration
      void tone(float freq, float volume, uint16_t duration, uint8_t time = TIME_MS) {
        uint32_t usPerWave = freq_to_us(freq);
        pwm_config config = pwm_get_default_config();
        
        // Disable these outputs while we configure:
        pwm_set_enabled(_sliceNum, false);

        // Turn on phase correction:
        pwm_config_set_phase_correct(&config, true);

        // Run PWM at full speed
        pwm_config_set_clkdiv_int(&config, 1);

        // Specify what the PWM counter rolls over at:
        pwm_config_set_wrap(&config, TOP);

        // Initialize, but don't start yet:
        pwm_init(_sliceNum, pwm_gpio_to_channel(_pinPlus), &config, false);

        if (volume > 100) {
          volume = 100;
        }

        if (volume < 0) {
          volume = 0;
        }
        
        _level = (uint16_t)round(volume * 10.0f);

        pwm_set_gpio_level(_pinPlus, 0);

        if (_diff) {
          pwm_set_gpio_level(_pinMinus, _level);
        }

        // Now we need to set up our timer stuff:
        switch (time) {
          case TIME_US:
            _numRepeats = (uint32_t)duration/usPerWave;
            break;
          case TIME_MS:
            _numRepeats = (uint32_t)1000*duration/usPerWave;
            break;
        }

        
        if (_timerData != NULL) {
          cancel_repeating_timer(&_timer);
          delete _timerData;
        }
        
        _timerData = new timer_data();
        _timerData->numRepeats = _numRepeats;
        _timerData->repeats = 0;
        _timerData->high = false; // starts as false so we turn it off first.
        _timerData->pinPlus = _pinPlus;
        _timerData->pinMinus = _pinMinus;
        _timerData->sliceNum = _sliceNum;
        _timerData->diff = _diff;
        _timerData->level = _level;

        // The default pool is not created for some reason, so we need to
        // make it ourselves. 

        if (_alarmPool != NULL) {
          alarm_pool_destroy(_alarmPool);
        }

        _alarmPool = alarm_pool_create(
                TONE_ALARM_POOL_HARDWARE_ALARM_NUM,
                PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS
                );


        alarm_pool_add_repeating_timer_us(_alarmPool, usPerWave,
                              timer_cb, (void *)_timerData, &_timer);
        
        pwm_set_counter(_sliceNum, 0);

        pwm_set_enabled(_sliceNum, true); // Turn on PWM now that we're all set
      }

      /// @brief Turns off the timer and puts PWM pins low
      void stop_tone() {

        cancel_timer();
        delete _timerData;
        _timerData = NULL;
        alarm_pool_destroy(_alarmPool);
        _alarmPool = NULL;

        // Set PWMs to low
        pwm_set_gpio_level(_pinPlus, 0);
        if (_diff) {
          pwm_set_gpio_level(_pinMinus, 0);
        }
      }

    private:
      uint16_t            _level;
      uint8_t             _pinPlus;
      uint8_t             _pinMinus;
      uint8_t             _sliceNum;
      bool                _diff = false;

      struct timer_data  *_timerData = NULL;
      uint32_t            _numRepeats;
      alarm_pool_t       *_alarmPool = NULL;
      repeating_timer_t   _timer = repeating_timer_t();

      
      /// @brief Frequency (in Hz) to microsecond conversion for how often the
      /// PWM inputs need to be switched to generate an appropriate wave.
      /// @param freq (Hz)
      /// @return uint32_t microseconds per half period (rounded)
      uint32_t freq_to_us(float freq) {
        return (uint32_t)round((1.0/(2.0*freq)) * 1e6); // Multiplied by 2 as we switch every half-cycle
      }

      static bool timer_cb(struct repeating_timer *data) {
        struct timer_data *tData = (timer_data*)(data->user_data);
        tData->repeats++;
        if (tData->repeats >= tData->numRepeats) {
          // Set PWMS to 0% duty cycle, but leave running so they go to low correctly
          pwm_set_gpio_level(tData->pinPlus, 0);
          if (tData->diff) {
              pwm_set_gpio_level(tData->pinMinus, 0);
          }
          return false; // this stops when needed. Might be some slack in this...
        }

        if (tData->diff) {
          if (tData->high) {
            pwm_set_gpio_level(tData->pinPlus, 0);
            pwm_set_gpio_level(tData->pinMinus, tData->level);
          } else {
            pwm_set_gpio_level(tData->pinPlus, tData->level);
            pwm_set_gpio_level(tData->pinMinus, 0);
          }
        } else {
          if (tData->high) {
            pwm_set_gpio_level(tData->pinPlus, 0);
          } else {
            pwm_set_gpio_level(tData->pinPlus, tData->level);
          }
        }

        tData->high = !tData->high;

        return true;

      }

      void cancel_timer() {
        cancel_repeating_timer(&_timer);
      }

};

#endif
