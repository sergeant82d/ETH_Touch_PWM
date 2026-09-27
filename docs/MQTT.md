# MQTT / Home Assistant

The only link to Home Assistant since Phase 3 (2026-09-27). It replaced the REST link
(`home_assistant.cpp`, HA token, `input_number` / `input_boolean` helpers, `rest_command`,
configuration.yaml). Design first verified in HA with the simulator `tools/mqtt_sim.py`.
Modeled on Wifi_Fan_Knob's `src/mqtt.cpp`. **HA needs two automations** (below): the network
temperature (required for blending) and fault notifications (optional).

Broker: Mosquitto add-on on HA, `192.168.10.85:1883`, login required.

## Firmware status

- **Phase 1 (2026-09-27, tested on the ESP32-S3-ETH):** `src/mqtt.cpp`. Web page section "MQTT (Home
  Assistant)": node ID, broker, port, user, password (never sent back to the page; blank =
  keep) and a status line. Publishes the read-only entities: temperatures, probe faults, fan
  speed/duty/fault (fans up to Active Fan Channels; others removed from HA), IP, uptime. REST
  ran alongside until Phase 3. Saving the form reconnects; a changed node ID removes the old device
  from HA. Runs in `loop()` (the Ethernet library isn't thread-safe); a connect attempt every
  15 s can block up to ~1 s (unreachable) or 5 s (no broker reply).
- **Phase 2 (2026-09-27):** thresholds and manual override from HA (`t_min`, `t_max`,
  `override`, `override_speed` + `/set`). Changes are saved (thresholds) and logged to SD with
  `source=HA`, like web/LCD changes; the device republishes its state after every command.
  Removed: the `input_number`/`input_boolean` helper sync (60 s and 5 min polls, pushes from
  the web page and LCD) and the helper entity fields on the web page. `/override_set` stays
  (the web page uses it).
  Tested on the ESP32-S3-ETH over MQTT (commands as HA sends them): thresholds set/saved/
  range-checked, override on/speed/off with fan duty following, speed ignored while off,
  web page override changes reach MQTT within 1 s, command echo 0.15-0.7 s.
- **Phase 3 (2026-09-27):** REST removed (`home_assistant.cpp`, token, host/port/sensor fields,
  the 2 s telemetry POST and network-temperature GET, persistent notifications). Network
  temperature arrives on `network_temp/set` (HA automation); failed after 5 minutes without
  one. Daily summary is the "Summary of the day" sensor (re-sent on the next connect if MQTT
  was down at the day change). Settings layout unchanged (version 5): the REST-era fields are
  kept as `unused...` and emptied on load, which wiped the stored HA token (boot log: "Old
  REST-era HA settings (incl. token) cleared."). Tested on the ESP32-S3-ETH: token wipe, NTP,
  SD, MQTT connect, network temperature in °F/°C, `unavailable` and bad JSON = failed,
  recovery, blending, marked failed 5 minutes after the last value. Not yet seen: a real day
  change (daily summary).
- State cadence: temperatures, faults, duty on change (checked every second); fan RPM on a
  60 RPM change, to/from stopped, or after 30 s; IP and uptime every 60 s.
- Settings: version 5 adds the MQTT fields; a version 4 file (864 bytes) is upgraded in place,
  so IP, node ID and thresholds are kept.

## Device

- `nodeID` (web page setting, default `fanController_xx`) is the device name, MQTT client ID,
  topic base and `unique_id` prefix. Letters, digits, `_`, `-` only. MQTT stays off while it
  is still `fanController_xx`, so two unconfigured units can't fight over one name.
- HA entity IDs are lower case: `fanController_02` + "Air temperature local" becomes
  `sensor.fancontroller_02_air_temperature_local` (IDs are fixed when an entity is first
  created; renaming later changes only the displayed name).
- Entity names sort into groups on HA's device page (it lists by name): "Air temperature
  blended/local/network", "Fan duty N", "Fan speed N", "Fault fan N / local probe /
  network probe"; IP address and Uptime under Diagnostic. Controls: "Fan curve start"
  (tMin), "Fan curve top" (tMax), "Manual override", "Manual override speed".
- Discovery: `homeassistant/<component>/<nodeID>/<object>/config`, retained.
- Availability: `<nodeID>/status` = `online` / `offline` (Last Will), retained.

## Topics (base `<nodeID>/`)

Device to HA, retained. Temperatures in °C with the temperature device class, so HA shows
them in its own unit system. `None` = unknown (probe failed).

| Object | Component | Payload | Replaces |
|---|---|---|---|
| `local_temp`, `network_temp`, `blended_temp` | sensor | `24.1` / `None` | attributes of `sensor.<nodeID>` |
| `fanN_rpm` | sensor | `1450` | `fanN_rpm` attribute |
| `fanN_duty` | sensor | `0`-`100` % | (new) |
| `fanN_fault` | binary_sensor, problem | `ON` / `OFF` (duty > 51 of 255 and 0 RPM) | `fanN_fault` attribute |
| `local_probe_fault`, `network_probe_fault` | binary_sensor, problem | `ON` / `OFF` | persistent notifications |
| `t_min`, `t_max` | number, °C, 0-100, step 0.1 | `26.7` | `input_number` tmin/tmax helpers |
| `override` | switch | `ON` / `OFF` | `input_boolean` override helper |
| `override_speed` | number, 0-100 % | `100` | `input_number` override speed (0-255) |
| `daily_summary` | sensor "Summary of the day", attributes in `daily_summary/attributes` (min/max per temperature and fan, `null` = no samples) | `2026-09-26` | `sensor.<nodeID>_daily_summary` |
| `ip`, `uptime` | sensor, diagnostic | `192.168.10.54`, seconds | (new) |

HA to device (not retained): `t_min/set`, `t_max/set`, `override/set`, `override_speed/set`,
`network_temp/set`.

Rules:
- `override/set ON` starts at 100 %, like the web page and LCD. `override_speed/set` is
  ignored while the override is off (as now); the device republishes its real state, so
  HA's slider snaps back.
- The device publishes its state after every command, so HA, web page and LCD agree.
- `network_temp/set` payload: `{"value": "24.1", "unit": "°C"}` (°F is converted). Anything
  that doesn't parse (e.g. `"value": "unavailable"`) marks the network probe failed, as does
  no message for 5 minutes.

## HA automation: network temperature (required)

`living_room_probe_02` is a Bluetooth sensor in HA, so HA sends it to the device. Without this
automation the device runs on its local probe only and reports "Fault network probe". Create
it in Settings > Automations > Create > (three dots) Edit in YAML, paste, save. `topic` is the
device's nodeID + `/network_temp/set` (`fanController_sim` for the simulator). For several
controllers, add one `mqtt.publish` action per controller.

```yaml
alias: Fan controller - network temperature
description: Sends living_room_probe_02 to the fan controller over MQTT (every change + every minute)
triggers:
  - trigger: state
    entity_id: sensor.living_room_probe_02_temperature
  - trigger: time_pattern
    minutes: /1
actions:
  - action: mqtt.publish
    data:
      topic: fanController_01/network_temp/set
      payload: >-
        {"value": "{{ states('sensor.living_room_probe_02_temperature') }}",
        "unit": "{{ state_attr('sensor.living_room_probe_02_temperature', 'unit_of_measurement') }}"}
mode: queued
```

## HA automation: fault notifications (optional)

Replaces the persistent notifications the REST firmware created. One notification per fault,
dismissed when it clears. The entity IDs are the ones HA created for `fanController_01` in
Phase 1 (check them on the device page; they keep their first names even after renaming).

```yaml
alias: Fan controller - fault notifications
description: Notification while a fan controller reports a fault
triggers:
  - trigger: state
    entity_id:
      - binary_sensor.fancontroller_01_fan_1_fault
      - binary_sensor.fancontroller_01_fan_2_fault
      - binary_sensor.fancontroller_01_local_probe_fault
      - binary_sensor.fancontroller_01_network_probe_fault
    to: ["on", "off"]
actions:
  - choose:
      - conditions: "{{ trigger.to_state.state == 'on' }}"
        sequence:
          - action: persistent_notification.create
            data:
              notification_id: "{{ trigger.entity_id }}"
              title: Fan controller fault
              message: "{{ trigger.to_state.name }} since {{ now().strftime('%H:%M') }}."
    default:
      - action: persistent_notification.dismiss
        data:
          notification_id: "{{ trigger.entity_id }}"
mode: queued
```

## HA cleanup after the switch (Phase 4)

What the REST link needed in HA (from `archive/web_changes.md` section 19). Remove only once
every controller runs the MQTT firmware:

- `rest_command: fan_ctrl_02_set_override` (configuration.yaml)
- Automations "Fan Ctrl 02 - Push Override Switch to Device" and "... Push Override Speed to
  Device" (automations.yaml)
- Helpers `input_boolean.fan_ctrl_02_override`, `input_number.fan_ctrl_02_override_speed`,
  `input_number.fan_ctrl_02_tmin`, `input_number.fan_ctrl_02_tmax`
- Template `binary_sensor`s reading the `fan1_fault` / `fan2_fault` attributes, if made
- The REST-posted `sensor.fan_controller_02` and `sensor.fan_controller_02_daily_summary`
  (gone after an HA restart once nothing posts them)
- The long-lived access token used by the controllers (revoke it in the HA user profile;
  the MQTT firmware wipes its stored copy)
- `sensor.fan_controller_01` (posted by the old Arduino build on the ESP32-S3-ETH)

## Simulator test checklist

`python tools/mqtt_sim.py` (see its header for setup). In HA: Settings > Devices >
MQTT > `fanController_sim`.

1. Device appears with all entities; temperatures show in HA's units.
2. Network temperature fills in within a minute of saving the automation; the simulator logs
   `[CMD] ... network_temp/set`.
3. Min/Max threshold changed in HA: simulator logs it, fan duty follows.
4. Manual override on: duty 100 %; speed slider changes duty; slider while off snaps back.
5. Type `l` in the simulator: Local probe fault on, blended = network. Type `1`: Fan 1 fault on.
6. Disable the automation: Network probe fault on after 5 minutes.
7. `q`: device shows unavailable. `--remove` deletes it from HA.
