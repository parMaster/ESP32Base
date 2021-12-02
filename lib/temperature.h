#include <DallasTemperature.h>

DallasTemperature sensors(&oneWire);

DeviceAddress probes[];

void searchDS() {
	DeviceAddress probeFound;

	oneWire.reset_search();

	while (oneWire.search(probeFound)) {
		Serial.println("Probe Found:");

		// SEE METHODS in DallasTemperature.h


		//Serial.println(probeFound);
	}
}

uint8_t findDevices(int pin) {
	OneWire ow(pin);

	uint8_t address[8];
	uint8_t count = 0;

	if (ow.search(address)) {
		Serial.print("\nuint8_t pin");
		Serial.print(pin, DEC);
		Serial.println("[][8] = {");
		do {
			count++;
			Serial.println("  {");
			for (uint8_t i = 0; i < 8; i++) {
				Serial.print("0x");
				if (address[i] < 0x10)
					Serial.print("0");
				Serial.print(address[i], HEX);
				if (i < 7)
					Serial.print(", ");
			}
			Serial.println("  },");
		} while (ow.search(address));

		Serial.println("};");
		Serial.print("// nr devices found: ");
		Serial.println(count);
	}

	return count;
}


class Sensor_Emulator {
public:
	// constructor
	Sensor_Emulator(int i = 0) {
		total = i;
	}

	// interface to outside world
	void addNum(int number) {
		total += number;
	}

	// interface to outside world
	int getTotal() {
		return total;
	};

private:
	// hidden data from outside world
	int total;
};
