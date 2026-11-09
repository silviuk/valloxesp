# Vallox to home assistant
Vallox SE (RS-485 supported) ventilation control software for ESP-01 & Home Assistant integration

# ESP-01 (valloxesp)
See guide at https://www.creatingsmarthome.com/?p=73

Thanks for original vallox libary implementation by dirtyha:
https://github.com/dirtyha/my-esp8266/tree/master/Vallox

## Tested and verified with:
- Vallox 096 SE
- Vallox 110 SE
- Vallox 121 SE (version without front heating module)
- Vallox 121 SE (version with front heating)
- Vallox 150 SE
- Vallox 270 SE
- Vallox Digit SE
- Vallox Digit SE 2
- Vallox ValloPlus 350 SE
- Vallox ValloPlus SE 500

# Home Assistant component (custom_components)
Contained entities:
* climate: Climate sensor has basic functionalty like mode: heat/ fan, fan speed: 1-8, heat target
* status: Binary status sensor (on/off) that has multiple read-only attributes
* switch: Switch is used to perform boost or fireplace function (depending which one you have selected from vallox configuration)
* sensor: switch type to see if switch is configured as fireplace or boost
* sensors: outside, inside, exhaust and incoming temperatures
* sensors: rh1 and rh2 if supported by your Vallox Ventilation machine
* sensor: co2 if supported by your Vallox Ventilation machine
* binary sensors: motor in/motor out statuses
* binary sensor: summer mode
* sensor: service counter (how many months until you have to replace your filters)

## Read-only extra registers (firmware 1.1.0 / integration 1.2.0)

The following keys extend the existing `vallox/state` JSON. Existing keys and
unique IDs are unchanged. A key is omitted until its first valid response;
the corresponding Home Assistant entity is unavailable while the key is absent
or null. Zero and false are valid received values. State publishing continues
to use MQTT `beginPublish` streaming; the 2048-byte JSON document accommodates
the added fields without depending on PubSubClient's packet buffer size.

| MQTT key | Register / conversion | Home Assistant entity |
|---|---|---|
| `fault_code` | 0x36, raw decimal | Fault Code sensor, no unit or measurement state class |
| `boost_remaining` | 0x79, raw minutes | Boost Remaining duration sensor, min, measurement |
| `post_heating_target` | 0x57, NTC table | Post Heating Target temperature sensor, °C, measurement |
| `bypass_setpoint` | 0xAF, NTC table | Bypass Setpoint temperature sensor, °C, diagnostic (no measurement state class) |
| `preheat_setpoint` | 0xA7, NTC table | Preheat Setpoint temperature sensor, °C, diagnostic (no measurement state class) |
| `defrost_setpoint` | 0xA8, NTC table | Defrost Setpoint temperature sensor, °C, diagnostic (no measurement state class) |
| `defrost_hysteresis_raw` | 0xB2, raw decimal | Defrost Hysteresis Raw sensor, no unit, diagnostic |
| `defrost_hysteresis` | 0xB2, raw / 3.0 | Defrost Hysteresis difference sensor, K, diagnostic (no measurement state class) |
| `max_fan_speed` | 0xA5, fan bitmask decoded to 1–8 | Maximum Fan Speed sensor, no unit, diagnostic |
| `filter_warning` | 0xA3 bit 4 | Filter Warning problem binary sensor |
| `error_relay` | 0x08 bit 2 | Error Relay problem binary sensor |
| `cell_freeze_alarm` | 0x6D bit 7 only | Cell Freeze Alarm problem binary sensor |

The four existing temperature sensors also expose temperature device class and
measurement state class. Fault Code is the last reported code: zero or a stored
code alone does not indicate a current alarm. Use the fault/error flags for
current status. Post Heating Target is the actual target register, distinct from
the existing `heat_target` user setpoint; behavior on this model still needs a
read test. Native boost countdown behavior also awaits device verification.

After status initialization, 0x36, 0x79, 0x57 and 0x6D are queued immediately and
about every 60 seconds. The five diagnostic setting registers are queued after
initialization and through the existing five-minute `QUERY_INTERVAL` path,
alongside I/O flags and the service counter. All polling, including startup and
missing-value retries, shares a deduplicated queue: at most one poll is sent per
loop iteration, without blocking delays and with at least 100 ms of observed bus idle between
requests. A matching checksum-valid mainboard reply to this panel releases the
outstanding request; otherwise it times out after two seconds. Unsupported
optional registers retry on their next normal 60-second/five-minute cycle and
never block status initialization. Missing base values retry on the existing
five-second retry path. Overlapping groups share the queue without bursts.

The five-minute I/O reply refreshes retained snapshots. The integration expires a topic after 15 minutes without a valid payload; this is a freshness timeout, not an immediate hardware connectivity indication. Optional keys omitted from a new snapshot are cleared. Unsupported optional registers back off after three missed replies and are probed hourly.

These additions provide no bus setters or MQTT commands. RH/CO₂ registers are
not periodically polled; existing
passive decoding is retained. Startup still issues the historical one-shot RH1
request for master-compatible boot behaviour; it is not retried if unanswered. Register 0x6D bit 6 is ignored. The bypass setpoint
does not command damper position, and maximum fan level enforcement may depend
on the unit's maximum-limit mode.

## Command write queue

Control writes use a small bounded queue (coalesce by register: newest value
wins, original deadline kept; FIFO across different registers). A write is only
transmitted when the bus is idle; it waits up to 2.5 s, then fails with a
`vallox/command_result` message (`CMD_RESULT failed …`). Values are published to
MQTT only after the frame is actually sent. Boost/fireplace activation is not
re-queued while already active, and repeated activates coalesce to one pulse.
Status writes require a previously received status byte and a fresh read after
confirmation (or timeout). Selecting FAN or HEAT combines power-on and heating
mode in one status write.

# Configuration
See guide at https://www.creatingsmarthome.com/?p=105

## Breaking changes
Configuration is no longer possible by using configurations.yaml. Please use UI configuration from integrations!

If you have old configuration in configurations.yaml, please remove it and make a new configuration through UI.


## Support the developer?
[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/tokorhon)
