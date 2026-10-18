# Thermo dial

A tile that looks and works like Home Assistant's thermostat card: a dial you drag to set the target temperature of a
climate entity, with the temperature now under it and keys to step it and change its mode.

## Set up

1. In Tessera, open **Plugins**, choose **Thermo dial** and tick your screens. Each screen builds once.
2. In Layout, place the **Thermo dial** tile at 2x2 or larger.
3. Choose the `climate.*` entity in the tile's inspector.

## What it shows

- From 2x2: the dial with the target large in the middle, "Now 19.8°" and what the thermostat is doing under it, and the
  keys - , mode and + below. The ring is amber while heating, blue while cooling and grey when idle or off.
- Smaller than 2x2: the target as a number, with - and + from 2x1.

## Good to know

- Dragging changes the number only; Home Assistant gets one `climate.set_temperature` when you let go. The - and + keys
  send one after 600 ms without another tap.
- If Home Assistant does not report the new target within 5 seconds, the dial goes back to the real one.
- Range and step come from the entity (`min_temp`, `max_temp`, `target_temp_step`).
- An unavailable thermostat shows a grey dial and a dash. The mode key shows only when the entity offers two modes or more.

## Boards

Tried on a real screen: the Waveshare ESP32-S3-Touch-LCD-7B (7 inch, 1024x600), with a real `climate` entity: dragging
the dial, the - and + keys and the mode key. Drawn by the plugin bench on the CYD, a 4-inch Guition, a 4.3-inch
Waveshare and a 10-inch Guition P4, light and dark. The dial needs room: on the small screens a 2x2 tile shows the
number and keys instead.
