#include "MDS_Platform.h"
#include "AgentConfig.h"
#include <MDS_Display.h>
#include <MDS_HTTP.h>
#include <MDS_WiFi.h>

// ---- Configuration -------------------------------------------------------------------
//
#define USE_STATUS_0
// STATUS_1 and DFPlayer are mutually exclusive
#define USE_STATUS_1
#define INVERT_STATUS
// #define USE_RELAY_0
// RELAY_1 and DFPlayer are mutually exclusive
// #define USE_RELAY_1
// #define USE_DFPLAYER
// #define PLAY_TRIGGER_RELAY_0
// #define USE_MIDI
#define RELAY_POSITIVE_LOGIC
// #define RELAY_NEGATIVE_LOGIC
#define USE_1WIRE_TEMPERATURE
// #define USE_DHT11_TEMPERATURE
// #define ROTATE_DISPLAY
//
// --------------------------------------------------------------------------------------

#if IS_ESP8266
	#include <ESP8266WiFi.h>
#elif IS_ESP32
	#include <WiFi.h>
#endif
#include <Update.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#ifdef USE_1WIRE_TEMPERATURE
#include <OneWire.h>
#include <DallasTemperature.h>
#endif
#include <arduino-timer.h>
#include <Wire.h>
#ifdef USE_DHT11_TEMPERATURE
#include "DHT.h"
#endif
#ifdef USE_DFPLAYER
	#if IS_ESP8266
		#include <SoftwareSerial.h>
	#endif
#include <DFRobotDFPlayerMini.h>
#endif
#ifdef USE_MIDI
	#include <MIDI.h>
	#if IS_ESP8266
		#include <SoftwareSerial.h>
	#endif
#endif


#define VERSION				"2.2"

// #define DEBUG_D						// Mirror screen to serial

#if IS_ESP8266
	// LED
	#define PIN_LED 			16
	#define PIN_LED_1			2
	// I2C OLED
	#define PIN_I2C_SDA			4
	#define PIN_I2C_SCL			5
	// DFPlayer
	#define PIN_DFP_RX			13
	#define PIN_DFP_TX			2
	// Inputs
	#define PIN_RESET_WIFI 		12
	#ifdef USE_STATUS_0
		#define PIN_STATUS_0 	10
	#endif
	#ifdef USE_STATUS_1
		#define PIN_STATUS_1 	13
	#endif
	// 1-wire temp
	#define PIN_TEMP_0		 	14
	// Relay
	#if defined (USE_RELAY_0) || defined (PLAY_TRIGGER_RELAY_0)
		#define PIN_RELAY_0 	0
	#endif
	#ifdef USE_RELAY_1
		#define PIN_RELAY_1 	2
	#endif
#elif IS_ESP32 && IS_ESP32C6
	// LED
	#ifndef LED_BUILTIN
		#define LED_BUILTIN		15
	#endif
	#define PIN_LED				LED_BUILTIN
	// I2C OLED
	#define PIN_I2C_SDA			SDA    // D4 / GPIO22
	#define PIN_I2C_SCL			SCL    // D5 / GPIO23
	// DFPlayer
	#define DFP_UART_NUM		1
	#define PIN_DFP_TX			D6    // GPIO16
	#define PIN_DFP_RX			D7    // GPIO17
	// Inputs
	#define PIN_RESET_WIFI		D1  // GPIO1  (button to GND, INPUT_PULLUP)
	#ifdef USE_STATUS_0
		#define PIN_STATUS_0	D2  // GPIO2  (INPUT_PULLUP)
	#endif
	#ifdef USE_STATUS_1
		#define PIN_STATUS_1 	D8	// GPIO19
	#endif
	// 1-wire temp
	#define PIN_TEMP_0			D0   // GPIO0  (1-wire)
	// Relay
	#if defined(USE_RELAY_0) || defined(PLAY_TRIGGER_RELAY_0)
		#define PIN_RELAY_0		D3   // GPIO21
	#endif
	#ifdef USE_RELAY_1
		#define PIN_RELAY_1 	D10
	#endif
#else
#endif

// if relay is disabled, don't allow play-trigger
#if !defined(USE_RELAY_0)
  #undef PLAY_TRIGGER_RELAY_0
#endif

#define DHT_TYPE				DHT11

#define	RELAY_TIMEOUT			500 	// Time(ms) for relay pulses
#define	HAPPY_PERIOD			5000	// Time(ms) for happy LED blinks
#define	HAPPY_NOMQTT_PERIOD		10000	// Time(ms) for happy LED blinks
#define	DFPLAYER_RESET			3600	// Time(s) between resetting DFPlayer
#define ONLINE_CHECK_PERIOD 	10000	// Time(ms) between checks to see if we're still online
#define REBOOT_DAYS				7		// Reboot every N days
#define REBOOT_BUTTON_TIME		10000	// Time(ms) WiFi button down to cause Reboot

#ifdef RELAY_POSITIVE_LOGIC
	#define LOGIC_HIGH 1
	#define LOGIC_LOW 0
#else
	#define LOGIC_HIGH 0
	#define LOGIC_LOW 1
#endif

#define MS_PER_DAY				(24UL * 60UL * 60UL * 1000UL)

MDS_WiFi wifi;

// Borrow the library's stable MAC-based identity for MQTT topics and payloads.
const char* const systemID = wifi.systemID();

MDS_HTTP http;

#ifdef USE_1WIRE_TEMPERATURE
OneWire 			oneWire(PIN_TEMP_0);
DallasTemperature 	sensors(&oneWire);
#endif

#ifdef USE_DHT11_TEMPERATURE
DHT					dht(PIN_TEMP_0, DHT_TYPE);
#endif

#ifdef USE_DFPLAYER
	#if IS_ESP8266
		SoftwareSerial mySoftwareSerial(PIN_DFP_RX, PIN_DFP_TX); // RX, TX
	#elif IS_ESP32
		HardwareSerial mySoftwareSerial(DFP_UART_NUM);
	#endif
DFRobotDFPlayerMini dfp;
#endif

#ifdef USE_MIDI
SoftwareSerial mySerial(16, 0); // RX, TX  D0, D3
// MIDI_NAMESPACE::SerialMIDI<SoftwareSerial> serialMIDI(mySerial);
// MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>> MIDI((MIDI_NAMESPACE::SerialMIDI<SoftwareSerial>&)serialMIDI);
MIDI_CREATE_INSTANCE(SoftwareSerial, mySerial, midiA);
#endif

WiFiClient 			wifiClient;
PubSubClient 		client(wifiClient);

#ifdef ROTATE_DISPLAY
constexpr uint8_t DisplayRotation = 2;
#else
constexpr uint8_t DisplayRotation = 0;
#endif

MDS_Display display(Wire, 0x3C, DisplayRotation);

Timer<>::Task		happyBlinkTimer;
Timer<>::Task		temperatureTimer;
Timer<>::Task		statusTimer;
#ifdef USE_DFPLAYER
Timer<>::Task		dfplayerTimer;
#endif

auto					timer = timer_create_default();
unsigned long			lastMQTTOnlineCheck;
static unsigned long	rebootPressedTime = 0;

// Track readiness transitions only to restart MQTT connection attempts.
static bool wifiWasReady = false;

