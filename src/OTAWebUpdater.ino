#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <EEPROM.h>
#include <../lib/tpl.h>
#include <../lib/eeprom.h>
#include <DallasTemperature.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}
// #include <AsyncMqttClient.h>
TimerHandle_t wifiReconnectTimer;
TimerHandle_t clockUpdateTimer;
TimerHandle_t afterStartupTimer;
TimerHandle_t logStatusTimer;

// #define M5Dev
#ifdef M5Dev
//#include <M5Stack.h>
#endif

#define EEPROM_SIZE		 128	// 128 bytes total
#define EEPROM_RESV_ADDR 0		// first byte reserved for init flag
#define EEPROM_RESV_SIZE 1		// 

#define EEPROM_SSID_ADDR 1		// Bytes 1:32 store wifi SSID
#define EEPROM_SSID_SIZE 32		// 32 bytes long SSID

#define EEPROM_PASS_ADDR 33		// Bytes 33:97 store wifi password
#define EEPROM_PASS_SIZE 64		// 64 bytes long

int TIMEOUT_START = 10;

const int tickMS = 2;
const int sec = int(1000/tickMS);

const char *host = "esp32";
const char *ssid_start = "esp32start";
const char *password_start = "esp32start";

String ssid = "";
String password = "";

volatile int cc = 0;

// RUN MODES 
#define MODE_STARTUP 		1
#define MODE_CONNECTED		2
#define MODE_TIMESET		4
volatile byte mode = MODE_STARTUP;
/*
	Set bit			mode |= MODE_STARTUP; 
	Toggle bit		mode ^= MODE_STARTUP;
	Unset bit		mode &= ~MODE_STARTUP;

	if (mode & MODE_STARTUP) {
		mode ^= MODE_STARTUP;
	}
*/

AsyncWebServer server(80);

#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

#include <ESP32Time.h>
ESP32Time rtc;
const int TIMEZONE_OFFSET = 2*3600; // GMT+2, Kyiv

// =========================================================================

// PINS 
#define ONE_WIRE_BUS 		21 // GPIO 21 (SDA)
#define WIFI_STATUS_LED_PIN 2
#define RELAY_1_PIN 34
#define RELAY_2_PIN 35

// Sensors setup
#define TEMPERATURE_PRECISION 10
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define NUM_SENSORS 2
const DeviceAddress probesAddr[2] = {
	{0x28, 0x7D, 0x10, 0x45, 0x92, 0x0D, 0x02, 0x83},
	{0x28, 0xDF, 0x7B, 0x45, 0x92, 0x18, 0x02, 0xC3},
};

float temperature[NUM_SENSORS];
            // Hour of the day:       0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
static const uint8_t tempProfile[]={ 32,33,34,35,35,35,34,34,33,33,32,32,31,30,29,28,27,26,26,26,27,29,30,31};

uint8_t highestAllowedTemp  = 36;
uint8_t defaultTemp         = 32;
uint8_t targetTemp          = 0;
float currentTemp           = 0;
float lowest                = 1000.0;
float highest               = -1000.0;
float average               = 0.0;

float getTemperature(DeviceAddress deviceAddress) {
	return sensors.getTempC(deviceAddress);
}

void triggerHeater() {
#ifdef SERIAL_DEBUG
	Serial.println("");
	Serial.print(tm.Hour);
	Serial.print(":");
	Serial.print(tm.Minute);
	Serial.print(":");
	Serial.print(tm.Second);

	Serial.println("");
	Serial.print("S1 = ");
	Serial.print(temperature[0]);
	Serial.print(" | S2 = ");
	Serial.print(temperature[1]);
	Serial.print(" | S3 = ");
	Serial.println(temperature[2]);

	Serial.print("cT - ");
	Serial.print(currentTemp);
	Serial.print(" | tT = ");
	Serial.print(targetTemp);
	Serial.print(" | highest = ");
	Serial.print(highest);
	Serial.print(" | max Allowed = ");
	Serial.print(highestAllowedTemp);
#endif

	if ((currentTemp < targetTemp) &&
		(highest < highestAllowedTemp)) {
		
//		digitalWrite(heaterRelayPin, LOW);
//		heaterRelayState = LOW;
		// Serial.println(" | triggerHeater ON");
	}
	else
	{
//		digitalWrite(heaterRelayPin, HIGH);
//		heaterRelayState = HIGH;
		// Serial.println(" | triggerHeater OFF");
	}
}
// =========================================================================

