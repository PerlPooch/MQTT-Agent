#ifndef MDS_CONFIG
#define MDS_CONFIG

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
	#include <LittleFS.h>
#endif

#define CONFIG_DOC_SIZE				512
#define MDS_CONFIG_DEFAULT_FILE		"/config.json"

enum MDS_ConfigType {
	MDS_CONFIG_STRING,
	MDS_CONFIG_UINT8,
	MDS_CONFIG_UINT16,
	MDS_CONFIG_UINT32,
	MDS_CONFIG_INT32,
	MDS_CONFIG_BOOL,
	MDS_CONFIG_FLOAT
};

enum MDS_ConfigError {
	MDS_CONFIG_OK,
	MDS_CONFIG_FS_BEGIN_FAILED,
	MDS_CONFIG_FILE_NOT_FOUND,
	MDS_CONFIG_FILE_OPEN_FAILED,
	MDS_CONFIG_DESERIALIZE_FAILED,
	MDS_CONFIG_SERIALIZE_FAILED,
	MDS_CONFIG_UNKNOWN_TYPE
};

struct MDS_ConfigField {
	const char*		key;
	MDS_ConfigType	type;
	size_t			offset;
	size_t			size;
	struct {
		const char*	stringValue;
		uint32_t	uintValue;
		int32_t		intValue;
		bool		boolValue;
		float		floatValue;
	} defaultValue;
	double			minValue;
	double			maxValue;
};

#define MDS_CONFIG_FIELD_STRING(structType, member, key, defaultValue) \
	{ key, MDS_CONFIG_STRING, offsetof(structType, member), sizeof(structType::member), { defaultValue, 0, 0, false, 0.0f }, 0, 0 }

#define MDS_CONFIG_FIELD_UINT8(structType, member, key, defaultValue, minValue, maxValue) \
	{ key, MDS_CONFIG_UINT8, offsetof(structType, member), sizeof(uint8_t), { nullptr, (uint32_t)defaultValue, 0, false, 0.0f }, minValue, maxValue }

#define MDS_CONFIG_FIELD_UINT16(structType, member, key, defaultValue, minValue, maxValue) \
	{ key, MDS_CONFIG_UINT16, offsetof(structType, member), sizeof(uint16_t), { nullptr, (uint32_t)defaultValue, 0, false, 0.0f }, minValue, maxValue }

#define MDS_CONFIG_FIELD_UINT32(structType, member, key, defaultValue, minValue, maxValue) \
	{ key, MDS_CONFIG_UINT32, offsetof(structType, member), sizeof(uint32_t), { nullptr, (uint32_t)defaultValue, 0, false, 0.0f }, minValue, maxValue }

#define MDS_CONFIG_FIELD_INT32(structType, member, key, defaultValue, minValue, maxValue) \
	{ key, MDS_CONFIG_INT32, offsetof(structType, member), sizeof(int32_t), { nullptr, 0, (int32_t)defaultValue, false, 0.0f }, minValue, maxValue }

#define MDS_CONFIG_FIELD_BOOL(structType, member, key, defaultValue) \
	{ key, MDS_CONFIG_BOOL, offsetof(structType, member), sizeof(bool), { nullptr, 0, 0, defaultValue, 0.0f }, 0, 0 }

#define MDS_CONFIG_FIELD_FLOAT(structType, member, key, defaultValue, minValue, maxValue) \
	{ key, MDS_CONFIG_FLOAT, offsetof(structType, member), sizeof(float), { nullptr, 0, 0, false, defaultValue }, minValue, maxValue }

template <typename ConfigT>
class MDS_Config {
	public:
		template <size_t N>
		MDS_Config(const char* filename,
		           const MDS_ConfigField (&fields)[N]);
		~MDS_Config();

		bool					setup();
		bool					load();
		bool					save();
		void					print(Print& out) const;
		bool					printFile(Print& out) const;
		void					applyDefaults();
		void					normalize();
		ConfigT&				data();
		const ConfigT&			data() const;
		MDS_ConfigError			lastError() const;
		const char*				lastErrorText() const;

	private:
		const char*				filename;
		const MDS_ConfigField*	fields;
		size_t					fieldCount;
		ConfigT					config;
		MDS_ConfigError			error;

		MDS_Config(const char* filename,
		           const MDS_ConfigField* fields,
		           size_t fieldCount);
		void*					fieldPtr(const MDS_ConfigField& field);
		const void*				fieldPtr(const MDS_ConfigField& field) const;
		bool					applyDefault(const MDS_ConfigField& field);
		bool					loadField(const MDS_ConfigField& field, JsonVariant value);
		bool					saveField(const MDS_ConfigField& field, StaticJsonDocument<CONFIG_DOC_SIZE>& doc) const;
		double					clampNumber(const MDS_ConfigField& field, double value) const;
};

template <typename ConfigT>
MDS_Config<ConfigT>::MDS_Config(const char* filename,
                                const MDS_ConfigField* fields,
                                size_t fieldCount)
: filename(filename),
  fields(fields),
  fieldCount(fieldCount),
  error(MDS_CONFIG_OK)
{
	memset(&config, 0, sizeof(config));
	applyDefaults();
}

