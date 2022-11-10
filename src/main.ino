#include <main_config.h>

#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DS18B20.h>

#include <../lib/eeprom.h>
#include <../lib/tpl.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}

const int tickMS = 1;
const int sec = int(1000/tickMS); // msec/sec ratio

TimerHandle_t timerSeconds;
TimerHandle_t timerAfterStartup;
TimerHandle_t timerLogStatus;
TimerHandle_t timerControl;
TimerHandle_t timerMqttMaintainConnect;

// RUN MODES 
#define MODE_STARTUP 				1
#define MODE_LOG					2
#define MODE_TIMESET				4
#define MODE_RSRVD				 	8
#define MODE_SECONDS_LOOP			16 // every second loop
#define MODE_CONTROL_LOOP			32 // every TIMEOUT_PID_CONTROL seconds loop
#define MODE_EXAMPLE				64
volatile byte mode = MODE_STARTUP;
/*
	Set bit			mode |= MODE_EXAMPLE; 
	Toggle bit		mode ^= MODE_EXAMPLE;
	Unset bit		mode &= ~MODE_EXAMPLE;

	if (mode & MODE_EXAMPLE) {
		mode ^= MODE_EXAMPLE;
	}
*/

AsyncWebServer server(80);

#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

#include <ESP32Time.h>
ESP32Time rtc;

String ssid = "";
String password = "";

#include <AsyncMqttClient.h>
AsyncMqttClient mqttClient;

char buffer[256];
char topicbuf[256];

// =========================================================================
// PINS 
#define ONE_WIRE_BUS 		21
#define WIFI_STATUS_LED_PIN 2

DS18B20 ds(ONE_WIRE_BUS);

            // Hour of the day:       0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
static const uint8_t tempProfile[]={ 32,33,34,35,35,35,34,34,33,33,32,32,31,30,29,28,27,26,26,26,27,29,30,31};

uint8_t highestAllowedTemp  = 36;
uint8_t targetTemp          = 0;
float currentTemp           = 0;
float lowest                = 99.0;
float highest               = -1.0;
float average               = 0.0;

long TEMP_TTL = 60; //seconds temp reading is still fresh
volatile long lastValidReadingTimestamp = 0;

#define TEMPH_SIZE 10 // TEMPerature History SIZE
float tempHist[TEMPH_SIZE];

byte heaterState = LOW;
void heaterActivate();
void heaterDeactivate();

         // Hour of the day:       0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23
static const byte lightProfile[]={ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
byte lightState = LOW;
void lightActivate();
void lightDeactivate();
// =========================================================================

void WiFiEvent(WiFiEvent_t event) {
	Serial.printf("[WiFi-event] event: %d\n", event);
	switch (event) {
	case SYSTEM_EVENT_STA_GOT_IP:
		Serial.println("WiFi connected, got IP: ");
		Serial.println(WiFi.localIP());
		xTimerStart(timerMqttMaintainConnect, 1*sec);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		Serial.println("WiFi lost connection");
		xTimerStop(timerMqttMaintainConnect, 0);
		break;
	case SYSTEM_EVENT_STA_CONNECTED:
		Serial.println("WiFi connected");
		break;
	}
}

void stopAP() {
	Serial.printf("%d sec timeout reached\r\n", TIMEOUT_START);
	Serial.printf("Stopping softAP...\r\n");
	mode ^= MODE_STARTUP;
	WiFi.softAPdisconnect(true);
}

void notFound(AsyncWebServerRequest *request) {
	request->send(404, "text/plain", "Not found");
}

void setup() {
	Serial.begin(115200);

	WiFi.disconnect(true);

	uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
	uint16_t chip = (uint16_t)(chipid >> 32);
	sprintf(IDENT, "ESP32-%04X%08X", chip, (uint32_t)chipid);
	sprintf(buffer, "IDENT: %s", IDENT);
	Serial.println(buffer);

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

	timerAfterStartup = xTimerCreate("timerAfterStartup", pdMS_TO_TICKS(TIMEOUT_START*sec), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(stopAP));
	xTimerStart(timerAfterStartup, TIMEOUT_START*sec);

	timerSeconds = xTimerCreate("timerSeconds", pdMS_TO_TICKS(1*sec), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(timerSecondsHandler));
	xTimerStart(timerSeconds, 15*sec);

	timerLogStatus = xTimerCreate("timerLogStatus", pdMS_TO_TICKS(TIMEOUT_LOG_STATUS*sec), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(timerLogStatusHandler));
	xTimerStart(timerLogStatus, 20*sec);

	timerControl = xTimerCreate("timerControl", pdMS_TO_TICKS(TIMEOUT_CONTROL*sec), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(timerControlHandler));
	xTimerStart(timerControl, 22*sec);

	timerMqttMaintainConnect = xTimerCreate("timerMqttMaintainConnect", pdMS_TO_TICKS(10*sec), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(timerMqttMaintainConnectHandler));

	WiFi.begin(ssid.c_str(), password.c_str());
	WiFi.onEvent(WiFiEvent);
	WiFi.waitForConnectResult();
	Serial.println(WiFi.dnsIP());
	WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), IPAddress(8,8,8,8)); 
	delay(10);
	Serial.println(WiFi.dnsIP());
	
	mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
	mqttClient.setServer(MQTT_HOST, MQTT_PORT);

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

	Serial.print("MQTT_IDENT: ");
	Serial.println(IDENT);
	// =========================================================================

	server.onNotFound(notFound);

	AsyncElegantOTA.begin(&server);
	server.begin();

	timeClient.begin();
	timeClient.setTimeOffset(TIMEZONE_OFFSET);
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
	sprintf(topicbuf, "%s/%s",IDENT, topic);
	return msgMQTT(topicbuf, message);
}

