#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>


#include <Update.h>
#include <../lib/tpl.h>

int TIMEOUT_START = 30;

const int sec = 800;

const char *host = "esp32";
const char *ssid_start = "esp32start";
const char *password_start = "esp32start";

String ssid = "esp32main";
String password = "esp32main";

volatile int cc = 0;

const int MODE_STARTUP = 0;
const int MODE_CONFIGURED /* ??? */ = 1;

volatile byte mode = MODE_STARTUP;

AsyncWebServer server(80);

void onRequest(AsyncWebServerRequest *request){
  //Handle Unknown Request
  request->send(404);
}

void onBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
  //Handle body
}

void onUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  //Handle upload
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void setup() {
	Serial.begin(115200);

	// WiFi.mode(WIFI_AP);
	// WiFi.softAP(host);
	WiFi.softAP(ssid_start, password_start);
	WiFi.softAPsetHostname(host);
	
	IPAddress IP = WiFi.softAPIP();
	Serial.print("AP IP address: ");
	Serial.println(IP);
	
	if (MDNS.begin(host)) {
		Serial.println("mDNS responder started");
	}

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
			Serial.println(ssid + ":" + password + " credentials set");
			request->send(200, "text/plain", ssid + ":" + password + " credentials set");
			// request->redirect("/");
		} else {
			request->send(200, "text/plain", "empty ssid or password");
			// request->redirect("/");
		}
	});
	// handle GET request to <host>/setCredentials?ssid=<ssid>&password=<password>
	server.on("/loginForm", HTTP_GET, [] (AsyncWebServerRequest *request) {
		request->send(200, "text/html", loginIndex);
	});

	server.onNotFound(notFound);

	server.begin();

}

void loop(void) {

		cc++;
		if (!(cc % sec)) {
			Serial.println(String(cc / sec) + " sec \t "+String(mode)+" mode \t");
		}

		if (mode == MODE_STARTUP && !(cc % sec) && ! (cc % (sec*TIMEOUT_START))) {
			Serial.println(String(TIMEOUT_START) + "sec timeout reached");
			mode = MODE_CONFIGURED;
			// switchMode();
		}

		if (mode == MODE_STARTUP) {
			// startupServer.handleClient();
		}
		
		if (mode == MODE_CONFIGURED) {
			// server.handleClient();
		}

		delay(1);
}