static uint32_t	rebootTime = (uint32_t)((uint32_t)REBOOT_DAYS * MS_PER_DAY);

AgentConfig configuration;
// Stable, read-only runtime snapshot; only the managed registry can update it.
const AppConfig& appConfig = configuration.values();

struct temperature_t {
	char	address[24];	// "XX:XX:XX:XX:XX:XX:XX:XX" + '\0'
	float	tempF;
	bool	valid;
} ;

// === Upload/Update
static volatile bool shouldReboot = false;

static void handleUploadPage();
static void handleUploadPost();
static void handleUploadStream();

// Application-owned provisioning fields; MDS_WiFi owns WiFiManager.
static char portalMqttPort[6];     // "65535" + NUL
static char portalTemperatureRate[6];
static char portalStatusRate[6];

static WiFiManagerParameter pMqttBroker(
	"mqttBroker",
	"MQTT Broker",
	appConfig.MQTTBroker,
	sizeof(appConfig.MQTTBroker)
);

static WiFiManagerParameter pMqttPort(
	"mqttPort",
	"MQTT Port",
	portalMqttPort,
	sizeof(portalMqttPort)
);

static WiFiManagerParameter pTempRate(
	"tempRate",
	"Temperature Update Rate (s)",
	portalTemperatureRate,
	sizeof(portalTemperatureRate)
);

static WiFiManagerParameter pStatusRate(
	"statusRate",
	"Status Update Rate (s)",
	portalStatusRate,
	sizeof(portalStatusRate)
);

// Report configuration errors and apply the current fallback settings.
static bool configError() {
	Serial.print(F("Config: "));
	Serial.println(configuration.lastError());

	// A failed update uses defaults; apply both rates before returning to loop().
	timer.cancel(temperatureTimer);
	timer.cancel(statusTimer);
	if (appConfig.temperatureUpdateRate > 0)
		temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000UL, publishTemperature, (void *)0);
	if (appConfig.statusUpdateRate > 0)
		statusTimer = timer.every(appConfig.statusUpdateRate * 1000UL, publishStatus, (void *)0);
	if (!*appConfig.MQTTBroker) client.disconnect();
	return false;
}

// Parse and persist a numeric setting from an existing interface.
static bool setConfigNumber(const char* key, const String& text) {
	JsonDocument updates;
	if (!configuration.setJsonNumberFromText(updates, key, text.c_str()) || !configuration.applyAndSave(updates))
		return configError();
	return true;
}

bool lastStatus0 = false, lastStatus1 = false;
bool reset_is_down = false;



#ifdef USE_DFPLAYER
void dfpPrintDetail(uint8_t type, int value) {
	switch (type) {
		case TimeOut:
			Serial.println(F("DFP: Timeout."));
			break;
		case WrongStack:
			Serial.println(F("DFP: Stack Wrong."));
			break;
		case DFPlayerCardInserted:
			Serial.println(F("DFP: Card Inserted."));
			D(F("Card Inserted."));
			break;
		case DFPlayerCardRemoved:
			Serial.println(F("DFP: Card Removed."));
			D(F("Card Removed."));
			break;
		case DFPlayerCardOnline:
			Serial.println(F("DFP: Card Online."));
			D(F("Card Online."));
			Serial.print(F("DFP: "));
			Serial.print(dfp.readFileCounts());
			Serial.println(F(" items on card."));
			D(dfp.readFileCounts() + " items.");
			break;
		case DFPlayerUSBInserted:
			Serial.println(F("DFP: USB Inserted."));
			break;
		case DFPlayerUSBRemoved:
			Serial.println(F("DFP: USB Removed."));
			break;
		case DFPlayerPlayFinished:
			Serial.print(F("DFP: item "));
			Serial.print(value);
			Serial.println(F(" Play finished."));
			D("Play " + String(value) + " finished.");
#ifdef PLAY_TRIGGER_RELAY_0
			delay(200);
			digitalWrite(PIN_RELAY_0, LOGIC_LOW);
#endif
#ifdef USE_MIDI
			midiA.sendNoteOff(61, 0, 1);     // Stop the note
#endif
			publishPlayStatus((void *)0, value, "complete");
		break;
		case DFPlayerError:
			Serial.print(F("DFP: Error, "));

			switch (value) {
				case Busy:
					Serial.println(F("Card not found"));
					break;
				case Sleeping:
					Serial.println(F("Sleeping"));
					break;
				case SerialWrongStack:
					Serial.println(F("Get Wrong Stack"));
					break;
				case CheckSumNotMatch:
					Serial.println(F("Check Sum Not Match"));
					break;
				case FileIndexOut:
					Serial.println(F("File Index Out of Bound"));
					break;
				case FileMismatch:
					Serial.println(F("Cannot Find File"));
					break;
				case Advertise:
					Serial.println(F("In Advertise"));
					break;
				default:
					break;
			}
			break;
		default:
			break;
	}  
}
#endif

// Validate the portal's application settings together and save them through AgentConfig.
static void savePortalConfig() {
	JsonDocument updates;
	updates["MQTTBroker"] = pMqttBroker.getValue();

	if (!configuration.setJsonNumberFromText(updates, "MQTTPort", pMqttPort.getValue())
		|| !configuration.setJsonNumberFromText(updates, "temperatureUpdateRate", pTempRate.getValue())
		|| !configuration.setJsonNumberFromText(updates, "statusUpdateRate", pStatusRate.getValue())
		|| !configuration.applyAndSave(updates)) {
		configError();
	}
}

// Populate the portal fields and start the library's existing connection/provisioning policy.
static void setupWiFi(bool forcePortal) {
	snprintf(portalMqttPort, sizeof(portalMqttPort), "%u", (unsigned)appConfig.MQTTPort);
	snprintf(portalTemperatureRate, sizeof(portalTemperatureRate), "%u", (unsigned)appConfig.temperatureUpdateRate);
	snprintf(portalStatusRate, sizeof(portalStatusRate), "%u", (unsigned)appConfig.statusUpdateRate);

	pMqttBroker.setValue(appConfig.MQTTBroker, sizeof(appConfig.MQTTBroker));
	pMqttPort.setValue(portalMqttPort, sizeof(portalMqttPort));
	pTempRate.setValue(portalTemperatureRate, sizeof(portalTemperatureRate));
	pStatusRate.setValue(portalStatusRate, sizeof(portalStatusRate));

	wifi.addParameter(pMqttBroker);
	wifi.addParameter(pMqttPort);
	wifi.addParameter(pTempRate);
	wifi.addParameter(pStatusRate);

	wifi.setStatusCallback(D);
	wifi.setSaveCallback(savePortalConfig);

	MDS_WiFi::Options options;
	options.accessPointPrefix = "MQA";
	options.accessPointPassword = "12345678";
	options.portalTitle = "Agent Configuration";
	options.connectionPolicy = MDS_WiFi::ConnectionPolicy::Legacy;

	wifi.begin(options, forcePortal);
}

