# ESP32Base

## ESP32 blank to build IoT devices

MUST haves:
- Async web server
- OTA Update (AsyncElegantOTA)
- WiFi access point for limited time after device startup - providing a web form to enter Internet AP credentials.
- Storing credentials to eeprom with _simple and clear API_
- **Occasional Online** mode - Internet connection can't be relied upon, device must manage reconnects to WiFi AP.
- No ESP-IDF, only VSCode and PlatformIO required

Good to haves:
- As close to RTC as possible, even after multi-day offline modes. Without RTC, with occasional NTP availability
- Easily compilable and deployable examples
- MQTT interface with authentification
- Total control with a Telegram bot MQTT client
