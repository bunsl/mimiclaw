# Display Control
Control the onboard `1.44"` `128x128` TFT display for short, user-visible messages.

## When to use
Use this skill when the user asks to:
- Show text on the screen
- Clear the current message
- Switch display theme
- Switch display language
- Confirm a hardware action with visible on-device feedback
- Show a short status, label, icon, or debug message

## Tool mapping
- Use `display_show_text` to render short text with optional `title`, `icon`, `role`, and `duration_ms`.
- Use `display_clear` to clear the message area.
- Use `display_set_theme` to switch between `dark` and `light`.
- Use `display_set_locale` to switch locales such as `zh-CN`, `en-US`, or `ja-JP`.

## Execution rules
1. Execute the display tool first instead of only describing what should appear.
2. Keep messages short because the TFT UI is compact and optimized for glanceable status.
3. Prefer 1-3 short lines rather than long paragraphs.
4. Use `title` for the short headline and `text` for the main body.
5. Prefer an icon when it improves recognition. Good defaults include `wifi`, `microphone`, `circle_check`, `triangle_exclamation`, and `microchip_ai`.
6. Use `duration_ms` for temporary confirmations, toasts, and transient debug overlays.
7. Report the actual tool result in the reply.

## Theme And Locale
- Theme is persistent across restarts.
- Locale is persistent across restarts.
- Screen strings support multilingual resources with fallback to `zh-CN`.
- Icons use Font Awesome names.

## Constraints
- The display is designed for short operational messages, not long-form reading.
- If rendering fails, report the exact error and suggest checking wiring, SPI pins, flash image, or display configuration.