// Serve the existing browser form for uploading a firmware binary.
static void handleUploadPage()
{
	if (!wifi.ready()) {
		return;
	}

	// Minimal page: choose .bin and POST it
	static const char uploadHtml[] =
		"<!doctype html><html><head><meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width,initial-scale=1'>"
		"<title>Update Firmware</title>"
		"<style>"
		":root{--bg:#000000;--text:#fff;--card:#20202080;--border:rgba(43,14,161,0.58);--shadow:0 12px 30px rgba(0,0,0,.10);--radius:8px}"
		"html,body{height:100%;}"
		"body{margin:0;color:var(--text);background:var(--bg);font-family:Helvetica,Arial,sans-serif;}"
		".card{max-width:560px;margin:10vh auto;padding:24px;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);}"
		".row{display:flex;gap:8px;align-items:center;}"
		".row button{margin-left:auto;}"
		"h3{margin:0 0 14px 0;font-size:18px;font-weight:600;}"
		"input,button{font:inherit;}"
		"button{margin-left:8px;border-radius:var(--radius);background:#fff;cursor:pointer;}"
		"</style>"
		"</head><body>"
		"<div class='card'>"
		"<h3>Update Firmware</h3>"
		"<form method='POST' action='/upload' enctype='multipart/form-data'>"
		"<div class='row'>"
		"<input type='file' name='firmware' accept='.bin' required>"
		"<button type='submit'>Update</button>"
		"</div>"
		"</form>"
		"</div>"
		"</body></html>";

	http.server().send(200, "text/html", uploadHtml);
}

static void handleUploadPost()
{
	// Called after upload completes (success or failure)
	if (Update.hasError()) {
		String msg = "Update failed.\n";
		http.server().send(500, "text/plain", msg);
		return;
	}

	http.server().send(200, "text/plain", "Update OK. Rebooting...\n");
	shouldReboot = true; // reboot in loop after response is sent
}

// Write incoming firmware chunks and finalize or abort the update.
static void handleUploadStream()
{
	HTTPUpload &upload = http.server().upload();

	if (upload.status == UPLOAD_FILE_START) {
		Serial.printf("OTA: Start: %s\n", upload.filename.c_str());
		D(F("OTA: Start ..."));

		// Optional: stop other activity if you want (MQTT, timers, etc.)
		// client.disconnect();

		if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
			Serial.println("OTA: Update.begin() failed");
			D(F("OTA: failed."));
			Update.printError(Serial);
		}

	} else if (upload.status == UPLOAD_FILE_WRITE) {
		if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
			Serial.println("OTA: Update.write() failed");
			D(F("OTA: failed."));
			Update.printError(Serial);
		}

	} else if (upload.status == UPLOAD_FILE_END) {
		if (Update.end(true)) {
			Serial.printf("OTA: Success. Size: %u\n", upload.totalSize);
			D(F("OTA: Success."));
		} else {
			Serial.println("OTA: Update.end() failed");
			D(F("OTA: failed."));
			Update.printError(Serial);
		}

	} else if (upload.status == UPLOAD_FILE_ABORTED) {
		Serial.println("OTA: Upload aborted");
		D(F("OTA: aborted."));
		Update.end();
	}
}


void owAddressToCString(const uint8_t addr[8], char *out)
{
	static const char hex[] = "0123456789ABCDEF";
	uint8_t p = 0;

	for (uint8_t i = 0; i < 8; i++) {
		out[p++] = hex[(addr[i] >> 4) & 0x0F];
		out[p++] = hex[addr[i] & 0x0F];

		if (i < 7)
			out[p++] = ':';
	}

	out[p] = '\0';
}

void owInventory() {
	byte addr[8];
	int count = 0;
	char romStr[24];

	oneWire.reset_search();

	while (oneWire.search(addr)) {
		count++;

		Serial.print("            ");
		Serial.print("Device[");
		Serial.print(count);
		Serial.print("] Address: ");

		owAddressToCString(addr, romStr);
		Serial.print(romStr);

		if (OneWire::crc8(addr, 7) != addr[7])
			Serial.print(" (Fail), ");
		else
			Serial.print(" (OK), ");

		Serial.print("Family: 0x");
		if (addr[0] < 16)
			Serial.print("0");
		Serial.print(addr[0], HEX);

		Serial.println();
	}
}

bool clearRelay(void* opaque) {
	const size_t relayNum = (size_t)(uintptr_t)opaque;
	
	int relayPin = -1;
	
#if defined(USE_RELAY_0)
	if(relayNum == 0) relayPin = PIN_RELAY_0;
#endif
#if defined(USE_RELAY_1)
	if(relayNum == 1) relayPin = PIN_RELAY_1;
#endif
	
	if(relayPin < 0) {		// Unknown relayNum or relay not compiled in
		return false;
	}
	
	digitalWrite(relayPin, LOGIC_LOW);
	return false; // one-shot for SimpleTimer-style callbacks
}

temperature_t updateTemperature(uint8_t index) {
	temperature_t r;

	r.address[0] = '\0';
	r.tempF = 0.0f;
	r.valid = false;

#ifdef USE_1WIRE_TEMPERATURE
	DeviceAddress a;

	sensors.setWaitForConversion(true);
	sensors.requestTemperatures();

	r.tempF = sensors.getTempFByIndex(index);

	if (sensors.getAddress(a, index)) {
		owAddressToCString(a, r.address);
		r.valid = true;
	} else {
		r.valid = false;
	}
#elif defined(USE_DHT11_TEMPERATURE)
	(void)index; // unused

	// DHT11 has no 1-Wire ROM address; provide a dummy identifier
	strcpy(r.address, "DHT11");

	r.tempF = dht.readTemperature(true); // true = Fahrenheit (DHT library)
	if (!isnan(r.tempF))
		r.valid = true;
	else
		r.tempF = 0.0f
#else
	(void)index; // unused

	// No sensor configured
	strcpy(r.address, "NONE");
	r.tempF = 0.0f;
	r.valid = false;
#endif

	return r;
}

String updateHumidity() {
	float 	humidity;
	char	data[200];

#if defined(USE_DHT11_TEMPERATURE)
	humidity = dht.readHumidity();
#else
	humidity = 0.0f;
#endif
	snprintf(data, sizeof(data), "%0.1f", humidity);

	return String(data);
}


