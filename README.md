# ESP32Base

## Create a core functionality blank for ESP32 to build IoT devices right away

MUST haves:
- Async web server
- OTA Update (AsyncElegantOTA)
- WiFi access point for limited time after device startup - providing a web form to enter Internet AP credentials.
- Storing credentials to eeprom with _simple and clear API_
- **Occasional Online** mode - Internet connection can't be relied upon, device must manage reconnects to WiFi AP.

Good to haves:p
- As close to RTC as possible, even after multi-day offline modes. Without RTC, with occasional NTP availability
- Easily compilable and deployable examples
- Telegram bot interface

TBD:
- web sockets (how often an IoT device needs it?)