void timerMqttMaintainConnectHandler() {
	if (WiFi.isConnected() && !mqttClient.connected()) {
		mqttClient.connect();
	}
}

void timerLogStatusHandler() {
	mode |= MODE_LOG;
}

void loopLog() {
	Serial.println("loopLog(): called");

	// CSV Line
	// Y-m-d H:m:s ; T lowest; T highest; T moving average; T fib MA 10; T target; heater state; light state
	sprintf(buffer, "%s; %2.3f; %2.3f; %2.3f; %d; %d; %d", 
		rtc.getTime("%Y-%m-%d %H:%M:%S").c_str(),
		highest,
		getWeighedMA5Temp(),
		getFibWeighedMA10Temp(),
		targetTemp,
		heaterState,
		lightState
	);
	logMQTT("csvLog", buffer);

	sprintf(buffer, "%d", ESP.getFreeHeap());
	logMQTT("freeHeap", buffer);
	sprintf(buffer, "%ld", secSinceValidReading());
	logMQTT("sinceLastValidReading", buffer);
}


// isTempValid - basic temperature readings validation
bool isTempValid(float temp) {
	if (
		(temp > 9) &&
		(temp < 50)
	) {
		return true;
	}
	return false;
}

// returns number of seconds from last valid temperature reading
long secSinceValidReading() {
	if ((lastValidReadingTimestamp != 0) && 
		(MODE_TIMESET == (mode & MODE_TIMESET))) {

		return rtc.getEpoch() - lastValidReadingTimestamp;
	}
	return LONG_MAX;
}

// getWeighedMA5Temp - returns moving average temperature based on 5 last readings
// most recent readings are x5, x3, x2 their respective weights
float getWeighedMA5Temp() {
	return (
		tempHist[0] * 5 + 
		tempHist[1] * 3 + 
		tempHist[2] * 2 + 
		tempHist[3] * 1 + 
		tempHist[4] * 1  
	) / 12;
}

// getWeighedMATemp - returns weighed moving average temperature over 10 last readings max
// weights are Fibonacci-weighed so the recent ones are heavier
float getFibWeighedMA10Temp() {
	return (
		tempHist[0] * 55 + 
		tempHist[1] * 34 + 
		tempHist[2] * 21 + 
		tempHist[3] * 13 + 
		tempHist[4] * 8 + 
		tempHist[5] * 5 + 
		tempHist[6] * 3 + 
		tempHist[7] * 2 + 
		tempHist[8] * 1 + 
		tempHist[9] * 1  
	) / 143;
}

// get moving average temperature over `n` last records
float getMATemp(int n=0) {
	float sum = 0;

	n = min(max(1,n), TEMPH_SIZE); // make sure 0<n<=TEMPH_SIZE
	
	for (int i=0; i<n; i++) {
		sum+=tempHist[i];
	}
	return sum/n;
}

