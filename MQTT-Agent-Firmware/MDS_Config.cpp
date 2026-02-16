#include "MDS_Config.h"
#include <functional>
#include <FS.h>
#if IS_ESP8266 || IS_ESP32
	#include <LittleFS.h>
#endif

#define CONFIG_FILE			"/config.json"
#define CONFIG_DOC_SIZE		512


MDS_Config::MDS_Config() {
}

MDS_Config::~MDS_Config() {
}

void MDS_Config::setup() {
//	Serial.println(F("MDS_Config::setup"));
#if IS_ESP32
	LittleFS.begin(true);   // format-on-fail is handy on ESP32
#else
	LittleFS.begin();
#endif
}

void MDS_Config::load() {
//	Serial.println(F("MDS_Config::load"));
	File file = LittleFS.open(CONFIG_FILE, "r");

	if(file) {
		StaticJsonDocument<CONFIG_DOC_SIZE> doc;

		DeserializationError error = deserializeJson(doc, file);

		if(error) {
			Serial.print(F("Unable to read configuration: "));
			Serial.println(error.c_str());
		} else {
			setDataCallback(doc);
		} 

		file.close();
	} else {
		Serial.print(F("Unable to read configuration '"));
		Serial.print(CONFIG_FILE);
		Serial.println(F("': not found."));
	}
}


void MDS_Config::save() {
//	Serial.println(F("MDS_Config::save()"));

	LittleFS.remove(CONFIG_FILE);

	File file = LittleFS.open(CONFIG_FILE, "w");
	if (!file) {
		Serial.println(F("Unable to create configuration file"));
		return;
	}

	StaticJsonDocument<CONFIG_DOC_SIZE> doc;

	getDataCallback(doc);

	if (serializeJson(doc, file) == 0) {
		Serial.println(F("Unable to write configuration."));
	}

	file.close();
}


void MDS_Config::print() {
//	Serial.println(F("MDS_Config::printFile()"));

	File file = LittleFS.open(CONFIG_FILE, "r");
	if (!file) {
		Serial.println(F("Unable to read file"));
		return;
	}

	while (file.available()) {
		Serial.print((char)file.read());
	}
	Serial.println();

	file.close();
}


void MDS_Config::setSetConfigCallback(std::function<void(StaticJsonDocument<CONFIG_DOC_SIZE>&)> callback) {
//	Serial.println(F("MDS_Config::setSetConfigCallback"));
	setDataCallback = callback;
}


void MDS_Config::setGetConfigCallback(std::function<void(StaticJsonDocument<CONFIG_DOC_SIZE>&)> callback) {
//	Serial.println(F("MDS_Config::setGetConfigCallback"));
	getDataCallback = callback;
}