void WiFiEvent(WiFiEvent_t event)
{
	Serial.printf("[WiFi-event] event: %d\n", event);
	switch (event) {
	case SYSTEM_EVENT_STA_GOT_IP:
		Serial.println("WiFi connected");
		Serial.println("IP address: ");
		Serial.println(WiFi.localIP());
		// connectToMqtt();
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		Serial.println("WiFi lost connection");
		// xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		xTimerStart(wifiReconnectTimer, 0);
		xTimerStop(clockUpdateTimer, 0);

		break;
	case SYSTEM_EVENT_STA_CONNECTED:
		xTimerStop(wifiReconnectTimer, 0);
		xTimerStart(clockUpdateTimer, 0);
		break;
	}
}

bool connectToWifi() {
	if ((WiFi.status() != WL_CONNECTED) && ssid.length() > 1 && password.length() > 1) {
		int i = 0;
		WiFi.begin(ssid.c_str(), password.c_str());
		Serial.printf("Connecting to WiFi: %s:%s \n\r", ssid.c_str(), password.c_str());
		while (WiFi.status() != WL_CONNECTED && (i < 10)) {
			i++;
			delay(1 * sec);
			Serial.print(i);
		}
		Serial.printf("WiFi.status(): %d\r\n", WiFi.status());
	}
	return (WiFi.status() == WL_CONNECTED);
}
bool updateClock() {
	if (WiFi.status() == WL_CONNECTED) {	
		if (!timeClient.update()) {
			timeClient.forceUpdate();
		} else {
			rtc.setTime(timeClient.getEpochTime());
			mode |= MODE_TIMESET;
			return true;
		}
	}
	return false;
}

void logStatus() {
		Serial.print(timeClient.getFormattedTime());
		Serial.print(" ");
		// Serial.print("\t mode: ");
		// Serial.print(mode);
		if (MODE_TIMESET == (mode & MODE_TIMESET)) {
			Serial.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
		}

		if (MODE_CONNECTED == (mode & MODE_CONNECTED)) {
		
			// report to the mothership

		}
}
void stopAP() {
	Serial.printf("%d sec timeout reached\r\n", TIMEOUT_START);
	Serial.printf("Stopping softAP...\r\n");
	mode ^= MODE_STARTUP;
	WiFi.softAPdisconnect(true);
	delay(100);
}

void notFound(AsyncWebServerRequest *request) {
	request->send(404, "text/plain", "Not found");
}

