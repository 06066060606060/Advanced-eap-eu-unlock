# Advanced EAP & EU-Unlock V2.5c
### Unified firmware for LilyGO / T-2Can

> ⚠️ **Research / educational firmware only**
>
> This project interacts with a Tesla vehicle CAN bus. It is intended for controlled bench testing, code review, and research environments only.
>
> It sends signals directly to the controller, not a physical command to the steering wheel. **Do not use this on public roads or in any situation where unsafe behavior could put people or property at risk.**
>
> You are responsible for your own testing, wiring, configuration, and compliance with local laws.

>  **Important:** You will need another ESP32 for **Nag-Killer on Party CAN**.

### ⚠️ DO NOT APPLY TLSSC RESTORE ON NON BANNED CAR (you will be banned instantly)

### CAN Bus Configuration

- **CAN A — Body CAN** → Advanced EAP
- **CAN B — Chassis CAN** → EU Summon Unlock

---

## 📋 What's New

### V2.5c

- Added **TLSSC Restore** for banned cars.  
- Added **Blindspot aggressiveness** settings (`MadMax`).
- Added the ability to **cancel a lane-change request using the open-door button**.

### V2.0b

- Completely reworked code.
- New dashboard design by **LP_YL**.
- Toggle to activate **Auto Lane Change** in:
  - AP / NOA
  - NOA only
- Summon TX Priority state machine.
- TLSSC injection with an **AP-active-only gate**.
- CAN A/B self-healing and hard reinitialization.
- Runtime and boot timing diagnostics.

---

## 🚗 Advanced EAP

- Automatically activates the turn signal.
  - Delay can be configured from the dashboard.
- All lane-change safety features are maintained.
- The turn signal starts only when the vehicle requests a lane change.
- Lane changes can always be cancelled:
  - On screen.
  - With the opposite turn signal.
  - Using the open-door button.

---

## 🇪🇺 EU Unlock

- Bypass **R79 EU restriction** in AP.
- Expand Summon range to **±85 m**.
- Expanded lateral acceleration limits.
- Lane changes near forks are not disabled (EAP).
- Instantaneous lane change on blinker (EAP).
- No lane-change timeout once initiated (EAP).
- Automatically takes forks and exits (EAP).
- Toggle to activate **TLSSC** where it is not available.
  - A valid **EAP/FSD subscription** is required.
- Continue on Green with Car in Front (**TLSSC**).
- OTA update support.

---

## 🚙 Model YL Branch

A dedicated branch is available for **Model YL**:

https://github.com/06066060606060/Advanced-eap-eu-unlock/tree/modelYL

---

## 🔧 Hardware Target

This fork was adapted for the **LilyGO / T-2Can**.

| Device | CAN Interface | CAN Bus | Function | Power |
|---|---|---|---|---|
| LilyGO / T-2Can | CAN A — MCP2515 | Body CAN | Advanced EAP  | 
| LilyGO / T-2Can | CAN B — TWAI | Chassis CAN | Summon Unlock | USB-C or stable 12 V |

**CAN speed:** 500 kbps

### ⚠️ 120 Ω Resistors

Don't forget to remove the two **120-ohm resistors**, which can cause signal errors.

<img width="407" height="180" alt="LILYGO-T-2CAN_9" src="https://github.com/user-attachments/assets/0d272b7e-bd82-408f-9ca1-239e6dab44d5" />

---

## 🛠️ Board Setup — Arduino IDE

### Board

- **LilyGo T-Display S3**

### Required Libraries

- **ESP32 BLE Arduino** — built-in.
- **MCP2515 by autowp** — install via the Arduino Library Manager.
- Repository: https://github.com/autowp/arduino-mcp2515

---

## 📁 Sketch Files

The sketch folder contains:

```text
EU-Advanced-EAP.ino
index_html.h
pin_config.h
```

---

## ✅ Confirmed Working

| Vehicle / Firmware | Status |
|---|---|
| AP Injection | Does not work before **2026.20** |
| Model Y | **2024 HW4 — Berlin** |
| Firmware | **2026.26.1** |

---

## 🔌 Wiring

| Interface | Transceiver | Vehicle CAN Bus | Function |
|---|---|---|---|
| **CAN A** | MCP2515 | Body CAN (9–10) | Advanced EAP   |
| **CAN B** | TWAI | Chassis CAN (13–14) | Summon Unlock |

---

## 🌐 Dashboard

After boot, connect to the ESP32 Wi-Fi access point.

| Parameter | Value |
|---|---|
| **SSID** | `T2CAN-A1B2` — example |
| **Password** | `12345678` |
| **Dashboard** | `http://192.168.4.1` |

> ⚠️ **TLSSC warning:** Do not enable TLSSC if you do not have the EAP option.

---

## 📡 OTA Firmware Update

Build the firmware using **Arduino IDE**:

1. Open the sketch in Arduino IDE.
2. Select:
   **Sketch → Export Compiled Binary**
3. Open:
   ```text
   /EU-Advanced-EAP/build/
   ```
4. Locate:
   ```text
   EU-Advanced-EAP.ino.bin
   ```
   *(approximately 900 KB)*
5. Open the web dashboard.
6. Go to **Update** and upload the `.bin` file.

---

## 💬 Community & Support

### Discord

Join the project Discord server:

https://discord.gg/euPbYG8Npc

---


### ☕ Support the Project

<a href="https://www.buymeacoffee.com/xbmod" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" style="height: 60px !important;width: 217px !important;" ></a>

### ₿ Bitcoin

```text
bc1pl9nuyhqd78gjc2wdcqr39de7qwtff732ngr28vy8r2sxfa7a6uzsrhe387
```

### ⚡ Lightning

```text
₿cakegrip53@phoenixwallet.me
```

---

## 🙏 Credits

- Inspired by **Ev Open Can tools**  
- Created by **X₿mod**, updated by **LP_YL**.
- ESP32 TWAI driver by **Espressif Systems**.
- Automotive CAN research community.

## 📸 Dashboard

<img width="270" height="475" alt="Advanced EAP" src="https://github.com/user-attachments/assets/98c1d6aa-3649-4669-b88b-619103429297" />
