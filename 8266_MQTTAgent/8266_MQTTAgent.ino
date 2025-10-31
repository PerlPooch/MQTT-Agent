// ---- Configuration -------------------------------------------------------------------
//
// #define USE_1WIRE_TEMPERATURE
#define USE_DHT11_TEMPERATURE
// #define USE_RELAY_0
// RELAY_1 and DFPlayer are mutually exclusive
// #define USE_RELAY_1
// #define USE_STATUS_0
// STATUS_1 and DFPlayer are mutually exclusive
// #define USE_STATUS_1
// #define USE_DFPLAYER
// #define PLAY_TRIGGER_RELAY_0
// #define USE_MIDI
// #define RELAY_POSITIVE_LOGIC
#define RELAY_NEGATIVE_LOGIC
//
// --------------------------------------------------------------------------------------

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <PubSubClient.h>
#ifdef USE_1WIRE_TEMPERATURE
#include <OneWire.h>
#include <DallasTemperature.h>
#endif
#include <arduino-timer.h>
#include <FS.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#ifdef USE_DHT11_TEMPERATURE
#include "DHT.h"
#endif
#ifdef USE_DFPLAYER
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#endif
#ifdef USE_MIDI
#include <MIDI.h>
#endif



#define VERSION				"1.6"


#define SCREEN_WIDTH		128		// OLED display width, in pixels
#define SCREEN_HEIGHT		32		// OLED display height, in pixels
#define OLED_RESET    		-1		// Reset pin # (or -1 if sharing Arduino reset pin)
// #define DEBUG_D						// Mirror screen to serial

#if defined (USE_RELAY_0) || defined (PLAY_TRIGGER_RELAY_0)
#define PIN_RELAY_0 		0
#endif
#ifdef USE_RELAY_1
#define PIN_RELAY_1 		2
#endif
#define PIN_LED_1			2		// NodeMCU Secondary LED (shares pin with relay!)
#ifdef USE_STATUS_0
#define PIN_STATUS_0 		10
#endif
#ifdef USE_STATUS_1
#define PIN_STATUS_1 		13
#endif
#define PIN_RESET_WIFI 		12
#define PIN_TEMP_0		 	14
#define PIN_LED 			16

#define DHT_TYPE			DHT11

#undef AUTOCONNECT_MENULABEL_HOME
#define AUTOCONNECT_MENULABEL_HOME        "Activate"
#define AC_USE_SPIFFS

#define CONFIG_FILE			"/config.json"
#define AUX_SETTING_URI		"/mqtt_setting"
#define AUX_SAVE_URI		"/mqtt_save"
#define AUX_CLEAR_URI		"/mqtt_clear"


#define	DISPLAY_TIMEOUT		5000	// Time(ms) between each display scroll
#define	DISPLAY_LINES		4		// Number of text lines that fit on the display
#define	RELAY_TIMEOUT		500 	// Time(ms) for relay pulses
#define	HAPPY_PERIOD		5000	// Time(ms) for happy LED blinks
#define	HAPPY_NOMQTT_PERIOD	10000	// Time(ms) for happy LED blinks
#define	DFPLAYER_RESET		3600	// Time(s) between resetting DFPlayer
#define ONLINE_CHECK_PERIOD 10000	// Time(ms) between checks to see if we're still online

#ifdef RELAY_POSITIVE_LOGIC
#define LOGIC_HIGH 1
#define LOGIC_LOW 0
#else
#define LOGIC_HIGH 0
#define LOGIC_LOW 1
#endif

char				systemID[32];		// System ID. Based on the WiFi MAC

ESP8266WebServer 	Server;
AutoConnect      	Portal(Server);

#ifdef USE_1WIRE_TEMPERATURE
OneWire 			oneWire(PIN_TEMP_0);
DallasTemperature 	sensors(&oneWire);
#endif

#ifdef USE_DHT11_TEMPERATURE
DHT					dht(PIN_TEMP_0, DHT_TYPE);
#endif

#ifdef USE_DFPLAYER
SoftwareSerial mySoftwareSerial(13, 2); // RX, TX  D4, D7
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

Adafruit_SSD1306	display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Timer<>::Task		happyBlinkTimer;
Timer<>::Task		scrollTimer;
Timer<>::Task		temperatureTimer;
Timer<>::Task		statusTimer;
#ifdef USE_DFPLAYER
Timer<>::Task		dfplayerTimer;
#endif
auto				timer = timer_create_default();
unsigned long		lastMQTTOnlineCheck;

