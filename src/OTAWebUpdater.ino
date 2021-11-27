#include <WiFi.h>
#include <WiFiClient.h>
#include <Webserver.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <../lib/tpl.h>

const int sec = 1000;

const char* host = "esp32";
const char* ssid_start = "esp32start";
const char* password_start ="esp32start";
WebServer serverUpdater(8080);

char* ssid = "esp32start";
char* password ="esp32start";
WebServer server(80);

/*
 * setup function
 */
void setup(void) {
  Serial.begin(115200);

	int i = 0;

//   WiFi.begin(ssid_start, password_start);
//   while ((WiFi.status() != WL_CONNECTED) && i <= 30) {
	// i++
//     delay(sec);
//     Serial.print(".");
//   }
//   if (WiFi.status() != WL_CONNECTED) {
//  Serial.println("Can't connect to:");
//  Serial.println(ssid);
//  Serial.println(password);
//     delay(30*sec);
//		    ESP.restart();
// } 
//   Serial.println("");
//   Serial.print("Started Access Point ");
//   Serial.println(ssid);
//   Serial.print("IP address: ");
//   Serial.println(WiFi.localIP());

  WiFi.softAP(ssid_start, password_start);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  /*use mdns for host name resolution*/
  if (!MDNS.begin(host)) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(sec);
    }
  }
  Serial.println("mDNS responder started");
  /*return index page which is stored in serverIndex */
  serverUpdater.on("/", HTTP_GET, []() {
    serverUpdater.sendHeader("Connection", "close");
    serverUpdater.send(200, "text/html", loginIndex);
  });
  serverUpdater.on("/serverIndex", HTTP_GET, []() {
    serverUpdater.sendHeader("Connection", "close");
    serverUpdater.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  serverUpdater.on("/update", HTTP_POST, []() {
    serverUpdater.sendHeader("Connection", "close");
    serverUpdater.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = serverUpdater.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  serverUpdater.begin();
}

void loop(void) {
  serverUpdater.handleClient();
  delay(1);
}
