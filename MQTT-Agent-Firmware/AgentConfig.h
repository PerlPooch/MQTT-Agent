#pragma once

#include <MDS_Config.h>

struct AppConfig {
	char MQTTBroker[64] = {};
	uint16_t MQTTPort = 1883;
	uint16_t temperatureUpdateRate = 10;
	uint16_t statusUpdateRate = 60;
	uint16_t relayPulseDuration = 500;
};

// Application schema and recovery policy; storage and validation belong to MDS_Config.
class AgentConfig {
public:
	// Load saved settings, replacing a missing or bad file with defaults.
	bool begin();

	// Apply validated edits, save configuration, and copy values into AppConfig.
	// Reset runtime settings to defaults if validation or saving fails.
	bool applyAndSave(const JsonDocument& updates);

	// Parse text as an integer from 0 to 65535 and store it as a JSON number at updates[key].
	// Return false if the text is invalid or the JSON assignment runs out of memory.
	bool setJsonNumberFromText(JsonDocument& updates, const char* key, const char* text);

	// Borrow the stable runtime settings used by MQTT-Agent.
	const AppConfig& values() const {
		return values_;
	}

	// Describe the most recent configuration failure.
	const char* lastError() const {
		return error_.c_str();
	}

	// Print current settings through the shared library's diagnostics.
	void dump() const {
		storage_.dump();
	}

private:
	// Read the managed settings from MDS_Config and copy them into AppConfig for MQTT-Agent.
	void copyConfigValuesToAppConfig();

	MDS_Config storage_;
	AppConfig values_;
	String error_;
};