struct AppConfig {
	char		MQTTBroker[64];
	uint16_t	MQTTPort;
	uint16_t	temperatureUpdateRate;
	uint16_t	statusUpdateRate;
};
AppConfig appConfig;

struct Screen {
	String		lines[DISPLAY_LINES];
};
Screen screen;

bool lastStatus0, lastStatus1;
bool reset_is_down = false;

static const char AUX_mqtt_setting[] PROGMEM = R"raw(
[
  {
    "title": "Configure MQTT",
    "uri": "/mqtt_setting",
    "menu": true,
    "element": [
      {
        "name": "style",
        "type": "ACStyle",
        "value": "label+input,label+select{position:sticky;left:120px;width:230px!important;box-sizing:border-box;}"
      },
      {
        "name": "mqttserver",
        "type": "ACInput",
        "value": "",
        "label": "Broker",
        "pattern": "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$",
        "placeholder": "MQTT Broker"
      },
      {
        "name": "mqttport",
        "type": "ACInput",
        "value": "",
        "label": "Port",
        "placeholder": "MQTT Port"
      },
      {
        "name": "save",
        "type": "ACSubmit",
        "value": "Save",
        "uri": "/mqtt_save"
      }
    ]
  },
  {
    "title": "MQTT",
    "uri": "/mqtt_save",
    "menu": false,
    "element": [
      {
        "name": "style",
        "type": "ACStyle",
        "value": "label+input,label+select{position:sticky;left:120px;width:230px!important;box-sizing:border-box;}"
      },
      {
        "name": "parameters",
        "type": "ACText",
        "value": "Saved."
      }
    ]
  }
]
)raw";


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
			break;
		case DFPlayerUSBInserted:
			Serial.println("DFP: USB Inserted.");
			break;
		case DFPlayerUSBRemoved:
			Serial.println("DFP: USB Removed.");
			break;
		case DFPlayerPlayFinished:
			Serial.print(F("DFP: Number:"));
			Serial.print(value);
			Serial.println(F(" Play Finished."));
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