bool publishTemperature(void* opaque) {
	char	data[200];
	char	topic[96];
	char	baseTopic[96];
	char	idStr[24];			// raw "28:FF:..."
	char	idStrStripped[17];	// "28FF..." (16 hex + '\0')

	(void)opaque;

	if (strlen(appConfig.MQTTBroker) == 0)
		return true;

	blinkLED(nullptr);

	// Base topic: "spencer/<systemID>/temperature"
	memset(baseTopic, 0, sizeof(baseTopic));
	strncpy(baseTopic, "spencer/", sizeof(baseTopic) - 1);
	strncat(baseTopic, systemID, sizeof(baseTopic) - strlen(baseTopic) - 1);
	strncat(baseTopic, "/temperature", sizeof(baseTopic) - strlen(baseTopic) - 1);

	// Single-sensor topic is exactly baseTopic
    const char* legacyTopic = baseTopic;

	uint8_t sensorCount = 1;
	char	tempStr[16];
	temperature_t averageTemp;
	
#ifdef USE_1WIRE_TEMPERATURE
	sensorCount = sensors.getDeviceCount();
	if (sensorCount == 0)
		return true;

	averageTemp.tempF = 0.0f;
#endif

	for (uint8_t i = 0; i < sensorCount; i++){
		temperature_t t = updateTemperature(i);
		
		if(sensorCount > 1)
			averageTemp.tempF += t.tempF;

#ifdef USE_1WIRE_TEMPERATURE
		strncpy(idStr, t.address, sizeof(idStr) - 1);
		idStr[sizeof(idStr) - 1] = '\0';

		// Strip ':' → build 16-char hex ID
		uint8_t p = 0;
		for(uint8_t j = 0; j < strlen(idStr); j++) {
			if (idStr[j] != ':' && p < 16)
				idStrStripped[p++] = idStr[j];
		}
		idStrStripped[p] = '\0';
#else
		strcpy(idStrStripped, "0");
#endif

		// Topic
		const char* publishTopic = legacyTopic;

		if (sensorCount > 1) {
			// Build per-sensor topic safely
			snprintf(topic, sizeof(topic), "%s/%s", baseTopic, idStrStripped);
			publishTopic = topic;
        }

		// Payload
		StaticJsonDocument<200> doc;
		doc["id"] = systemID;
		doc["updateRate"] = (String)appConfig.temperatureUpdateRate;
		
		snprintf(tempStr, sizeof(tempStr), "%.1f", t.tempF);
		doc["temperature"] = tempStr;
		
		memset(data, 0, sizeof(data));
		serializeJson(doc, data, sizeof(data));
		
		// Publish
		client.publish(publishTopic, data);
		
		// ALSO publish sensor 0 as /temperature
		if (sensorCount > 1 && i == 0) {
			client.publish(legacyTopic, data);
		}
		
		// D(String(F("T")) + String(i) + F(": ") + String(t.tempF, 1) + F(" -> ") + String(publishTopic));
		D(String(F("T")) + String(i) + F(": ") + String(t.tempF, 1));
    }

	if(sensorCount > 1) {
		averageTemp.tempF /= sensorCount;
	
		StaticJsonDocument<200> doc;
	
		doc["id"] = systemID;
		doc["updateRate"] = (String)appConfig.temperatureUpdateRate;
	
		snprintf(tempStr, sizeof(tempStr), "%.1f", averageTemp.tempF);
		doc["temperature"] = tempStr;
	
		memset(data, 0, sizeof(data));
		serializeJson(doc, data, sizeof(data));

		snprintf(topic, sizeof(topic), "%s/Average", baseTopic);
		client.publish(topic, data);
		
		D(String(F("Ta: ")) + String(averageTemp.tempF, 1));
	}
		
#ifdef USE_DHT11_TEMPERATURE
	publishHumidity(0);
#endif

	return true;
}

bool publishHumidity(void* opaque) {
	char	data[200];
	char	buf[64];

// Serial.println(F("publishHumidity()"));

	String temp = updateHumidity();
	D(String(F("Humd 0: ")) + temp);

	memset(buf, 0, sizeof(buf));
	strncpy(buf, "spencer/", sizeof(buf));
	strcat(buf, systemID);
	strcat(buf, "/humidity");
	
	blinkLED((void *)0);
	
	if(strlen(appConfig.MQTTBroker) > 0) {
		StaticJsonDocument<200> doc;

		doc["id"] = systemID;
		doc["humidity"] = temp;
		doc["updateRate"] = (String)appConfig.temperatureUpdateRate; // Note we copy this

		serializeJson(doc, data, sizeof(data));
	
		if (client.publish(buf, data)) {
		}
	}
		
	return true;
}


bool publishStatus(void* opaque) {
	char	data[400];
	char	buf[64];

	bool	input0 = false;
	bool	input1 = false;

#ifdef USE_STATUS_0
	input0 = ! digitalRead(PIN_STATUS_0);
#endif
#ifdef USE_STATUS_1
	input1 = ! digitalRead(PIN_STATUS_1);
#endif

	D(String(F("Status: ")) + String(input0) + String(F(" ")) + String(input1));

	memset(buf, 0, sizeof(buf));
	strncpy(buf, "spencer/", sizeof(buf));
	strcat(buf, systemID);
	strcat(buf, "/status");
	
	blinkLED((void *)0);
	
	if(strlen(appConfig.MQTTBroker) > 0) {
		DynamicJsonDocument doc = getStatusAsJSON();

		serializeJson(doc, data, sizeof(data));

		if (client.publish(buf, data)) {
		}
	}
		
	return true;
}


bool publishReboot(void* opaque) {
	char	data[400];
	char	buf[64];

	bool	input0 = false;
	bool	input1 = false;

	memset(buf, 0, sizeof(buf));
	strncpy(buf, "spencer/", sizeof(buf));
	strcat(buf, systemID);
	strcat(buf, "/reboot");
	
	blinkLED((void *)0);
	
	if(strlen(appConfig.MQTTBroker) > 0) {
		StaticJsonDocument<200> doc;

		doc["id"] = systemID;
		doc["uptime"] = millis();

		serializeJson(doc, data, sizeof(data));

		if (client.publish(buf, data)) {
		}
	}
		
	return true;
}


bool publishRelays(void* opaque) {
	char	data[200];
	char	buf[64];

#ifdef USE_RELAY_0
	bool relay0 = digitalRead(PIN_RELAY_0);
#else
	bool relay0 = false;
#endif
#ifdef USE_RELAY_1
	bool relay1 = digitalRead(PIN_RELAY_1);
#else
	bool relay1 = false;
#endif
#ifdef RELAY_POSITIVE_LOGIC
#else
	#ifdef USE_RELAY_0
		relay0 = !relay0;
	#endif
	#ifdef USE_RELAY_1
		relay1 = !relay1;
	#endif
#endif
	D(String(F("Relays: ")) + String(relay0) + String(F(" ")) + String(relay1));

	memset(buf, 0, sizeof(buf));
	strncpy(buf, "spencer/", sizeof(buf));
	strcat(buf, systemID);
	strcat(buf, "/relay");
	
	blinkLED((void *)0);
	
	if(strlen(appConfig.MQTTBroker) > 0) {
		StaticJsonDocument<200> doc;

		doc["id"] = systemID;
#ifdef USE_RELAY_0
		doc["relay0"] = (uint8_t)(relay0 ? 1 : 0);
#endif
#ifdef USE_RELAY_1
		doc["relay1"] = (uint8_t)(relay1 ? 1 : 0);
#endif
		serializeJson(doc, data, sizeof(data));
	
		if (client.publish(buf, data)) {
		}
	}
	
	return true;
}


bool publishPlayStatus(void* opaque, int item, String status) {
	char	data[200];
	char	buf[64];

	memset(buf, 0, sizeof(buf));
	strncpy(buf, "spencer/", sizeof(buf));
	strcat(buf, systemID);
	strcat(buf, "/trigger");
	
	blinkLED((void *)0);
	
	if(strlen(appConfig.MQTTBroker) > 0) {
		StaticJsonDocument<200> doc;

		doc["id"] = systemID;
		doc["item"] = (String)item;
		doc["status"] = (String)status;
	
		serializeJson(doc, data, sizeof(data));
	
		if (client.publish(buf, data)) {
		}
	}
		
	return true;
}


