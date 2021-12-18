#include <config.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DS18B20.h>

#include <../lib/tpl.h>
#include <../lib/eeprom.h>
#include <../lib/mqtt.h>


extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}

TimerHandle_t wifiReconnectTimer;
TimerHandle_t clockUpdateTimer;
TimerHandle_t afterStartupTimer;
TimerHandle_t logStatusTimer;
TimerHandle_t pidControlLoopTimer;

// #define M5Dev
#ifdef M5Dev
#include <M5Stack.h>
#endif

#define EEPROM_SIZE		 128	// 128 bytes total
#define EEPROM_RESV_ADDR 0		// first byte reserved for init flag
#define EEPROM_RESV_SIZE 1		// 

#define EEPROM_SSID_ADDR 1		// Bytes 1:32 store wifi SSID
#define EEPROM_SSID_SIZE 32		// 32 bytes long SSID

#define EEPROM_PASS_ADDR 33		// Bytes 33:97 store wifi password
#define EEPROM_PASS_SIZE 64		// 64 bytes long

int TIMEOUT_START = 10;

const int tickMS = 1;
const int sec = int(1000/tickMS); // msec/sec ratio

String ssid = "";
String password = "";

// RUN MODES 
#define MODE_STARTUP 				1
#define MODE_CONNECTED				2
#define MODE_TIMESET				4
#define MODE_RSRVD8				 	8
#define MODE_RSRVD16				16
#define MODE_CONTROL_LOOP			32
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

// =========================================================================

// PINS 
#define ONE_WIRE_BUS 		21 // GPIO 21
#define WIFI_STATUS_LED_PIN 2

DS18B20 ds(ONE_WIRE_BUS);

            // Hour of the day:       0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
static const uint8_t tempProfile[]={ 32,33,34,35,35,35,34,34,33,33,32,32,31,30,29,28,27,26,26,26,27,29,30,31};

uint8_t highestAllowedTemp  = 36;
uint8_t prevTemp            = 0;
uint8_t targetTemp          = 0;
float currentTemp           = 0;
float lowest                = 1000.0;
float highest               = -1000.0;
float average               = 0.0;

byte heaterState = LOW;
void heaterActivate();
void heaterDeactivate();

         // Hour of the day:       0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