void printFile(const char *filename) {
	File file = SPIFFS.open(filename, "r");
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

void listDir(fs::FS &fs, const char *dirname){
	Serial.printf("  Directory %s:\n", dirname);

	String str = "";
	Dir dir = fs.openDir(dirname);
	while (dir.next()) {
		str += F("    ");
		str += dir.fileName();
		str += F(" (");
		str += dir.fileSize();
		str += F("b)\n");
	}
	Serial.print(str);
}

bool loadConfiguration(const char *filename, AppConfig &config) {
	File file = SPIFFS.open(filename, "r");

	if (file) {
		StaticJsonDocument<512> doc;

		DeserializationError error = deserializeJson(doc, file);

		if (error) {
			Serial.println(F("Unable to read configuration"));
			file.close();
			return false;
		} else {
			strlcpy(config.MQTTBroker, doc["MQTTBroker"], sizeof(config.MQTTBroker));

			config.MQTTPort = doc["MQTTPort"];

			config.temperatureUpdateRate = doc["temperatureUpdateRate"];
//			if(config.temperatureUpdateRate == NULL) config.temperatureUpdateRate = 1;

			config.statusUpdateRate = doc["statusUpdateRate"];
//			if(config.statusUpdateRate == NULL) config.statusUpdateRate = 1;
		} 

		file.close();
		return true;
	} else {
		Serial.println(F("Unable to open configuration."));
		return false;
	}
}


void saveConfiguration(const char *filename, const AppConfig &config) {
	SPIFFS.remove(filename);

	File file = SPIFFS.open(filename, "w");
	if (!file) {
		Serial.println(F("Unable to create configuration file"));
		return;
	}

	StaticJsonDocument<512> doc;

	doc["MQTTBroker"] = config.MQTTBroker;
	doc["MQTTPort"] = config.MQTTPort;
	doc["temperatureUpdateRate"] = config.temperatureUpdateRate;
	doc["statusUpdateRate"] = config.statusUpdateRate;

	if (serializeJson(doc, file) == 0) {
		Serial.println(F("Unable to write configuration file"));
	}

	file.close();
}


String saveMQTTParams(AutoConnectAux& aux, PageArgument& args) {
	AutoConnectAux* mqtt_setting = Portal.aux(Portal.where());
	AutoConnectInput& serverInput = mqtt_setting->getElement<AutoConnectInput>("mqttserver");
	AutoConnectInput& portInput = mqtt_setting->getElement<AutoConnectInput>("mqttport");

	String serverValue = serverInput.value;
	String portValue = portInput.value;

	strncpy(appConfig.MQTTBroker, serverValue.c_str(), sizeof(appConfig.MQTTBroker));
	appConfig.MQTTPort = (uint16_t)portValue.toInt();

	D(F("Configuration updated."));

	saveConfiguration(CONFIG_FILE, appConfig);
	printFile(CONFIG_FILE);
	
	AutoConnectText&  result = aux["parameters"].as<AutoConnectText>();
	result.value = "Broker: " + serverValue + ":" + portValue;

	return String("");
}


bool clearRelay(void* opaque) {
	size_t relayNum = (size_t)opaque;

	byte relay;
#ifdef USE_RELAY_0
	if(relayNum == 0) relay = PIN_RELAY_0;
#endif
#ifdef USE_RELAY_1
	if(relayNum == 1) relay = PIN_RELAY_1;
#endif

	digitalWrite(relay, LOGIC_LOW);
	return false; // repeat?
}


String updateTemperature() {
	float 	temp;
	char	data[200];

#ifdef USE_1WIRE_TEMPERATURE
	sensors.requestTemperatures(); // Send the command to get temperatures
	temp = sensors.getTempFByIndex(0);
#elif defined(USE_DHT11_TEMPERATURE)
	temp = dht.readTemperature(1);
#else
	temp = 0.0f;
#endif

	snprintf(data, sizeof(data), "%0.1f", temp);

	return String(data);
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
	char	buf[64];

	String temp = updateTemperature();
	D(String(F("Temp 0: ")) + temp);

	memset(buf, 0, sizeof(buf));
	strncpy(buf, "spencer/", sizeof(buf));
	strcat(buf, systemID);
	strcat(buf, "/temperature");
	
	blinkLED((void *)0);
	
	if(strlen(appConfig.MQTTBroker) > 0) {
		StaticJsonDocument<200> doc;

		doc["id"] = systemID;
		doc["temperature"] = temp;
		doc["updateRate"] = (String)appConfig.temperatureUpdateRate;

		serializeJson(doc, data, sizeof(data));
	
		if (client.publish(buf, data)) {
		}
	
#ifdef USE_DHT11_TEMPERATURE
		publishHumidity(0);
#endif
	}
	
	return true;
}


bool publishHumidity(void* opaque) {
	char	data[200];
	char	buf[64];

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
#ifdef USE_POSITIVE_LOGIC
#else
	relay0 = !relay0;
	relay1 = !relay1;
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
		doc["relay0"] = (String)relay0;
#endif
#ifdef USE_RELAY_1
		doc["relay1"] = (String)relay1;
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
	strncpy(buf, (char*)in_message, length);
	buf[length] = 0;
	message = String(buf);
	Serial.println("t: " + String(in_topic));
	Serial.println("m: " + message);

	StaticJsonDocument<512> doc;

	DeserializationError error = deserializeJson(doc, message);
	const char* jsonValue;

	if (error) {
		Serial.println(F("Unable to read message"));
		return;
	}

	// --- Command ---
	jsonValue = doc["command"];
	command = String(jsonValue);

	if(command.length() == 0) {
		return;
	}
	Serial.println("Command: " + command);

	// Parse the topic into parts.
	// we use spencer/systemID/<device>
	const char delimeter[2] = "/";

	// Spencer prefix
	token = strtok(in_topic, delimeter);
	Serial.println("1: " + String(token));

	// Since we only subscribed to topics matching our systemID, we don't need to check it here.
	token = strtok(NULL, delimeter);
	Serial.println("2: " + String(token));

	// Now get the device
	token = strtok(NULL, delimeter);
	device = String(token);
	Serial.println("3: " + device);

 
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
		jsonValue = doc["rate"];
		String rate = String(jsonValue);

		if(rate.length() == 0) {
			Serial.println("rate missing");
			return;
		}
 	
	 	if(device == "temperature") {
 			appConfig.temperatureUpdateRate = rate.toInt();
 			D("Set-rate Temp: " + String(appConfig.temperatureUpdateRate));
 			saveConfiguration(CONFIG_FILE, appConfig);
 			timer.cancel(temperatureTimer);
 			if(appConfig.temperatureUpdateRate > 0)
 				temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000, publishTemperature, (void *)0);
		} else if(device == "status") {
 			appConfig.statusUpdateRate = rate.toInt();
 			D("Set-rate Status: " + String(appConfig.statusUpdateRate));
 			saveConfiguration(CONFIG_FILE, appConfig);
 			timer.cancel(statusTimer);
 			if(appConfig.statusUpdateRate > 0)
 				statusTimer = timer.every(appConfig.statusUpdateRate * 1000, publishStatus, (void *)0);
		}
 	} else if(command == "set") {
		jsonValue = doc["device-num"];
		String deviceNum = String(jsonValue);

		jsonValue = doc["state"];
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
		jsonValue = doc["item"];
		String item = String(jsonValue);

		if(item.length() == 0) {
			Serial.println("item missing");
			return;
		}
 	
#ifdef USE_DFPLAYER
 		D("Play item " + item);
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
	char	buff[10];
	String	config;

	DynamicJsonDocument doc(400);

	doc["version"] = String(VERSION);

	if (WiFi.status() == WL_CONNECTED) {
		long rssi = WiFi.RSSI();

		doc["rssi"] = String(rssi);
		doc["ip"] = WiFi.localIP().toString();
		doc["id"] = systemID;
	}

#ifdef USE_STATUS_0
	bool	input0 = ! digitalRead(PIN_STATUS_0);
	config += "I0 ";
#endif
#ifdef USE_STATUS_1
	bool	input1 = ! digitalRead(PIN_STATUS_1);
	config += "I1 ";
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
	String	temp = updateTemperature();
	doc["temperature"] = temp;
	doc["temperatureUpdateRate"] = (String)appConfig.temperatureUpdateRate;
#endif
#ifdef USE_STATUS_0
	doc["status0"] = (String)input0;
#endif
#ifdef USE_STATUS_1
	doc["status1"] = (String)input1;
#endif
#ifdef USE_RELAY_0
	doc["relay0"] = (String)relay0;
#endif
#ifdef USE_RELAY_1
	doc["relay1"] = (String)relay1;	
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

	return doc;
}