// This is called any time an MQTT message matches our subscription. Since we subscribe to the same
// topics we publish, this will be called every time we publish, too. So, we bail as early as we can
// (no command).
void callback(char* in_topic, byte* in_message, unsigned int length) {
	String	message;
	char*	token;
	char	buf[512];

	String	device;
	String	command;
	String	args;

	// Convert in_message to a String
	if (length == 0) return;

	size_t n = (length < sizeof(buf) - 1) ? length : (sizeof(buf) - 1);
	memcpy(buf, in_message, n);
	buf[n] = '\0';

	Serial.print(F("t: ")); Serial.println(in_topic);
	Serial.print(F("m: ")); Serial.println(buf);

	StaticJsonDocument<512> doc;

	DeserializationError error = deserializeJson(doc, buf);
	const char* jsonValue;

	if (error) {
		Serial.println(F("Unable to read message"));
		return;
	}

	// --- Command ---
	// 	/temperature
	// 		{"command": "fetch"}
	// 		{"command": "set-rate", "rate": "int-as-string, 0=disable"}
	// 	/status
	// 		{"command": "set-rate", "rate": "int-as-string, 0=disable"}
	// 	/relay
	// 		{"command": "set", "device": 0|1, "state": on|off|pulse}
	//	/audio
	//		{"command": "play", "item": "1-indexed int-as-string"}
	
	// Valid: fetch|set-rate|set|play
	jsonValue = doc["command"] | "";
	if (!*jsonValue) return;
	command = String(jsonValue);

	if(command.length() == 0) {
		return;
	}

	// Parse the topic into parts.
	// we use spencer/systemID/<device>
	const char delimeter[2] = "/";

	char topicCopy[128];
	strncpy(topicCopy, in_topic, sizeof(topicCopy)-1);
	topicCopy[sizeof(topicCopy)-1] = '\0';

	// Spencer prefix
	token = strtok(topicCopy, delimeter);
	if(!token) return;
// 	Serial.println("1: " + String(token));

	// Since we only subscribed to topics matching our systemID, we don't need to check it here.
	token = strtok(NULL, delimeter);
	if(!token) return;
// 	Serial.println("2: " + String(token));

	// Now get the device
	token = strtok(NULL, delimeter);
	if(!token) return;
	device = String(token);
// 	Serial.println("3: " + device);

	Serial.print(F("MQTT: Device: "));
	Serial.print(device);
	Serial.print(F(", Command: "));
	Serial.println(command);

  	if(command == "fetch") {
	 	if(device == "temperature") {
			publishTemperature((void *)0);
			publishHumidity((void *)0);
		} else if(device == "status") {
			publishStatus((void *)0);
		} else if(device == "relay") {
			publishRelays((void *)0);
		}
	} else if(command == "set-rate") {
		jsonValue = doc["rate"] | "";
		if (!*jsonValue) return;
		String rate = String(jsonValue);

		if(rate.length() == 0) {
			Serial.println("rate missing");
			return;
		}

		if(device == "temperature") {
			if (!setConfigNumber("temperatureUpdateRate", rate)) return;
			D("Set-rate Temp: " + String(appConfig.temperatureUpdateRate));
			timer.cancel(temperatureTimer);
			if(appConfig.temperatureUpdateRate > 0)
				temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000UL, publishTemperature, (void *)0);
		} else if(device == "status") {
			if (!setConfigNumber("statusUpdateRate", rate)) return;
			D("Set-rate Status: " + String(appConfig.statusUpdateRate));
			timer.cancel(statusTimer);
			if(appConfig.statusUpdateRate > 0)
				statusTimer = timer.every(appConfig.statusUpdateRate * 1000UL, publishStatus, (void *)0);
		}
 	} else if(command == "set") {
		jsonValue = doc["device-num"] | "";
		if (!*jsonValue) return;
		String deviceNum = String(jsonValue);

		jsonValue = doc["state"] | "";
		if (!*jsonValue) return;
		String state = String(jsonValue);

		if(deviceNum.length() == 0) {
			Serial.println("device-num missing");
			return;
		}
		if(state.length() == 0) {
			Serial.println("state missing");
			return;
		}
 	
	 	if(device == "relay") {
			byte relay;
#ifdef USE_RELAY_0
			if(deviceNum.toInt() == 0) relay = PIN_RELAY_0;
#else
			if(deviceNum.toInt() == 0) return;
#endif
#ifdef USE_RELAY_1
			if(deviceNum.toInt() == 1) relay = PIN_RELAY_1;
#else
			if(deviceNum.toInt() == 1) return;
#endif
			if(deviceNum.toInt() > 1) return;
			
			if(state == "on") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " On");

				digitalWrite(relay, LOGIC_HIGH);
			} else if(state == "off") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Off");

				digitalWrite(relay, LOGIC_LOW);
			} else if(state == "pulse") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Pulse");

				digitalWrite(relay, LOGIC_HIGH);
				timer.in(RELAY_TIMEOUT, clearRelay, (void *)deviceNum.toInt());
			}
		}
 	} else if(command == "play") {
		jsonValue = doc["item"] | "";;
		String item = String(jsonValue);

		if(item.length() == 0) {
			Serial.println("item missing");
			return;
		}
 	
#ifdef USE_DFPLAYER
		D(String(F("Play item ")) + item);
		Serial.println(String(F("DFP: Play item ")) + item);
#ifdef USE_MIDI
		midiA.sendNoteOn(61, 127, 1);    // Send a Note (pitch 42, velo 127 on channel 1)
#endif
#ifdef PLAY_TRIGGER_RELAY_0
		digitalWrite(PIN_RELAY_0, LOGIC_HIGH);
#endif
#if defined (PLAY_TRIGGER_RELAY_0) || defined (USE_MIDI)
		delay(200);
#endif
//		dfp.play(item.toInt());
		dfp.playFolder(01, (byte)item.toInt());
		
#endif
	}
}




