

# Nag-killer Advanced EAP & EU-Unlock V2.0 Model YL version for LilyGO/T-2Can  

> ⚠️ Research / educational firmware only.
>
> This project interacts with a vehicle CAN bus. It is intended for controlled bench testing, code review, and research environments only.It sends signals directly to the controller, not a physical command to the steering wheel. Do not use this on public roads or in any situation where unsafe behavior could put people or property at risk. You are responsible for your own testing, wiring, configuration, and local laws.
---



## Version2.0 by LP YL

-----------------Nag Killer------------------    
- remove nag  
-----------------Advanced EAP----------------  
- automatically activates the turn signal in NOA (delay can be set on the dashboard)  
- All lane change safety features are maintained.  
- the turn signal start only when the vehicle request a lane change,  
- you can always cancel on screen or with opposite turn signal.  

--------------------EU Unlock------------------
- toggle to activate TLSSC where it is not available. (you need a valid FSD subscription)  
- bypass R79 EU restriction in AP
- Expend summon to +/-  85m
- expanded lateral acceleration limits
- lane changes near forks isn't disabled (EAP)
- instantaneous lane change on blinker (EAP)
- no lane change timeout once initiated (EAP)
- takes forks and exits automatically (EAP)
- Continue on Green with Car in Front (TLSSC)
- OTA Update

  <img width="301" height="655" alt="IMG_1839" src="https://github.com/user-attachments/assets/9b9cdb59-b0e8-45e1-8369-70a5b4e1fdab" />

## Hardware Target

This fork was adapted for:

| Device                       | Can Transceiver                         | CAN RX / CAN TX   | Can Bus      | Power                     |
| ---------------------------- | --------------------------------------- | ----------------- | ------------ | ------------------------- |
| LilyGO/T-2Can                |CAN A Party CAN                          |                   |              |                           |
|                              |CAN B Body/VH CAN                        |                   | 500 kbps CAN | USB-C or stable 12V supply|

Don't forget to remove the two 120-ohm resistors which can cause signal errors.   
<img width="407" height="180" alt="LILYGO-T-2CAN_9" src="https://github.com/user-attachments/assets/0d272b7e-bd82-408f-9ca1-239e6dab44d5" />

---

## Board Setup (Arduino IDE)  
- Board: LilyGo T-Display S3 

## Libraries needed:  
- ESP32 BLE Arduino (built-in)  
- mcp2515 by autowp (install via Library Manager)  
https://github.com/autowp/arduino-mcp2515

## Files in sketch folder:
- EU-Advanced-EAP.ino  
- index_html.h  
- pin_config.h  

## Wiring model YL
- CAN A (MCP2515): connect to the Party CAN bus (2-3)
- CAN B (TWAI): connect to the Body CAN bus (9-10)

## Dashboard Notes

- The Wifi AP name will be something like T2CAN-A1B2 (password: 12345678).  
- Open 192.168.4.1 in a browser.  
- Use the tabs to switch between Advanced-EAP, Nag Echo and Summon Unlock

⚠️ Do not enable TLSSC if you do not have the EAP/FSD subscription. 

## Build firmware using arduino IDE for OTA
- Open Sketch > Export Compiled Binary.
- Open /EU-Advanced-EAP/build/ folder
- Upload EU-Advanced-EAP.ino.bin (+/- 900Ko) using the web dashboard & Update
---  

## Discord server: 
https://discord.gg/euPbYG8Npc

> **Support the project:**  
> [![Buy Me A Coffee](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://buymeacoffee.com/mickymurcid)


## Credits

- Inspired by `Ev Open Can Mod` https://github.com/ev-open-can-tools/ev-open-can-tools
- Created by X₿mod.
- ESP32 TWAI driver by Espressif Systems
- Automotive CAN research community





