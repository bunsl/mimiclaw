# LED Control
Control the onboard RGB status LED.

## Trigger
Use this skill when the user asks to:
- Turn the onboard LED on/off
- Set the onboard LED color
- Flash or pulse the onboard LED
- Verify LED action

## Tool mapping
- Use `led_set` for all onboard RGB LED actions.
- Input schema: `{"r":0-255,"g":0-255,"b":0-255}`.

## Execution rules
1. Execute the tool first. Do not only describe code snippets or methods.
2. Report the real tool output in the final reply.
3. For "turn off LED", call `led_set {"r":0,"g":0,"b":0}`.
4. For named colors, map to RGB:
   - red: `255,0,0`
   - green: `0,255,0`
   - blue: `0,0,255`
   - white: `255,255,255`
   - yellow: `255,255,0`
   - cyan: `0,255,255`
   - magenta: `255,0,255`
5. If the user asks for blink/pulse, simulate with a short sequence of multiple `led_set` calls and then report completion.

## Constraints
- Do not use `gpio_write` for onboard RGB LED control.
- If `led_set` fails, explain the exact error and suggest retry after flashing/reboot.