// probeTemperature - poll the probes, save results
float probeTemperature() {
	highest = -1.0;
	int probes = 0;

	ds.resetSearch();
	while (ds.selectNext()) {
		float temp = ds.getTempC();

		if (isTempValid(temp)) {
			for (int i=0; i < (TEMPH_SIZE-1); i++) {
				tempHist[i+1] = tempHist[i];
			}
			tempHist[0] = temp;

			if (temp > highest) {
				highest = temp;
			}

			if (MODE_TIMESET == (mode & MODE_TIMESET)) {
				lastValidReadingTimestamp = rtc.getEpoch();
			}
			probes++;
		}

		sprintf(topicbuf, "%s/p/ds18b20/%d", IDENT, probes);
		sprintf(buffer, "%2.3f", temp);
		msgMQTT(topicbuf, buffer);
	}

	return (probes > 0);
}

float getCurrentTemperature() {
	return getFibWeighedMA10Temp();
}


void heaterActivate() {
	if (LOW == heaterState) {
		heaterState = HIGH;
		digitalWrite(PIN_HEATER_RELAY, HIGH);
		logMQTT("log", "Heater activated");
		msgMQTT("croco/cave/heater", "1");
	} // else - already HIGH
}

void heaterDeactivate() {
	if (HIGH == heaterState) {
		heaterState = LOW;
		digitalWrite(PIN_HEATER_RELAY, LOW);
		logMQTT("log", "Heater deactivated");
		msgMQTT("croco/cave/heater", "0");
	} // else - already LOW
}

void lightActivate() {
	digitalWrite(PIN_LIGHT_RELAY, HIGH);
	if (lightState == LOW) {
		logMQTT("log", "Light activated");
		msgMQTT("croco/cave/light", "1");
		lightState = HIGH;
	}
}

void lightDeactivate() {
	digitalWrite(PIN_LIGHT_RELAY, LOW);
	if (lightState == HIGH) {
		logMQTT("log", "Light deactivated");
		msgMQTT("croco/cave/light", "0");
		lightState = LOW;
	}
}

void timerSecondsHandler() {
	mode |= MODE_SECONDS_LOOP;
}

void loopSeconds() {
	rtc.setTime(timeClient.getEpochTime());
	probeTemperature();
	Serial.println(rtc.getTime("%Y-%m-%d %H:%M:%S").c_str());
}

void timerControlHandler() {
	mode |= MODE_CONTROL_LOOP;
}

void loopControl() {
	// Serial.println("loopControl(): called");
	int currentHour = rtc.getHour(true);

	// sprintf(buffer, "Devices found: %d", ds.getNumberOfDevices());
	// logMQTT("log", buffer);

	// sprintf(buffer, "tempProfile[%d]: %d", currentHour, tempProfile[currentHour]);
	// logMQTT("log", buffer);

	sprintf(buffer, "%2.3f", getMATemp());
	logMQTT("getMATemp", buffer);

	// sprintf(buffer, "%2.3f", getFibWeighedMA10Temp());
	// logMQTT("getFibWeighedMA10Temp", buffer);

	sprintf(buffer, "%2.3f", getWeighedMA5Temp());
	logMQTT("getWeighedMA5Temp", buffer);

	sprintf(buffer, "%2.3f", currentTemp);
	msgMQTT("croco/cave/temperature", buffer);

	currentTemp = getCurrentTemperature();

	// === Controlling 
	targetTemp = tempProfile[currentHour];

	sprintf(buffer, "%d", targetTemp);
	msgMQTT("croco/cave/targetTemperature", buffer);

	if (
		// (secSinceValidReading() < TEMP_TTL) && 	// temp reading is still fresh
		(currentTemp < targetTemp) &&			// Goal is to reach targetTemp
		(highest < highestAllowedTemp)) {		// Mustn't exceed highest allowed temp at any sensor
			heaterActivate();
	} else {
			heaterDeactivate();
	}

	if (1 == lightProfile[rtc.getHour(true)]) {
		lightActivate();
	} else {
		lightDeactivate();
	}
}

void loop(void) {

	if (mode & MODE_CONTROL_LOOP) {
		mode ^= MODE_CONTROL_LOOP;
		loopControl();
	}

	if (mode & MODE_SECONDS_LOOP) {
		mode ^= MODE_SECONDS_LOOP;
		loopSeconds();
	}

	if (mode & MODE_LOG) {
		mode ^= MODE_LOG;
		loopLog();
	}

	if (WiFi.isConnected() && timeClient.update()) {
		mode |= MODE_TIMESET;
	}

	// do the most basic down to earth 1 minute loop to self-check:
	// timers are running
	// lastValidTemp is fresh
	// etc.
}