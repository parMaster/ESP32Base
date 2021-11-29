#include <EEPROM.h>

// eepromReadString - reads from EEPROM, ignoring zero values
String eepromReadString(int from, int size) {
	String result;

	Serial.print("Reading EEPROM: ");
	Serial.print(size);
	Serial.print(" bytes from ");
	Serial.println(from);

	for (int i = from; i < from+size; ++i) {
		if (EEPROM.read(i) != 0) {
			Serial.print(EEPROM.read(i));
			result += char(EEPROM.read(i));
		} 
	}
	Serial.print("EEPROM read result: ");
	Serial.println(result);
	return result;
}

void eepromClear(int from, int size) {
	for (int i = 0; i < size; ++i) {
		EEPROM.write(i + from, 0);
	}
}

// eepromWriteString - writes value to EEPROM
bool eepromWriteString(int from, String value) {

	if (value.length() == 0) {
		Serial.println("Error: empty string");
		return false;
	}

	Serial.println("Writing eeprom byte-by-byte:");
	for (int i = 0; i < value.length(); ++i) {
		EEPROM.write(i + from, value[i]);
		Serial.println(value[i]);
	}

	return EEPROM.commit();
}