void rootPage() {
	char	data[400];

	if(Server.hasArg(F("statusUpdateRate"))) {
		appConfig.statusUpdateRate = Server.arg(F("statusUpdateRate")).toInt();
		D("Set-rate Status: " + String(appConfig.statusUpdateRate));
		saveConfiguration(CONFIG_FILE, appConfig);
		timer.cancel(statusTimer);
		if(appConfig.statusUpdateRate > 0)
			statusTimer = timer.every(appConfig.statusUpdateRate * 1000, publishStatus, (void *)0);
	}

	if(Server.hasArg(F("temperatureUpdateRate"))) {
		appConfig.temperatureUpdateRate = Server.arg(F("temperatureUpdateRate")).toInt();
		D("Set-rate Temp: " + String(appConfig.temperatureUpdateRate));
		saveConfiguration(CONFIG_FILE, appConfig);
		timer.cancel(temperatureTimer);
		if(appConfig.temperatureUpdateRate > 0)
			temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000, publishTemperature, (void *)0);
	}	
	
	if(Server.hasArg(F("set"))) {
		String state = Server.arg(F("state"));
		String deviceNum = Server.arg(F("device-num"));

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

	Server.send(200, "application/json", data);
}


bool clearMessage(void* opaque) 
{
	D("");
			
	return true;
}

void clearDisplay() 
{
	display.clearDisplay();
	display.setCursor(0,0);
	display.display();
	
	for(int i = 0; i < DISPLAY_LINES; i++)
		screen.lines[i] = String();
}

void D(String m)
{
	for(int i = 0; i < DISPLAY_LINES-1; i++) {
		screen.lines[i] = screen.lines[i + 1];
	}
	screen.lines[DISPLAY_LINES-1] = m;
	
#ifdef DEBUG_D
	Serial.println("D: " + m);
#endif

	display.clearDisplay();
	display.setCursor(0,0);
	
	for(int i = 0; i < DISPLAY_LINES; i++) {
		display.println(screen.lines[i]);
//		Serial.println("D[" + String(i) + "]: " + screen.lines[i]);
	}

	if(m.length() != 0) {
		timer.cancel(scrollTimer);
		scrollTimer = timer.every(DISPLAY_TIMEOUT, clearMessage, (void *)0);
	}

	display.display();
}

