#include <AsyncMqttClient.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;

void connectToMqtt() {
	Serial.println("Connecting to MQTT...");
	mqttClient.connect();
}

/*
There are three levels of QoS:
    0 - at most once
    1 - at least once
    2 - exactly once
*/

void onMqttConnect(bool sessionPresent) {
	Serial.println("Connected to MQTT");
	Serial.print("Session present: ");
	Serial.println(sessionPresent);
	uint16_t packetIdSub = mqttClient.subscribe("esp32base/log", 2);
	Serial.print("Subscribing at QoS 2, packetId: ");
	Serial.println(packetIdSub);
	mqttClient.publish("esp32base/lol", 0, true, "test 1");
	Serial.println("Publishing at QoS 0");
	uint16_t packetIdPub1 = mqttClient.publish("esp32base/lol", 1, true, "test 2");
	Serial.print("Publishing at QoS 1, packetId: ");
	Serial.println(packetIdPub1);
	uint16_t packetIdPub2 = mqttClient.publish("esp32base/lol", 2, true, "test 3");
	Serial.print("Publishing at QoS 2, packetId: ");
	Serial.println(packetIdPub2);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
	Serial.println("Disconnected from MQTT.");

	if (WiFi.isConnected()) {
		xTimerStart(mqttReconnectTimer, 0);
	}
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
	Serial.println("Subscribe acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
	Serial.print("  qos: ");
	Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId) {
	Serial.println("Unsubscribe acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
	Serial.printf("MQTT => %s \r\n", payload);




	Serial.println("Publish received.");
	Serial.print("  topic: ");
	Serial.println(topic);
	Serial.print("  qos: ");
	Serial.println(properties.qos);
	Serial.print("  dup: ");
	Serial.println(properties.dup);
	Serial.print("  retain: ");
	Serial.println(properties.retain);
	Serial.print("  len: ");
	Serial.println(len);  
	Serial.print("  strlen: ");
	Serial.println(strlen(payload));
	Serial.print("  index: ");
	Serial.println(index);
	Serial.print("  total: ");
	Serial.println(total);
}

void onMqttPublish(uint16_t packetId) {
	Serial.println("Publish acknowledged.");
	Serial.print("  packetId: ");
	Serial.println(packetId);
}