DynamicJsonDocument getStatusAsJSON() {
	String	config;

	DynamicJsonDocument doc(512);

	doc["version"] = String(VERSION);

	if (WiFi.status() == WL_CONNECTED) {
		long rssi = WiFi.RSSI();

		doc["rssi"] = rssi;
		doc["ip"] = WiFi.localIP().toString();
		doc["id"] = systemID;
	}

#ifdef USE_STATUS_0
#ifdef INVERT_STATUS
	bool	input0 = digitalRead(PIN_STATUS_0);
#else
	bool	input0 = ! digitalRead(PIN_STATUS_0);
#endif
	config += "I0 ";
#endif
#ifdef USE_STATUS_1
#ifdef INVERT_STATUS
	bool	input1 = digitalRead(PIN_STATUS_1);
#else
	bool	input1 = ! digitalRead(PIN_STATUS_1);
#endif
	config += "I1 ";
#endif
#ifdef INVERT_STATUS
	config += "I- ";
#else
	config += "I+ ";
#endif
#ifdef USE_RELAY_0
	bool 	relay0 = digitalRead(PIN_RELAY_0);
	config += "R0 ";
#endif
#ifdef USE_RELAY_1
	bool	relay1 = digitalRead(PIN_RELAY_1);
	config += "R1 ";
#endif
#ifdef RELAY_POSITIVE_LOGIC
	config += "R+ ";
#else
	config += "R- ";
#ifdef USE_RELAY_0
	relay0 = !relay0;
#endif
#ifdef USE_RELAY_1
	relay1 = !relay1;
#endif
#endif
#if defined (USE_DHT11_TEMPERATURE)
	config += "TD ";
#endif
#if defined (USE_1WIRE_TEMPERATURE)
	config += "T1 ";
#endif
#if defined (USE_DFPLAYER)
	config += "DF ";
#endif
#if defined (PLAY_TRIGGER_RELAY_0)
	config += "PT ";
#endif
#if defined (USE_MIDI)
	config += "Md ";
#endif
#if defined (USE_DHT11_TEMPERATURE) || defined (USE_1WIRE_TEMPERATURE)
	uint8_t sensorCount = 1;
	char	tempStr[16];
	
#ifdef USE_1WIRE_TEMPERATURE
	sensorCount = sensors.getDeviceCount();
	if (sensorCount == 0)
		sensorCount = 1;
#endif
	doc["temperatureUpdateRate"] = (String)appConfig.temperatureUpdateRate;

	temperature_t	averageTemp;
	averageTemp.tempF = 0.0f;

	for (uint8_t i = 0; i < sensorCount; i++) {
		temperature_t t = updateTemperature(i);

		// Sum for average (works for 1 or many)
		averageTemp.tempF += t.tempF;

		if (sensorCount == 1) {
			snprintf(tempStr, sizeof(tempStr), "%.1f", t.tempF);
			doc["temperature"] = tempStr;
		} else {
			char	key[64];
			char	idStripped[17];
#ifdef USE_1WIRE_TEMPERATURE
			// Strip ':' from "XX:XX:..." into 16-char hex
			uint8_t p = 0;
			for (uint8_t j = 0; j < 24 && t.address[j] != '\0'; j++) {
				if (t.address[j] != ':' && p < 16)
					idStripped[p++] = t.address[j];
			}
			idStripped[p] = '\0';
#else
			strcpy(idStripped, "0");
#endif
			memset(key, 0, sizeof(key));
			strncpy(key, "temperature-", sizeof(key) - 1);
			strncat(key, idStripped, sizeof(key) - strlen(key) - 1);

			snprintf(tempStr, sizeof(tempStr), "%.1f", t.tempF);
			doc[key] = tempStr;
		}
	}

	// Average
	if (sensorCount > 0) {
		averageTemp.tempF /= (float)sensorCount;
		snprintf(tempStr, sizeof(tempStr), "%.1f", averageTemp.tempF);
		doc["temperatureAverage"] = tempStr;	// name as you prefer
	}
	
// 	String	temp = updateTemperature();
// 	doc["temperature"] = temp;
// 	doc["temperatureUpdateRate"] = (String)appConfig.temperatureUpdateRate;
#endif
#ifdef USE_STATUS_0
	doc["status0"] = (uint8_t)(input0 ? 1 : 0);
#endif
#ifdef USE_STATUS_1
	doc["status1"] = (uint8_t)(input1 ? 1 : 0);
#endif
#ifdef USE_RELAY_0
	doc["relay0"] = (uint8_t)(relay0 ? 1 : 0);
#endif
#ifdef USE_RELAY_1
	doc["relay1"] = (uint8_t)(relay1 ? 1 : 0);
#endif

	doc["statusUpdateRate"] = (String)appConfig.statusUpdateRate;
	doc["broker"] = (String)appConfig.MQTTBroker;
	doc["brokerPort"] = (String)appConfig.MQTTPort;
#if defined(USE_DHT11_TEMPERATURE)
	String	humid = updateHumidity();
	doc["humidity"] = humid;
#endif
	config.trim();
	doc["config"] = config;
	doc["uptime"] = millis();

	return doc;
}


// Apply query-string controls and return the current agent status as JSON.
void rootPage() {
	if (!wifi.ready()) {
		return;
	}
	
	char	data[512];

	memset(data, 0, sizeof(data));

	if(http.server().hasArg(F("statusUpdateRate"))) {
		if (!setConfigNumber("statusUpdateRate", http.server().arg(F("statusUpdateRate")))) {
			http.server().send(400, "text/plain", configuration.lastError());
			return;
		}
		D("Set-rate Status: " + String(appConfig.statusUpdateRate));
		timer.cancel(statusTimer);
		if(appConfig.statusUpdateRate > 0)
			statusTimer = timer.every(appConfig.statusUpdateRate * 1000UL, publishStatus, (void *)0);
	}

	if(http.server().hasArg(F("temperatureUpdateRate"))) {
		if (!setConfigNumber("temperatureUpdateRate", http.server().arg(F("temperatureUpdateRate")))) {
			http.server().send(400, "text/plain", configuration.lastError());
			return;
		}
		D("Set-rate Temp: " + String(appConfig.temperatureUpdateRate));
		timer.cancel(temperatureTimer);
		if(appConfig.temperatureUpdateRate > 0)
			temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000UL, publishTemperature, (void *)0);
	}	
	
	if(http.server().hasArg(F("broker"))) {
		String host = http.server().arg(F("broker")); // Existing "host:port" or "host" interface.
		host.trim();
		JsonDocument updates;
		updates["MQTTPort"] = 1883;
		int colon = host.indexOf(':');
		if (colon >= 0) {
			String port = host.substring(colon + 1);
			port.trim();
			if (port.length() && !configuration.setJsonNumberFromText(updates, "MQTTPort", port.c_str())) {
				configError();
				http.server().send(400, "text/plain", configuration.lastError());
				return;
			}
			host = host.substring(0, colon);
			host.trim();
		}
		updates["MQTTBroker"] = host;
		if (!configuration.applyAndSave(updates)) {
			configError();
			http.server().send(400, "text/plain", configuration.lastError());
			return;
		}
		if (host.length()) {
			D(String(F("Set-broker: ")) + host + F(":") + String(appConfig.MQTTPort));
		} else {
			D(F("Set-broker: none"));
			shouldReboot = true;
		}
	}

	if(http.server().hasArg(F("set"))) {
		String state = http.server().arg(F("state"));
		String deviceNum = http.server().arg(F("device-num"));

		byte relay;
#ifdef USE_RELAY_0
		if(deviceNum.toInt() == 0) relay = PIN_RELAY_0;
#else
		if(deviceNum.toInt() == 0) return;
#endif
#ifdef USE_RELAY_1
		if(deviceNum.toInt() == 1) relay = PIN_RELAY_1;
#else
		if(deviceNum.toInt() == 1) return;
#endif
		if(deviceNum.toInt() > 1) return;

			if(state == "on") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " On");

				digitalWrite(relay, LOGIC_HIGH);
			} else if(state == "off") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Off");

				digitalWrite(relay, LOGIC_LOW);
			} else if(state == "pulse") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Pulse");

				digitalWrite(relay, LOGIC_HIGH);
				timer.in(RELAY_TIMEOUT, clearRelay, (void *)deviceNum.toInt());
			}
	}	
	
	DynamicJsonDocument doc = getStatusAsJSON();

	blinkLED((void *)0);
	
	serializeJsonPretty(doc, Serial);
	Serial.println();

	serializeJsonPretty(doc, data, sizeof(data));

	http.server().send(200, "application/json", data);
}


