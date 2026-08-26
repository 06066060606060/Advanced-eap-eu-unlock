# MODEL YL UNLOCK — T2CAN v2.0

Clean release package for the LilyGO T-2CAN / ESP32-S3 firmware currently used on Tesla Model YL.

## Release status

- Target build: **Model YL** (`MODEL_YL = 1`)
- CAN A: MCP2515, 500 kbit/s, Party CAN
- CAN B: ESP32 TWAI, 500 kbit/s, VH CAN
- Dashboard title: **MODEL YL UNLOCK**


## Files required to compile

Keep these three files in the same Arduino sketch folder:

- `EU-Advanced-EAP.ino`
- `index_html.h`
- `pin_config.h`

`index_html.h` contains the embedded dashboard. `pin_config.h` contains the T-2CAN GPIO mapping.

## Board / hardware notes

The current project configuration uses the LilyGO T-2CAN hardware with an ESP32-S3 and an external MCP2515.

Current pin configuration:

- TWAI TX: GPIO 7
- TWAI RX: GPIO 6
- MCP2515 CS: GPIO 10
- MCP2515 RESET: GPIO 9
- MCP2515 INT: GPIO 8
- SPI SCLK: GPIO 12
- SPI MOSI: GPIO 11
- SPI MISO: GPIO 13
- MCP2515 oscillator setting: 16 MHz

## Current Model YL CAN topology

### CAN A — Party CAN — MCP2515

- `0x370`: NAG source / injected echo
- `0x118` (legacy decimal 280): gear / DI state input
- `0x186` (legacy decimal 390): vehicle / gear state input
- `0x399` (legacy decimal 921): DAS / Autopilot state
- `0x24A`, DLC 8: `DAS_visualDebug` source used by Advanced EAP Auto Blinker

### CAN B — VH CAN — TWAI

- `0x249`, DLC 4: SCCM turn-indicator stalk status; live template and injected Auto Blinker frame
- `0x3F8` (legacy decimal 1016): driver-assist / SPR and passive stock ULC telemetry
- `0x3FD` (legacy decimal 1021): Summon Unlock on mux 1 and TLSSC on mux 0
- A separate VH-local `0x24A`, DLC 4 can exist and is intentionally **not** used as the YL `DAS_visualDebug` source

## Main active features

- NAG Killer
- EU / Summon Unlock
- Summon TX Priority state machine
- TLSSC injection with an AP-active-only gate
- Advanced EAP Auto Blinker, NOA-only
- CAN A/B self-healing and Hard Reinitialize
- OTA firmware upload
- Runtime and boot timing diagnostics

## Auto Blinker current behavior

- Trigger source: Party CAN `0x24A` DLC 8, behavior bits 56..57
- Allowed only when Party CAN `0x399` DAS state is `5` (`ACTIVE_NAV` / NOA)
- NOA status must be fresh within **2000 ms**
- Default trigger delay: **1.0 s**
- Delay is configurable from the dashboard and stored in NVS
- SCCM command pulse: **350 ms**
- SCCM transmit period while pulsing: **20 ms**
- Model YL SCCM frame: `0x249`, DLC 4
- Soft right stalk value: `2`
- Soft left stalk value: `6`
- CRC: Model YL validated CRC-8 model using polynomial `0x2F` and the live stock frame/counter

## CAN recovery current behavior

The firmware monitors CAN task heartbeat and separate CAN A/B RX freshness.

- First cold acquisition timeout: 2 s after CAN initialization
- CAN task heartbeat timeout: 3 s after a 5 s startup grace
- Bus freshness window: 2 s
- One-bus-only stale confirmation: 4 s
- Sleep detection: both buses quiet for 5 s after a proven active session
- Wake acquisition allowance: 5 s
- Hard Reinitialize cooldown: 10 s
- Cold retry interval after the first attempt: 15 s
- Maximum bounded cold retries: 3, followed by passive wait

Hard Reinitialize stops both CAN tasks, cold-initializes MCP2515, fully reinstalls/restarts TWAI, recreates both CAN tasks, clears bus freshness state, and fails closed for NOA Auto Blinker until fresh status returns.

## Boot timing capture

Passive startup diagnostics are exported at:

`/api/system/boot-capture.csv`

The capture includes first CAN A/B frames, first `0x370`, first `0x399`, first Party `0x24A`, first VH `0x249`, Wi-Fi-ready time, and Hard Reinitialize events.