void setup() {
	Serial.begin(115200);

	#ifdef M5Dev
	M5.begin();
	M5.Power.begin();
	#endif

	WiFi.mode(WIFI_MODE_APSTA);
	WiFi.softAP(ssid_start, password_start);
	WiFi.softAPsetHostname(host);

	IPAddress IP = WiFi.softAPIP();
	Serial.printf("AP IP address: %s \r\n", IP.toString().c_str());

	if (MDNS.begin(host)) {
		Serial.println("mDNS responder started");
	}

	EEPROM.begin(EEPROM_SIZE);
	delay(100);

	ssid = eepromReadString(EEPROM_SSID_ADDR, EEPROM_SSID_SIZE);
	Serial.printf("SSID from EEPROM: %s\r\n", ssid.c_str());

	password = eepromReadString(EEPROM_PASS_ADDR, EEPROM_PASS_SIZE);
	Serial.printf("PASS from EEPROM: %s\r\n", password.c_str());

	wifiReconnectTimer = xTimerCreate("wifiReconnectTimer", pdMS_TO_TICKS(1000), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
	xTimerStart(wifiReconnectTimer, 0);
	clockUpdateTimer = xTimerCreate("clockUpdateTimer", pdMS_TO_TICKS(2000), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(updateClock));
	xTimerStart(clockUpdateTimer, 0);
	afterStartupTimer = xTimerCreate("afterStartupTimer", pdMS_TO_TICKS(TIMEOUT_START*1000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(stopAP));
	xTimerStart(afterStartupTimer, 0);
	logStatusTimer = xTimerCreate("logStatusTimer", pdMS_TO_TICKS(1000), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(logStatus));
	xTimerStart(logStatusTimer, 0);

	WiFi.onEvent(WiFiEvent);

	timeClient.begin();
	timeClient.setTimeOffset(TIMEZONE_OFFSET);

	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
			  { request->redirect("/loginForm"); 
	});

	server.on("/stayPut", HTTP_GET, [](AsyncWebServerRequest *request) {
		Serial.println("Staying in startup mode for 10 more minutes. Enjoy!");
		TIMEOUT_START = 600 * sec;
		request->redirect("/login");
	});

	// handle GET request to <host>/setCredentials?ssid=<ssid>&password=<password>
	server.on("/setCredentials", HTTP_GET, [](AsyncWebServerRequest *request) {
		if (request->hasParam("ssid") && request->hasParam("password")) {
			
			ssid = request->getParam("ssid")->value();
			password = request->getParam("password")->value();
			Serial.printf("Setting credentials. %s : %s", ssid.c_str(), password.c_str());
			if (ssid.length() > 0 && password.length() > 0) {
				eepromClear(0, EEPROM_SIZE);
				eepromWriteString(EEPROM_SSID_ADDR, ssid);
				eepromWriteString(EEPROM_PASS_ADDR, password);
				Serial.printf("%s:%s credentials set. Rebooting in 10 sec...", ssid.c_str(), password.c_str());
			}

			request->send(200, "text/plain", "New credentials set");
			delay(10*sec);
			// request->redirect("/");
			ESP.restart();
		} else {
			request->send(200, "text/plain", "empty ssid or password");
			// request->redirect("/");
		} 
	});

	server.on("/loginForm", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send(200, "text/html", loginIndex); 
	});

	// =========================================================================

	pinMode(WIFI_STATUS_LED_PIN, OUTPUT);
	pinMode(RELAY_1_PIN, OUTPUT);
	pinMode(RELAY_2_PIN, OUTPUT);

	sensors.begin();

	for (int i = 0; i < NUM_SENSORS; i++) {
		sensors.setResolution(probesAddr[i], TEMPERATURE_PRECISION);
	}
	// =========================================================================

	server.onNotFound(notFound);

	AsyncElegantOTA.begin(&server);
	server.begin();
}

void loop(void) {
	cc++;




	// =========================================================================


	// every second
	if (!(cc % sec)) {

		sensors.requestTemperatures();

		for (int i = 0; i < NUM_SENSORS; i++) {
			// temperature[i] = getTemperature(probesAddr[i]);
			if (temperature[i] == -127.0) {
				temperature[i] = 0;
			}
		}

		lowest = 1000.0;
		highest = -1000.0;
		average = 0.0;
		int measures = 0;
		for (int i = 0; i < NUM_SENSORS; i++) {
			if (temperature[i] != 0) {
				measures++;
				average += temperature[i];

				if (temperature[i] < lowest)
					lowest = temperature[i];

				if (temperature[i] > highest)
					highest = temperature[i];
			}
		}
		if (measures > 0)
			average /= measures;

		currentTemp = average;

		triggerHeater();
		// triggerLed();


	}

	// Indicate WiFi connection with a blue LED on WROOM board 
	if (!(cc % sec)) {
		if (mode & MODE_CONNECTED) {
			digitalWrite(WIFI_STATUS_LED_PIN, HIGH);			
		} else {
			digitalWrite(WIFI_STATUS_LED_PIN, LOW);
		}
	}

	// =========================================================================
	
	delay(tickMS);
}

// http://esp32.local/setCredentials?ssid=guesto&password=password123