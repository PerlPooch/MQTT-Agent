#include "AgentConfig.h"
#include <LittleFS.h>
#include <ctype.h>
#include <string.h>

namespace {
	using Type = MDS_ConfigType;
	using Access = MDS_ConfigAccess;

	const MDS_ConfigSetting Settings[] = {
		{"MQTTBroker",				Type::Text,		Access::ReadWrite,	true, "",		0,		0,	63},
		{"MQTTPort",				Type::Unsigned, Access::ReadWrite,	true, nullptr,	1883,	1,	65535},
		{"temperatureUpdateRate",	Type::Unsigned, Access::ReadWrite,	true, nullptr,	10,		0,	65535},
		{"statusUpdateRate",		Type::Unsigned, Access::ReadWrite,	true, nullptr,	60,		0,	65535},
		{"relayPulseDuration",		Type::Unsigned, Access::ReadWrite,	true, nullptr,	500,	100,	60000}
	};
}

// Read the managed settings from MDS_Config and copy them into AppConfig for MQTT-Agent.
void AgentConfig::copyConfigValuesToAppConfig() {
	const char* broker = storage_.get("MQTTBroker").as<const char*>();
	memcpy(values_.MQTTBroker, broker, strlen(broker) + 1);

	values_.MQTTPort = storage_.get("MQTTPort").as<uint16_t>();
	values_.temperatureUpdateRate = storage_.get("temperatureUpdateRate").as<uint16_t>();
	values_.statusUpdateRate = storage_.get("statusUpdateRate").as<uint16_t>();
	values_.relayPulseDuration = storage_.get("relayPulseDuration").as<uint16_t>();
}

// Load configuration, discarding an unreadable file and any interrupted write.
bool AgentConfig::begin() {
	if (!storage_.setSettings(Settings) || !storage_.begin(MDS_Config::FormatPolicy::OnMountFailure)) {
		error_ = storage_.lastError();
		return false;
	}

	if (!storage_.load()) {
		Serial.print("Config: replacing missing or invalid configuration: ");
		Serial.println(storage_.lastError());

		// The registry retains defaults on a rejected load. Discard the bad file
		// and any interrupted write, then create a clean configuration.
		for (const char* path : {"/config.json", "/config.json.tmp"}) {
			if (LittleFS.exists(path) && !LittleFS.remove(path)) {
				error_ = "Cannot remove invalid configuration";
				return false;
			}
		}

		if (!storage_.save()) {
			error_ = storage_.lastError();
			return false;
		}
	}

	copyConfigValuesToAppConfig();
	error_ = "";
	return true;
}

// Apply validated edits, save configuration, and copy values into AppConfig.
// Reset runtime settings to defaults if validation or saving fails.
bool AgentConfig::applyAndSave(const JsonDocument& updates) {
	if (!storage_.set(updates) || !storage_.save()) {
		error_ = storage_.lastError();
		storage_.setSettings(Settings); // Reset the registry to its declared defaults.
		copyConfigValuesToAppConfig();
		return false;
	}

	copyConfigValuesToAppConfig();
	error_ = "";
	return true;
}

// Parse text as an integer from 0 to 65535 and store it as a JSON number at updates[key].
// Return false if the text is invalid or the JSON assignment runs out of memory.
bool AgentConfig::setJsonNumberFromText(JsonDocument& updates, const char* key, const char* text) {
	const char* cursor = text ? text : "";
	while (isspace(static_cast<unsigned char>(*cursor))) {
		++cursor;
	}

	uint32_t value = 0;
	bool digits = false;

	while (*cursor >= '0' && *cursor <= '9') {
		digits = true;
		value = value * 10 + (*cursor++ - '0');
		if (value > 65535) {
			break;
		}
	}

	while (isspace(static_cast<unsigned char>(*cursor))) {
		++cursor;
	}

	if (!digits || *cursor || value > 65535) {
		error_ = "Expected an integer from 0 to 65535";
		return false;
	}

	updates[key] = value;
	if (updates.overflowed()) {
		error_ = "Cannot allocate configuration edit";
		return false;
	}

	error_ = "";
	return true;
}
