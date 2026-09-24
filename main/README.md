\mainpage Wireless 0-10 volt dimmer

# Design and implementation notes

This design is for the case of 0-10 volt dimmable fixtures controlled by multiple switches replacing
old fixtures that either weren't dimmable or used mains dimming, e.g. standard triac-based devices.
Therefore either new 0-10 volt wiring or a wireless solution is needed. This project is about the
second approach.

It is my first ESP32 project. All feedback is very welcome. Be kind.

## Design goals

- Retain existing on/off switches. No mains power switching by the dimmer.
  - Avoids safety considerations and complexity.
- Receiver (rx) box at each light running on switched light power, producing the 0-10 volt control.
- Transmitter (tx) box with a knob at each switch (or anywhere else). Twisting any changes the 0-10
  volt output of all receivers.
- No batteries. Mains power for all boxes.
- Secure: Reasonably proof against playback and spoofing attacks.
- Power interruption resilience: doesn't lose dimming level and keeps working.

## Design approach

- ESP32-WROOM dev board for each tx and rx.
- ESP-IDF environment because several threads will be needed.
  - Looks more full-featured than Arduino.
- ESP-NOW wifi broadcast protocol for communication.
  - Repeated broadcasts for robustness. No tx/rx handshake.
- Messages secured via checkpointed sequence numbers and 128-bit hmac.
  - Hard-coded hmac password to avoid distribution complexity.
- Rotary quadrature encoder for tx knob. Polled for simplicity.
- Checkpoints include sequence current dimming level for power off resilience.
  - ESP32 non-volatile storage (nvs) written with de-bouncing to conserve flash life.
- Receiver PWM output to simple op-amp based 0-10 driver.
- On-board LED as state indicator.
- 3d-printed cases.
  - Tx case integrated with light switch plate.
  - Rx is a "cord bump" with mains plug entering one end and 0-10 volt control line the other.

## Firmware notes

Single firmware load for tx and rx. Ground strap on pin `RECV_STRAP_GPIO` in `dimmer.c` makes the
unit an rx. Else it's a tx.

### Configuration

The default ESP-IDF project configuration had to be modified.

- FreeRTOS timer tick rate (`CONFIG_FREERTOS_HZ`) to 1000
  - Supports rotary encoder polling.
- Flash size to 4Mb. (`CONFIG_ESPTOOLPY_FLASHSIZE_4MB` and `CONFIG_ESPTOOLPY_FLASHSIZE`) Boot
  logging suggested this.

Other dev boards may require changing `ONBOARD_LED_GPIO` in `led.h` and other GPIO assignments. All
other GPIO assignments are in `dimmer.c` and may be reconfigured at will subject to the normal
caveats.

### Threads

- Wifi stack making standard callbacks.
- Broadcast repeats. Async after initial trigger. Cancelled by new trigger.
- Flashing LED via FreeRTOS timer state machine.
- Rotary encoder via FreeRTOS timer-based polling.
- Checkpoints to nvs, debounced to conserve nvs writes.

### Shared state

The module `shared.c/h` houses data touched by several threads, therefore implemented with
`<stdatomic.h>`. These are exactly sufficient for power fail resilience checkpoints.

- Broadcast sequence number
- Current dimmer level

There is another small block of data protected by mutex in `wifi.c`. It contains state for sending
broadcasts repeatedly. The triggering thread sets up this data while holding the mutex, then wakes
up the repeat thread, which iterates the requested number of times, grabbing the mutex to ensure no
races with triggering threads.

## Rx driver circuit

There are two options: one that will always work and another depends on a particular implementation
of the driven lights.

Both options start with a 2-pole RC low pass filter to convert the 3.3 volt PWM output to a 0-3.3
volt DC signal. "Soft start" is also desired: forcing a 0v output for about one second while the
receiver boots and sets up the PWM output at turn-on time.

### All purpose option

Use a standard 3.03x non-inverting op amp circuit (LM385 or similar) to convert 0-3.3 volts to 0-10.
The op amp requires a 12 volt supply in this case, while the ESP32 needs either 3.3 or 5 volts. A
dual voltage supply is necessary.

### Pulled inputs option

If the controlled lights have a 10v (or more) default potential across the control line, then the op
amp can drive a BJT that serves only to draw down this potential to the dimming level. A 1:3 voltage
divider in the feedback loop lets the op amp bias the transistor properly using only a 5-volt
supply. This is handy, as it allows receivers to work with a single rather than dual voltage supply.
