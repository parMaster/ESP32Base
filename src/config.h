const int TIMEZONE_OFFSET = 2*3600; // GMT+2, Kyiv

const int TIMEOUT_PID_CONTROL = 5; // sec - interval between PID control loop polls/commands

// ESP32 temp soft access point credentials
const char *host = "esp32";
const char *ssid_start = "esp32start";
const char *password_start = "esp32start";

// MQTT broker credentials
#define MQTT_HOST	"mqtt.cdns.com.ua"
#define MQTT_PORT	1883
#define MQTT_IDENT	"esp32base"
#define MQTT_USER	"gusto"
#define MQTT_PASS	""

#define PIN_HEATER_RELAY		33
#define PIN_LIGHT_RELAY			32