template <typename ConfigT>
template <size_t N>
MDS_Config<ConfigT>::MDS_Config(const char* filename,
                                const MDS_ConfigField (&fields)[N])
: MDS_Config(filename, fields, N)
{
}

template <typename ConfigT>
MDS_Config<ConfigT>::~MDS_Config() {
}

template <typename ConfigT>
bool MDS_Config<ConfigT>::setup() {
#if defined(ARDUINO_ARCH_ESP32)
	if(!LittleFS.begin(true)) {
#else
	if(!LittleFS.begin()) {
#endif
		error = MDS_CONFIG_FS_BEGIN_FAILED;
		return false;
	}

	error = MDS_CONFIG_OK;
	return true;
}

template <typename ConfigT>
bool MDS_Config<ConfigT>::load() {
	applyDefaults();

	File file = LittleFS.open(filename, "r");
	if(!file) {
		error = MDS_CONFIG_FILE_NOT_FOUND;
		return false;
	}

	StaticJsonDocument<CONFIG_DOC_SIZE> doc;
	DeserializationError jsonError = deserializeJson(doc, file);
	file.close();

	if(jsonError) {
		error = MDS_CONFIG_DESERIALIZE_FAILED;
		return false;
	}

	for(size_t i = 0; i < fieldCount; i++) {
		JsonVariant value = doc[fields[i].key];
		if(!value.isNull() && !loadField(fields[i], value)) {
			error = MDS_CONFIG_UNKNOWN_TYPE;
			return false;
		}
	}

	normalize();
	error = MDS_CONFIG_OK;
	return true;
}

template <typename ConfigT>
bool MDS_Config<ConfigT>::save() {
	normalize();

	LittleFS.remove(filename);

	File file = LittleFS.open(filename, "w");
	if(!file) {
		error = MDS_CONFIG_FILE_OPEN_FAILED;
		return false;
	}

	StaticJsonDocument<CONFIG_DOC_SIZE> doc;
	for(size_t i = 0; i < fieldCount; i++) {
		if(!saveField(fields[i], doc)) {
			file.close();
			error = MDS_CONFIG_UNKNOWN_TYPE;
			return false;
		}
	}

	if(serializeJson(doc, file) == 0) {
		file.close();
		error = MDS_CONFIG_SERIALIZE_FAILED;
		return false;
	}

	file.close();
	error = MDS_CONFIG_OK;
	return true;
}

template <typename ConfigT>
void MDS_Config<ConfigT>::print(Print& out) const {
	StaticJsonDocument<CONFIG_DOC_SIZE> doc;
	for(size_t i = 0; i < fieldCount; i++) {
		saveField(fields[i], doc);
	}

	serializeJsonPretty(doc, out);
}

template <typename ConfigT>
bool MDS_Config<ConfigT>::printFile(Print& out) const {
	File file = LittleFS.open(filename, "r");
	if(!file) {
		return false;
	}

	while(file.available()) {
		out.print((char)file.read());
	}
	out.println();

	file.close();
	return true;
}

template <typename ConfigT>
void MDS_Config<ConfigT>::applyDefaults() {
	memset(&config, 0, sizeof(config));

	for(size_t i = 0; i < fieldCount; i++) {
		if(!applyDefault(fields[i])) {
			error = MDS_CONFIG_UNKNOWN_TYPE;
			return;
		}
	}

	normalize();
	error = MDS_CONFIG_OK;
}

template <typename ConfigT>
void MDS_Config<ConfigT>::normalize() {
	for(size_t i = 0; i < fieldCount; i++) {
		MDS_ConfigField field = fields[i];
		void* ptr = fieldPtr(field);

		switch(field.type) {
			case MDS_CONFIG_UINT8:
				*(uint8_t*)ptr = (uint8_t)clampNumber(field, *(uint8_t*)ptr);
				break;
			case MDS_CONFIG_UINT16:
				*(uint16_t*)ptr = (uint16_t)clampNumber(field, *(uint16_t*)ptr);
				break;
			case MDS_CONFIG_UINT32:
				*(uint32_t*)ptr = (uint32_t)clampNumber(field, *(uint32_t*)ptr);
				break;
			case MDS_CONFIG_INT32:
				*(int32_t*)ptr = (int32_t)clampNumber(field, *(int32_t*)ptr);
				break;
			case MDS_CONFIG_FLOAT:
				*(float*)ptr = (float)clampNumber(field, *(float*)ptr);
				break;
			case MDS_CONFIG_STRING:
			case MDS_CONFIG_BOOL:
				break;
			default:
				error = MDS_CONFIG_UNKNOWN_TYPE;
				return;
		}
	}
}

template <typename ConfigT>
ConfigT& MDS_Config<ConfigT>::data() {
	return config;
}

template <typename ConfigT>
const ConfigT& MDS_Config<ConfigT>::data() const {
	return config;
}

template <typename ConfigT>
MDS_ConfigError MDS_Config<ConfigT>::lastError() const {
	return error;
}

template <typename ConfigT>
const char* MDS_Config<ConfigT>::lastErrorText() const {
	switch(error) {
		case MDS_CONFIG_OK: return "OK";
		case MDS_CONFIG_FS_BEGIN_FAILED: return "LittleFS begin failed";
		case MDS_CONFIG_FILE_NOT_FOUND: return "Configuration file not found";
		case MDS_CONFIG_FILE_OPEN_FAILED: return "Configuration file open failed";
		case MDS_CONFIG_DESERIALIZE_FAILED: return "Configuration JSON deserialize failed";
		case MDS_CONFIG_SERIALIZE_FAILED: return "Configuration JSON serialize failed";
		case MDS_CONFIG_UNKNOWN_TYPE: return "Configuration field has unknown type";
		default: return "Unknown configuration error";
	}
}

template <typename ConfigT>
void* MDS_Config<ConfigT>::fieldPtr(const MDS_ConfigField& field) {
	return (void*)((uint8_t*)&config + field.offset);
}

template <typename ConfigT>
const void* MDS_Config<ConfigT>::fieldPtr(const MDS_ConfigField& field) const {
	return (const void*)((const uint8_t*)&config + field.offset);
}

template <typename ConfigT>
bool MDS_Config<ConfigT>::applyDefault(const MDS_ConfigField& field) {
	void* ptr = fieldPtr(field);

	switch(field.type) {
		case MDS_CONFIG_STRING: {
			const char* defaultValue = field.defaultValue.stringValue ? field.defaultValue.stringValue : "";
			strlcpy((char*)ptr, defaultValue, field.size);
			return true;
		}
		case MDS_CONFIG_UINT8:
			*(uint8_t*)ptr = (uint8_t)clampNumber(field, field.defaultValue.uintValue);
			return true;
		case MDS_CONFIG_UINT16:
			*(uint16_t*)ptr = (uint16_t)clampNumber(field, field.defaultValue.uintValue);
			return true;
		case MDS_CONFIG_UINT32:
			*(uint32_t*)ptr = (uint32_t)clampNumber(field, field.defaultValue.uintValue);
			return true;
		case MDS_CONFIG_INT32:
			*(int32_t*)ptr = (int32_t)clampNumber(field, field.defaultValue.intValue);
			return true;
		case MDS_CONFIG_BOOL:
			*(bool*)ptr = field.defaultValue.boolValue;
			return true;
		case MDS_CONFIG_FLOAT:
			*(float*)ptr = clampNumber(field, field.defaultValue.floatValue);
			return true;
		default:
			return false;
	}
}

template <typename ConfigT>
bool MDS_Config<ConfigT>::loadField(const MDS_ConfigField& field, JsonVariant value) {
	void* ptr = fieldPtr(field);

	switch(field.type) {
		case MDS_CONFIG_STRING: {
			const char* s = value | "";
			strlcpy((char*)ptr, s, field.size);
			return true;
		}
		case MDS_CONFIG_UINT8:
			*(uint8_t*)ptr = (uint8_t)clampNumber(field, value.as<unsigned long>());
			return true;
		case MDS_CONFIG_UINT16:
			*(uint16_t*)ptr = (uint16_t)clampNumber(field, value.as<unsigned long>());
			return true;
		case MDS_CONFIG_UINT32:
			*(uint32_t*)ptr = (uint32_t)clampNumber(field, value.as<unsigned long>());
			return true;
		case MDS_CONFIG_INT32:
			*(int32_t*)ptr = (int32_t)clampNumber(field, value.as<long>());
			return true;
		case MDS_CONFIG_BOOL:
			*(bool*)ptr = value.as<bool>();
			return true;
		case MDS_CONFIG_FLOAT:
			*(float*)ptr = (float)clampNumber(field, value.as<float>());
			return true;
		default:
			return false;
	}
}

template <typename ConfigT>
bool MDS_Config<ConfigT>::saveField(const MDS_ConfigField& field, StaticJsonDocument<CONFIG_DOC_SIZE>& doc) const {
	const void* ptr = fieldPtr(field);

	switch(field.type) {
		case MDS_CONFIG_STRING:
			doc[field.key] = (const char*)ptr;
			return true;
		case MDS_CONFIG_UINT8:
			doc[field.key] = *(const uint8_t*)ptr;
			return true;
		case MDS_CONFIG_UINT16:
			doc[field.key] = *(const uint16_t*)ptr;
			return true;
		case MDS_CONFIG_UINT32:
			doc[field.key] = *(const uint32_t*)ptr;
			return true;
		case MDS_CONFIG_INT32:
			doc[field.key] = *(const int32_t*)ptr;
			return true;
		case MDS_CONFIG_BOOL:
			doc[field.key] = *(const bool*)ptr;
			return true;
		case MDS_CONFIG_FLOAT:
			doc[field.key] = *(const float*)ptr;
			return true;
		default:
			return false;
	}
}

template <typename ConfigT>
double MDS_Config<ConfigT>::clampNumber(const MDS_ConfigField& field, double value) const {
	if(field.minValue != field.maxValue) {
		if(value < field.minValue) value = field.minValue;
		if(value > field.maxValue) value = field.maxValue;
	}

	return value;
}

#endif