void setupSystem()
{
	// Compute the systemID -- The unique ID for this device. This will be used as the root leaf
	// for the MQTT topic
	String mac = WiFi.macAddress();
	mac.getBytes((byte *)systemID, sizeof(systemID));
}


void setupDisplay()
{
	if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
		Serial.println(F("SSD1306 allocation failed"));
		for(;;); // Don't proceed, loop forever
	}

	clearDisplay();

	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setTextWrap(false);
	display.setCursor(0,0);

	display.display();

	for(int i = 0; i < DISPLAY_LINES; i++)
		screen.lines[i] = String();

	scrollTimer = timer.every(DISPLAY_TIMEOUT, clearMessage, (void *)0);
}


void setupDFP() {
#ifdef USE_DFPLAYER
	D(F("DFP: Initializing..."));
	Serial.println(F("DFP: Initializing..."));

	mySoftwareSerial.begin(9600);
  
	if (!dfp.begin(mySoftwareSerial, false, true)) {
	    Serial.println(F("DFP: Error, unable to communicate."));
	    Serial.println(F("Fatal."));

		while(true){
			delay(0); // Code to compatible with ESP8266 watch dog.
		}
	}
	D(F("DFP: Online."));
	Serial.println(F("DFP: Online."));

	dfp.volume(30);  //Set volume value. From 0 to 30

	dfplayerTimer = timer.every(DFPLAYER_RESET * 1000, dfpReset, (void *)0);
#endif
}


void welcome() {
	display.clearDisplay();

	display.setCursor(0,0);

	for(int i = 0; i < 3; i++) {
		digitalWrite(PIN_LED, 0);
		delay(50);
		digitalWrite(PIN_LED, 1);
		delay(300);
	}

	display.print(F("MQTT Agent, V"));
	display.println(VERSION); 
	display.println(); 
	display.println(F("(C) 2024"));
	display.println(F("Marc D. Spencer")); 

	display.display();

	digitalWrite(PIN_LED, 0);
	delay(5000);
	digitalWrite(PIN_LED, 1);

	display.clearDisplay();
	display.display();
}


bool clearLED(void* opaque) {
	digitalWrite(PIN_LED, 1);

	return false;
} 

bool blinkLED(void* opaque) {
	digitalWrite(PIN_LED, 0);

	timer.every(50, clearLED, (void *)0);
	
	return true;
}

#ifdef USE_DFPLAYER
bool dfpReset(void* opaque) {
	dfp.reset();	
	return true;
}
#endif

void mqttConnect() {
	char buf[32];
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

		if (WiFi.status() == WL_CONNECTED) {
			long rssi = WiFi.RSSI();
			D(String(F("RSSI: ")) + String(rssi) + String(F(" dBm")));
		}

		Serial.println(F("MQTT: Connected."));
			
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
			Serial.println(F("MQTT: SystemID OK."));
			D(F("MQTT: SystemID OK."));
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
	if(client.connected()) {
		char buf[32];

		memset(buf, 0, sizeof(buf));
		strncpy(buf, "spencer/", sizeof(buf));
		strcat(buf, systemID);
		strcat(buf, "/+");

		Serial.print("MQTT: Subscribing to ");
		Serial.println(buf);

		client.subscribe(buf);
	
		if(appConfig.temperatureUpdateRate > 0)
			temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000, publishTemperature, (void *)0);

		if(appConfig.statusUpdateRate > 0)
			statusTimer = timer.every(appConfig.statusUpdateRate * 1000, publishStatus, (void *)0);
	} else {
		D(F("MQTT: Not connected."));
	}
}