static const byte lightProfile[]={ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
byte lightState = LOW;
void lightActivate();
void lightDeactivate();

char buffer[256];
char topicbuf[256];
// =========================================================================

void WiFiEvent(WiFiEvent_t event) {
	Serial.printf("[WiFi-event] event: %d\n", event);
	switch (event) {
	case SYSTEM_EVENT_STA_GOT_IP:
		Serial.println("WiFi connected");
		Serial.println("IP address: ");
		Serial.println(WiFi.localIP());
		connectToMqtt();
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		Serial.println("WiFi lost connection");
		xTimerStart(wifiReconnectTimer, 0);
		xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		xTimerStop(clockUpdateTimer, 0);
		break;
	case SYSTEM_EVENT_STA_CONNECTED:
		xTimerStop(wifiReconnectTimer, 0);
		xTimerStart(mqttReconnectTimer, 0);
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

	// Indicate WiFi connection with a blue LED on WROOM board 
	digitalWrite(WIFI_STATUS_LED_PIN, (WiFi.status() == WL_CONNECTED)?HIGH:LOW);

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

uint16_t msgMQTT(String topic, String message) {
	return msgMQTT(topic.c_str(), message.c_str());
}
uint16_t msgMQTT(const char* topic, const char* message) {
	if (mqttClient.connected()) {
		return mqttClient.publish(topic, 2, true, message);
	}
	return 0;
}

uint16_t logMQTT(String topic, String message) {
	return logMQTT(topic.c_str(), message.c_str());
}
uint16_t logMQTT(const char* topic, const char* message) {
	sprintf(topicbuf, "%s/%s",MQTT_IDENT, topic);
	return msgMQTT(topicbuf, message);
}

void logStatus() {

	Serial.print(timeClient.getFormattedTime());
	Serial.print(" ");
	if (MODE_TIMESET == (mode & MODE_TIMESET)) {
		Serial.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));

		// CSV Line
		// Y-m-d H:m:s ; T lowest; T highest; T average; T target; heater state; light state
		sprintf(buffer, "%s; %3.2f; %3.2f; %3.2f; %d; %d; %d", 
			rtc.getTime("%Y-%m-%d %H:%M:%S").c_str(),
			lowest,
			highest,
			average,
			targetTemp,
			heaterState,
			lightState
		);
		logMQTT("csvLog", buffer);

		sprintf(buffer, "%d", ESP.getFreeHeap());
		logMQTT("freeHeap", buffer);
	}
}

void afterStartup() {
	stopAP();
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

	// #ifdef M5Dev
	// M5.begin();
	// M5.Power.begin();
	// #endif

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

	wifiReconnectTimer = xTimerCreate("wifiReconnectTimer", pdMS_TO_TICKS(1*sec), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
	xTimerStart(wifiReconnectTimer, 0);

	clockUpdateTimer = xTimerCreate("clockUpdateTimer", pdMS_TO_TICKS(2*sec), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(updateClock));
	xTimerStart(clockUpdateTimer, 0);

	afterStartupTimer = xTimerCreate("afterStartupTimer", pdMS_TO_TICKS(TIMEOUT_START*sec), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(stopAP));
	xTimerStart(afterStartupTimer, 0);

	logStatusTimer = xTimerCreate("logStatusTimer", pdMS_TO_TICKS(6*sec), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(logStatus));
	xTimerStart(logStatusTimer, 0);

	mqttReconnectTimer = xTimerCreate("mqttReconnectTimer", pdMS_TO_TICKS(2*sec), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));

	pidControlLoopTimer = xTimerCreate("pidControlLoop", pdMS_TO_TICKS(TIMEOUT_PID_CONTROL*sec), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(enterPidControlLoop));
	xTimerStart(pidControlLoopTimer, 0);
	
	mqttClient.onConnect(onMqttConnect);
	mqttClient.onDisconnect(onMqttDisconnect);
	mqttClient.onSubscribe(onMqttSubscribe);
	mqttClient.onUnsubscribe(onMqttUnsubscribe);
	mqttClient.onMessage(onMqttMessage);
	mqttClient.onPublish(onMqttPublish);
	mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
	mqttClient.setServer(MQTT_HOST, MQTT_PORT);

	WiFi.onEvent(WiFiEvent);

	timeClient.begin();
	timeClient.setTimeOffset(TIMEZONE_OFFSET);

	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
			  { request->redirect("/loginForm"); 
	});

	server.on("/stayPut", HTTP_GET, [](AsyncWebServerRequest *request) {
		Serial.println("API endpoint for testing purposes. Enjoy!");

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
	pinMode(PIN_HEATER_RELAY, OUTPUT);
	pinMode(PIN_LIGHT_RELAY, OUTPUT);

	// =========================================================================

	server.onNotFound(notFound);

	AsyncElegantOTA.begin(&server);
	server.begin();
}

// Implement mqtt messages handlers
void handleMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {

	// custom handlers

}

void enterPidControlLoop() {
	mode |= MODE_CONTROL_LOOP; 
}

void pidControlLoop() {

	int currentHour = rtc.getHour(true);

	sprintf(buffer, "Devices found: %d", ds.getNumberOfDevices());
	logMQTT("log", buffer);

	sprintf(buffer, "tempProfile[%d]: %d", currentHour, tempProfile[currentHour]);
	logMQTT("log", buffer);

	// === Probing
	lowest = 1000.0;
	highest = -1000.0;
	average = 0.0;
	int probes = 0;
	while (ds.selectNext()) {
		float temp = ds.getTempC();

		average += temp;

		if (temp < lowest) {
			lowest = temp;
		}

		if (temp > highest) {
			highest = temp;
		}

		sprintf(buffer, "Sensor %d. Temp C: %5.3f", probes, temp);
		logMQTT("log", buffer);
		
		sprintf(buffer, "%5.3f", temp);
		logMQTT(String("p/ds18b20/"+String(probes)), buffer);
		msgMQTT(String("croco/cage/raw/ds18b20/"+String(probes)), buffer);
		
		probes++;
	}
	if (probes > 0) {
		average /= probes;
	} else {
		average = 0;
	}

	currentTemp = average;

	sprintf(buffer, "%5.3f", currentTemp);
	msgMQTT("croco/cage/temperature", buffer);

	targetTemp = tempProfile[currentHour];

	sprintf(buffer, "%d", targetTemp);
	msgMQTT("croco/cage/targetTemperature", buffer);

	// === Controlling 

	if ((probes > 0) &&						// Mustn't decide to activate heaters without sensors feedback
		(currentTemp < targetTemp) &&		// Goal is to reach targetTemp
		(highest < highestAllowedTemp)) {	// Mustn't exceed highest allowed temp at any sensor
			heaterActivate();
	} else {
			heaterDeactivate();
	}
}

void heaterActivate() {
	digitalWrite(PIN_HEATER_RELAY, HIGH);
	heaterState = HIGH;
	logMQTT("log", "Heater activated");
	msgMQTT("croco/cage/heater", "1");
}

void heaterDeactivate() {
	digitalWrite(PIN_HEATER_RELAY, LOW);
	heaterState = LOW;
	logMQTT("log", "Heater deactivated");
	msgMQTT("croco/cage/heater", "0");
}

void lightControlLoop() {
	if (1 == lightProfile[rtc.getHour(true)]) {
		lightActivate();
	} else {
		lightDeactivate();
	}
	sprintf(buffer, "pin/%d/set", PIN_LIGHT_RELAY);
	logMQTT(buffer, String(lightState));
}

void lightActivate() {
	digitalWrite(PIN_LIGHT_RELAY, HIGH);
	if (lightState == LOW) {
		logMQTT("log", "Light activated");
		msgMQTT("croco/cage/light", "1");
		lightState = HIGH;
	}
}

void lightDeactivate() {
	digitalWrite(PIN_LIGHT_RELAY, LOW);
	if (lightState == HIGH) {
		logMQTT("log", "Light deactivated");
		msgMQTT("croco/cage/light", "0");
		lightState = LOW;
	}
}

void loop(void) {

	if (mode & MODE_CONTROL_LOOP) {
		mode ^= MODE_CONTROL_LOOP;
		pidControlLoop();
		lightControlLoop();
	}

}

// http://esp32.local/setCredentials?ssid=guesto&password=password123