// Send diagnostic messages to the shared display, optionally mirroring them to Serial.
void D(const String& message) {
#ifdef DEBUG_D
	Serial.println("D: " + message);
#endif

	display.D(message);
}

// Initialize the board's I2C bus and OLED; welcome() reports failure once Serial is ready.
void setupDisplay() {
	Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
	display.setup();
}


void setupDFP() {
#ifdef USE_DFPLAYER
	D(F("DFP: Initializing ..."));
	Serial.println(F("DFP: Initializing ..."));

#if IS_ESP8266
	mySoftwareSerial.begin(9600);
#elif IS_ESP32
	mySoftwareSerial.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
#endif
  
	if (!dfp.begin(mySoftwareSerial, false, true)) {
	    Serial.println(F("DFP: Error, unable to communicate."));
	    Serial.println(F("Fatal."));

		while(true){
			delay(0); // Code to compatible with ESP8266 watch dog.
		}
	}
	D(F("DFP: Online."));
	Serial.println(F("DFP: Online."));

	Serial.print(F("DFP: "));
	Serial.print(dfp.readFileCounts());
	Serial.println(F(" items on card."));

	dfp.volume(30);  //Set volume value. From 0 to 30

	dfplayerTimer = timer.every(DFPLAYER_RESET * 1000, dfpReset, (void *)0);
#endif
}


// Show the startup banner and blink the LED, continuing when no OLED is available.
void welcome() {
	display.clear();

	for (int i = 0; i < 3; i++) {
		digitalWrite(PIN_LED, 0);
		delay(50);
		digitalWrite(PIN_LED, 1);
		delay(300);
	}

	Serial.print(F("MQTT Agent, V "));
	Serial.print(VERSION);
	Serial.println(F(". Copyright (C) 2026, Marc D. Spencer"));

	if (!display.ready()) {
		Serial.println(F("OLED initialization failed; continuing without display."));
	}

	D(String(F("MQTT Agent, V")) + VERSION);
	D("");
	D(F("(C) 2026"));
	D(F("Marc D. Spencer"));

	digitalWrite(PIN_LED, 0);
	delay(5000);
	digitalWrite(PIN_LED, 1);

	display.clear();
}


bool clearLED(void* opaque) {
	digitalWrite(PIN_LED, 1);

	return false;
} 

bool blinkLED(void* opaque) {
	digitalWrite(PIN_LED, 0);
	timer.in(50, clearLED, nullptr);
	return true;
}

#ifdef USE_DFPLAYER
bool dfpReset(void* opaque) {
	dfp.reset();	
	return true;
}
#endif

void mqttConnect() {
	char buf[64];
	char data[200];

	client.setServer(appConfig.MQTTBroker, appConfig.MQTTPort);

	Serial.print(F("MQTT: Connecting to broker "));
	Serial.print(appConfig.MQTTBroker);
	Serial.print(F(":"));
	Serial.println(appConfig.MQTTPort);

	client.setCallback(callback);
	client.setBufferSize(512);

	memset(buf, 0, sizeof(buf));
	strncpy(buf, "MQTT-Agent-", sizeof(buf)-1);
	strncat(buf, systemID, sizeof(buf)-1);

	if(client.connect(buf)) {
		D(F("MQTT: Connected."));
		Serial.println(String(F("MQTT: Connected ")) + String(buf));

		if (WiFi.status() == WL_CONNECTED) {
			long rssi = WiFi.RSSI();
			D(String(F("RSSI: ")) + String(rssi) + String(F(" dBm")));
		}
			
		happyBlinkTimer = timer.every(HAPPY_PERIOD, blinkLED, (void *)0);

		memset(buf, 0, sizeof(buf));
		strncpy(buf, "spencer/", sizeof(buf)-1);
		strncat(buf, systemID, sizeof(buf)-1);
		strncat(buf, "/notice", sizeof(buf)-1);
		
		StaticJsonDocument<200> doc;
		doc["id"] = systemID;
		doc["status"] = "online";
		serializeJson(doc, data, sizeof(data));
	
		if (client.publish(buf, data)) {
			Serial.println(F("MQTT: Online."));
			D(F("MQTT: Online."));
			blinkLED((void *)0);
		}
		else {
			Serial.println(F("MQTT: SystemID failed."));
			D(F("MQTT: SystemID failed."));
		}
	} else {
		Serial.println(F("MQTT: Connect failed."));

		if (WiFi.status() == WL_CONNECTED) {
			long rssi = WiFi.RSSI();
			D(String(F("RSSI: ")) + String(rssi) + String(F(" dBm")));
		}

		D(F("MQTT: Connect failed."));

		timer.cancel(happyBlinkTimer);
	}

}


void mqttSubscribe() {
	if (!client.connected()) {
		Serial.println(F("MQTT: Subscribe failed. Not connected."));
		return;
	}
	
	char topic[128];
	
	snprintf(topic, sizeof(topic), "spencer/%s/temperature", systemID);
	client.subscribe(topic);
	
	snprintf(topic, sizeof(topic), "spencer/%s/status", systemID);
	client.subscribe(topic);
	
	snprintf(topic, sizeof(topic), "spencer/%s/relay", systemID);
	client.subscribe(topic);
	
	snprintf(topic, sizeof(topic), "spencer/%s/audio", systemID);
	client.subscribe(topic);
	
	Serial.print(F("MQTT: Subscribed to spencer/")); Serial.print(systemID);
	Serial.println(F("/{temperature,status,relay,audio}"));
	
	if (appConfig.temperatureUpdateRate > 0)
		temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000UL, publishTemperature, (void*)0);
	
	if (appConfig.statusUpdateRate > 0)
		statusTimer = timer.every(appConfig.statusUpdateRate * 1000UL, publishStatus, (void*)0);
}