void setup() {
#if defined (USE_RELAY_0) || defined (PLAY_TRIGGER_RELAY_0)
	pinMode(PIN_RELAY_0, OUTPUT);
	digitalWrite(PIN_RELAY_0, LOGIC_LOW);
#endif

#ifdef USE_RELAY_1
	pinMode(PIN_RELAY_1, OUTPUT);
	digitalWrite(PIN_RELAY_1, LOGIC_LOW);
#else
 	pinMode(PIN_LED_1, OUTPUT);
 	digitalWrite(PIN_LED_1, 1);		// PIN_RELAY_1 controls the secondary LED on the NodeMCU, so we force it off.
#endif

#ifdef USE_STATUS_0
	pinMode(PIN_STATUS_0, INPUT_PULLUP);
#endif
#ifdef USE_STATUS_1
	pinMode(PIN_STATUS_1, INPUT_PULLUP);
#endif

	pinMode(PIN_LED, OUTPUT);
	digitalWrite(PIN_LED, 1);

	setupSystem();
	setupDisplay();

	int is_reset = 0;


	delay(1000);

	pinMode(PIN_RESET_WIFI, INPUT_PULLUP);
	is_reset = digitalRead(PIN_RESET_WIFI);


#ifdef USE_DHT11_TEMPERATURE
	dht.begin();
#endif


	Serial.begin(115200);
	Serial.println();

	Serial.print(F("\n"));
	Serial.print(F(VERSION));
	Serial.println(F("\n\n"));

	if(!SPIFFS.begin()) {
		Serial.println(F("SPIFFS: Mount Failed"));
		return;
	} else {
		Serial.println(F("SPIFFS: OK"));
		listDir(SPIFFS, "/");
	}

	AutoConnectConfig acConfig;

	welcome();

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

	Serial.println();

	// Should load default config if run for the first time
	Serial.println(F("Loading configuration..."));
	if(! loadConfiguration(CONFIG_FILE, appConfig)) {
		Serial.println(F("Saving default configuration..."));
	 	saveConfiguration(CONFIG_FILE, appConfig);
	}
	Serial.println(F("Done."));

	// Dump config file
// 	Serial.println(F("Print config file..."));
// 	printFile(CONFIG_FILE);


	Server.on("/", rootPage);


	acConfig.apid = "OSCR " + String(systemID);
	acConfig.psk = "12345678";
	acConfig.ota = AC_OTA_BUILTIN;
	acConfig.title = String(systemID);
	acConfig.homeUri = "/";
	acConfig.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_DISCONNECT | AC_MENUITEM_RESET | AC_MENUITEM_UPDATE;

	if(Portal.load(FPSTR(AUX_mqtt_setting))) {
		AutoConnectAux& mqtt_setting = *Portal.aux(AUX_SETTING_URI);

		AutoConnectInput& broker = mqtt_setting.getElement<AutoConnectInput>("mqttserver");
		broker.value = appConfig.MQTTBroker;

 		AutoConnectInput& port = mqtt_setting.getElement<AutoConnectInput>("mqttport");
 		port.value = appConfig.MQTTPort;

		Portal.on(AUX_SAVE_URI, saveMQTTParams);
    } else {
		Serial.println("load error");
	}

	if(SPIFFS.exists("/ac_credt")) {
		Portal.restoreCredential("/ac_credt", SPIFFS);
	} else {
		D(F("WiFi AP Configuration"));
		D("");
		D(String(acConfig.apid));
		D("PW: " + String(acConfig.psk));
    }

	if (is_reset == LOW) {
		SPIFFS.remove("/ac_credt");

		D(F("WiFi AP Configuration"));
		D("");
		D(String(acConfig.apid));
		D("PW: " + String(acConfig.psk));
		acConfig.immediateStart = true;
		acConfig.autoRise = true;
	}

	Portal.config(acConfig);

	if (Portal.begin()) {  
		Portal.saveCredential("/ac_credt", SPIFFS);
		D("IP: " + WiFi.localIP().toString());
	} else {
	}

	if (is_reset == LOW)
		return;

#ifdef USE_DFPLAYER
	setupDFP();
#endif

#ifdef USE_MIDI
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


void loop() {
	Portal.handleClient();

	bool isConnected = client.loop();
	if((strlen(appConfig.MQTTBroker) > 0) && ! isConnected) {
		if(millis() > lastMQTTOnlineCheck) {
			timer.cancel(happyBlinkTimer);
			timer.cancel(temperatureTimer);
			timer.cancel(statusTimer);

			D(F("MQTT: Reconnecting..."));
			Serial.println("MQTT: Reconnecting...");
			mqttConnect();
			mqttSubscribe();

			lastMQTTOnlineCheck = millis() + ONLINE_CHECK_PERIOD;
		}
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
		D("IP: " + WiFi.localIP().toString());
		D("ID: " + String(systemID));
		Serial.println("IP: " + WiFi.localIP().toString());
	}
	is_reset = digitalRead(PIN_RESET_WIFI);
	if(is_reset && reset_is_down) {
		reset_is_down = false;
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
