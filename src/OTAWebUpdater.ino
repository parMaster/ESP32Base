#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <EEPROM.h>
#include <../lib/tpl.h>
#include <../lib/eeprom.h>

// #define M5Dev
#ifdef M5Dev
//#include <M5Stack.h>
#endif

#define EEPROM_SIZE		 128	// 128 bytes total
#define EEPROM_RESV_ADDR 0		// first byte reserved for init flag
#define EEPROM_SSID_ADDR 1		// Bytes 1-32 store wifi SSID
#define EEPROM_SSID_SIZE 32		// 32 bytes long SSID
#define EEPROM_PASS_ADDR 33		// Bytes 33-99 store wifi password
#define EEPROM_PASS_SIZE 64		// 64 bytes long

int TIMEOUT_START = 120;

const int tickMS = 1;
const int sec = 1000/tickMS;

const char *host = "esp32";
const char *ssid_start = "esp32start";
const char *password_start = "esp32start";

String ssid = "";
String password = "";

/* 

*/

#define WIFI_STATUS_LED_PIN 2

volatile int cc = 0;

const int MODE_STARTUP = 0;
const int MODE_CONFIGURED /* ??? */ = 1;
volatile byte mode = MODE_STARTUP;

AsyncWebServer server(80);

// Time related libs 
#include <NTPClient.h>
#include <WiFiUdp.h>
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

#include <ESP32Time.h>
ESP32Time rtc;
bool rtcSet = false;
const int TIMEZONE_OFFSET = 2*3600; // GMT+2, Kyiv

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

	if (ssid.length() > 1 && password.length() > 1) {
		WiFi.begin(ssid.c_str(), password.c_str());

		int i = 0;
		Serial.printf("Connecting to WiFi: %s:%s \n\r", ssid.c_str(), password.c_str());
		while (WiFi.status() != WL_CONNECTED && (i < 10)) {
			i++;
			delay(1 * sec);
			Serial.print(i);
		}
		
		Serial.printf("WiFi.status(): %d\r\n", WiFi.status());
	}

	pinMode(WIFI_STATUS_LED_PIN, OUTPUT);

	timeClient.begin();
	timeClient.setTimeOffset(TIMEZONE_OFFSET);

	server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
			request->redirect("/loginForm");
	});

	server.on("/stayPut", HTTP_GET, [](AsyncWebServerRequest *request) {
		Serial.println("Staying in startup mode for 10 more minutes. Enjoy!");
		TIMEOUT_START = 600 * sec;
		request->redirect("/login");
	});

	// handle GET request to <host>/setCredentials?ssid=<ssid>&password=<password>
	server.on("/setCredentials", HTTP_GET, [] (AsyncWebServerRequest *request) {
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
	
	server.on("/loginForm", HTTP_GET, [] (AsyncWebServerRequest *request) {
		request->send(200, "text/html", loginIndex);
	});

	server.onNotFound(notFound);
	
	AsyncElegantOTA.begin(&server);
	server.begin();
}

void loop(void) {
	cc++;
	if (!(cc % sec)) {
		Serial.print(timeClient.getFormattedTime());
		Serial.print(" - ");
		if (rtcSet) {
			Serial.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
		}
	}

	if ((cc==1) || !(cc %(10*sec))) {
		if (WiFi.status() == WL_CONNECTED) {
			
			digitalWrite(WIFI_STATUS_LED_PIN, HIGH);
			if (!timeClient.update()) {
				timeClient.forceUpdate();
			} else {
				rtc.setTime(timeClient.getEpochTime());
				rtcSet = true;
			}
		} else {
			digitalWrite(WIFI_STATUS_LED_PIN, LOW);
		}
	}

	if (mode == MODE_STARTUP && !(cc % sec) && ! (cc % (sec*TIMEOUT_START))) {
		Serial.printf("%d sec timeout reached\r\n", TIMEOUT_START);
		mode = MODE_CONFIGURED;
		WiFi.softAPdisconnect(true);
		// WiFi.disconnect(true);
	}

	if (mode == MODE_STARTUP) {
		// startupServer.handleClient();
	}
	
	if (mode == MODE_CONFIGURED) {
		// server.handleClient();
	}

	delay(tickMS);
}