// Initialize hardware, configuration, application routes, Wi-Fi, and MQTT.
void setup() {
	delay(100);
	
#if defined (USE_RELAY_0) || defined (PLAY_TRIGGER_RELAY_0)
	pinMode(PIN_RELAY_0, OUTPUT);
	digitalWrite(PIN_RELAY_0, LOGIC_LOW);
#endif

#ifdef USE_RELAY_1
	pinMode(PIN_RELAY_1, OUTPUT);
	digitalWrite(PIN_RELAY_1, LOGIC_LOW);
#else
	#if IS_ESP8266
 	pinMode(PIN_LED_1, OUTPUT);
 	digitalWrite(PIN_LED_1, 1);		// PIN_RELAY_1 controls the secondary LED on the NodeMCU, so we force it off.
	#endif
#endif

#ifdef USE_STATUS_0
	pinMode(PIN_STATUS_0, INPUT_PULLUP);
#endif
#ifdef USE_STATUS_1
	pinMode(PIN_STATUS_1, INPUT_PULLUP);
#endif

	pinMode(PIN_LED, OUTPUT);
	digitalWrite(PIN_LED, 1);

	delay(1000);

	setupDisplay();

	int is_reset = 0;

	delay(1000);

	pinMode(PIN_RESET_WIFI, INPUT_PULLUP);
	is_reset = digitalRead(PIN_RESET_WIFI);


#ifdef USE_DHT11_TEMPERATURE
	dht.begin();
#endif
#ifdef USE_1WIRE_TEMPERATURE
	sensors.begin();
#endif
	updateTemperature(0);

	Serial.begin(115200);
	delay(1000);
	Serial.println();

	Serial.print("\r\n\r\n");
	Serial.print("\x1b[0m");           // attributes default
	Serial.print("\x1b[2J\x1b[H");     // clear + home
	// Serial.print("\x1b" "c");       // optional: full terminal reset (not always supported)

	delay(1000);
	Serial.println(F("\n\n"));
	welcome();
	Serial.println();

#ifdef USE_DHT11_TEMPERATURE
	Serial.println(F("Config: DHT11 Temperature enabled"));
	Serial.println(F("Config:   Temperature sensor enabled"));
	Serial.println(F("Config:   Humidity sensor enabled"));
#else
	Serial.println(F("Config: DHT11 Temperature disabled"));
#endif
#ifdef USE_1WIRE_TEMPERATURE
	Serial.println(F("Config: 1-Wire Temperature enabled"));
	Serial.println(F("Config:   Temperature sensor enabled"));
	Serial.printf("Config:   %d devices found.\n", sensors.getDeviceCount());
owInventory();
#else
	Serial.println(F("Config: 1-Wire Temperature disabled"));
#endif
#ifdef RELAY_POSITIVE_LOGIC
	Serial.println(F("Config: Relay positive logic"));
#else
	Serial.println(F("Config: Relay negative logic"));
#endif
#ifdef USE_RELAY_0
	Serial.println(F("Config: Relay 0 enabled"));
#else
	Serial.println(F("Config: Relay 0 disabled"));
#endif
#ifdef USE_RELAY_1
	Serial.println(F("Config: Relay 1 enabled"));
#else
	Serial.println(F("Config: Relay 1 disabled"));
#endif
#ifdef INVERT_STATUS
	Serial.println(F("Config: Status negative logic"));
#else
	Serial.println(F("Config: Status positive logic"));
#endif
#ifdef USE_STATUS_0
	Serial.println(F("Config: Status 0 enabled"));
#else
	Serial.println(F("Config: Status 0 disabled"));
#endif
#ifdef USE_STATUS_1
	Serial.println(F("Config: Status 1 enabled"));
#else
	Serial.println(F("Config: Status 1 disabled"));
#endif
#ifdef USE_DFPLAYER
	Serial.println(F("Config: DFPlayer enabled"));
#ifdef PLAY_TRIGGER_RELAY_0
	Serial.println(F("Config: Play triggers Relay 0"));
#endif
#else
	Serial.println(F("Config: DFPlayer disabled"));
#endif
#ifdef USE_MIDI
	Serial.println(F("Config: MIDI enabled"));
#else
	Serial.println(F("Config: MIDI disabled"));
#endif

// ----------- Configuration ------------------------------------------------------

	if (!configuration.begin()) {
		configError();
		return;
	}
	configuration.dump();

// ----------- WiFi -----------------------------------------------------------

	// Register all application routes before Wi-Fi startup can return early.
	http.server().on("/", rootPage);
	http.server().on("/upload", HTTP_GET, handleUploadPage);
	http.server().on("/upload", HTTP_POST, handleUploadPost, handleUploadStream);
	
	// Holding reset at boot clears saved credentials and forces the configuration portal.
	setupWiFi(is_reset == LOW);
	wifiWasReady = wifi.ready();

	if (!wifiWasReady) {
		return;
	}

	http.begin();

	// Bail out of rest of setup if reset held
	if (is_reset == LOW)
		return;
	
#ifdef USE_DFPLAYER
	Serial.println(F("DFP: Setup."));
	setupDFP();
#endif

#ifdef USE_MIDI
	Serial.println(F("Midi: Setup."));
	midiA.begin(1);	// Launch MIDI and listen to channel 1
#endif
   
	if(strlen(appConfig.MQTTBroker) > 0) {
		mqttConnect();

		mqttSubscribe();
	} else {
		D(F("MQTT: Unconfigured."));
		Serial.println(F("MQTT: Unconfigured.\n"));

		happyBlinkTimer = timer.every(HAPPY_NOMQTT_PERIOD, blinkLED, (void *)0);
	}
	
	Serial.println(F("Ready.\n"));
}


void reboot() {
	D(F("Rebooting ..."));
	Serial.println("Rebooting ...");
	publishReboot(nullptr);

	delay(3000);

#if IS_ESP8266
	ESP.restart();
#else
	ESP.restart();
#endif
}

// Service display scrolling, network connections, timers, and device inputs.
void loop() {
	// Scroll diagnostics even when Wi-Fi is unavailable or provisioning is pending.
	display.tick();

	if (millis() >= rebootTime) {
		reboot();
	}
	// Button Down
	if (rebootPressedTime > 0 && (long)(millis() - rebootPressedTime) >= 0) {
		reboot();
	}
	
	wifi.tick();

	// The library requests the Legacy no-SSID reboot; the application performs it.
	if (wifi.rebootRequested()) {
		rebootTime = 0;
	}

	if (!wifi.ready()) {
		http.stop();
		wifiWasReady = false;
		return;
	}

	if (!wifiWasReady) {
		lastMQTTOnlineCheck = 0;
		wifiWasReady = true;
	}

	// begin() is idempotent; resume HTTP after association and DHCP are ready.
	http.begin();

	bool isConnected = client.loop();
	if((strlen(appConfig.MQTTBroker) > 0) && !isConnected) {
		if(millis() > lastMQTTOnlineCheck) {
			timer.cancel(happyBlinkTimer);
			timer.cancel(temperatureTimer);
			timer.cancel(statusTimer);

			D(F("MQTT: Reconnecting ..."));
			Serial.println("MQTT: Reconnecting ...");
			mqttConnect();
			mqttSubscribe();

			lastMQTTOnlineCheck = millis() + ONLINE_CHECK_PERIOD;
		}
	}

	http.tick();
	delay(0);

	if (shouldReboot) {
		D(F("Rebooting ..."));
		reboot();
	}

#ifdef USE_DFPLAYER
	if (dfp.available()) {
		dfpPrintDetail(dfp.readType(), dfp.read());
	}
#endif

	timer.tick();
	
	bool is_reset = digitalRead(PIN_RESET_WIFI);
	if(!is_reset && !reset_is_down) {
		reset_is_down = true;
		rebootPressedTime = millis() + REBOOT_BUTTON_TIME;
		D("MQTT Agent, V " + String(VERSION));
		D("");
		D("IP: " + WiFi.localIP().toString());
		D("ID: " + String(systemID));
		Serial.println("WiFi: IP: " + WiFi.localIP().toString());
	}
	is_reset = digitalRead(PIN_RESET_WIFI);
	if(is_reset && reset_is_down) {
		reset_is_down = false;
		rebootPressedTime = 0;
	}
	
#ifdef USE_STATUS_0
	bool status0 = ! digitalRead(PIN_STATUS_0);
#else
	bool status0 = false;
#endif
#ifdef USE_STATUS_1
	bool status1 = ! digitalRead(PIN_STATUS_1);
#else
	bool status1 = false;
#endif

	if((status0 != lastStatus0) || (status1 != lastStatus1)) {
		publishStatus((void *)0);
		lastStatus0 = status0;
		lastStatus1 = status1;
	}
}
