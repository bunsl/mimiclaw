# GPIO Control
Control and monitor GPIO pins on the ESP32-S3 for digital I/O.

## When to use
Use this skill when the user asks to:
- Turn on/off relays or simple digital outputs
- Check switch states, button presses, or digital sensor levels
- Confirm digital I/O status
- Get an overview of GPIO states

## How to use
1. To read a switch or sensor, call `gpio_read` with a pin number.
2. To set a digital output, call `gpio_write` with `pin` and `state` (`1` or `0`).
3. To scan all allowed pins, call `gpio_read_all`.
4. For confirmation flows, read before and after write when useful.

## Important note
- Do not use `gpio_write` for the onboard RGB LED (NeoPixel/WS2812).
- For onboard RGB LED requests, use the dedicated `led_set` tool.

## Pin safety
- Only allowed pins can be accessed.
- ESP32 flash/PSRAM pins are blocked.
- If a pin is rejected, explain why and suggest an allowed alternative.

## Example
User: "Turn on the relay on pin 5"
1. Call: `gpio_write {"pin":5,"state":1}`
2. Optional confirm: `gpio_read {"pin":5}`
3. Reply with the actual tool result.
