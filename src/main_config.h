const int TIMEZONE_OFFSET_DST = 3*3600; // GMT+3, Kyiv
const int TIMEZONE_OFFSET = 2*3600; // GMT+2, Kyiv
const int TIME_UPDATE_INTERVAL = 10*60; // sec; 10 min

// Soft AP credentials. Available until TIMEOUT_START is reached
const char *host = "esp32";
const char *ssid_start = "esp32start";
const char *password_start = "esp32start";
int TIMEOUT_START = 360;

int TIMEOUT_LOG_STATUS = 60; 		// sec; logStatus calling interval

const int TIMEOUT_CONTROL = 10;		// sec; interval for "COMMAND" lop - i.e. PID loop

char IDENT[18]; 					// aka MAC address 
									// M5: ESP32-344E3D8E0D84
									// CAM:
									// DOIT: ESP32-A473F53A7D80

// MQTT broker credentials
#define MQTT_HOST	"mqtt.server"
#define MQTT_PORT	1883
#define MQTT_USER	"login"
#define MQTT_PASS	"pass"

// EEPROM data mapping
#define EEPROM_SIZE		 128	// 128 bytes total
#define EEPROM_RESV_ADDR 0		// first byte reserved for init flag
#define EEPROM_RESV_SIZE 1		// 

#define EEPROM_SSID_ADDR 1		// Bytes 1:32 store wifi SSID
#define EEPROM_SSID_SIZE 32		// 32 bytes long SSID

#define EEPROM_PASS_ADDR 33		// Bytes 33:97 store wifi password
#define EEPROM_PASS_SIZE 64		// 64 bytes long

// ======== APP config
// PID controls connection pins
#define PIN_HEATER_RELAY		33
#define PIN_LIGHT_RELAY			32
