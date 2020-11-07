#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <arduino-timer.h>
#include <FS.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


#define VERSION				"1.1"

#define SCREEN_WIDTH		128		// OLED display width, in pixels
#define SCREEN_HEIGHT		32		// OLED display height, in pixels
#define OLED_RESET    		-1		// Reset pin # (or -1 if sharing Arduino reset pin)

#define PIN_RELAY_0 		0
#define PIN_RELAY_1 		2
#define PIN_STATUS_0 		10
#define PIN_STATUS_1 		13
#define PIN_RESET_WIFI 		12
#define PIN_ONE_WIRE_BUS 	14
#define PIN_LED 			16

#define AUTOCONNECT_MENULABEL_HOME        "Activate"

#define CONFIG_FILE			"/config.json"
#define AUX_SETTING_URI		"/mqtt_setting"
#define AUX_SAVE_URI		"/mqtt_save"
#define AUX_CLEAR_URI		"/mqtt_clear"

#define	DISPLAY_TIMEOUT		5000	// Time(ms) between each display scroll
#define	DISPLAY_LINES		4		// Number of text lines that fit on the display
#define	RELAY_TIMEOUT		500 	// Time(ms) for relay pulses
#define	HAPPY_PERIOD		5000	// Time(ms) for relay pulses



char				systemID[32];		// System ID. Based on the WiFi MAC

ESP8266WebServer 	Server;
AutoConnect      	Portal(Server);

OneWire 			oneWire(PIN_ONE_WIRE_BUS);
DallasTemperature 	sensors(&oneWire);

WiFiClient 			wifiClient;
PubSubClient 		client(wifiClient);

Adafruit_SSD1306	display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Timer<>::Task		happyBlinkTimer;;
Timer<>::Task		scrollTimer;
Timer<>::Task		temperatureTimer;
Timer<>::Task		statusTimer;
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


void loadConfiguration(const char *filename, AppConfig &config) {
	File file = SPIFFS.open(filename, "r");

	if (file) {
		StaticJsonDocument<512> doc;

		DeserializationError error = deserializeJson(doc, file);

		if (error)
			Serial.println(F("Unable to read configuration"));
		else {
			strlcpy(config.MQTTBroker, doc["MQTTBroker"], sizeof(config.MQTTBroker));

			config.MQTTPort = doc["MQTTPort"];

			config.temperatureUpdateRate = doc["temperatureUpdateRate"];
//			if(config.temperatureUpdateRate == NULL) config.temperatureUpdateRate = 1;

			config.statusUpdateRate = doc["statusUpdateRate"];
//			if(config.statusUpdateRate == NULL) config.statusUpdateRate = 1;
		} 

		file.close();
	} else {
		Serial.println(F("Unable to open configuration."));
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
	if(relayNum == 0) relay = PIN_RELAY_0;
	if(relayNum == 1) relay = PIN_RELAY_1;

	digitalWrite(relay, 0);
	return false; // repeat?
}


String updateTemperature() {
	sensors.requestTemperatures(); // Send the command to get temperatures
	
	float 	temp = sensors.getTempFByIndex(0);
	char	data[200];

	snprintf(data, sizeof(data), "%0.1f", temp);

	return String(data);
}

bool publishTemperature(void* opaque) {
	char	data[200];
	char	buf[64];

	String temp = updateTemperature();
	D(String(F("Temp 0: ")) + temp);

	memset(buf, 0, sizeof(buf));
	strncpy(buf, systemID, sizeof(buf));
	strcat(buf, "/temperature");
	
	blinkLED((void *)0);
	
	StaticJsonDocument<200> doc;

	doc["id"] = systemID;
	doc["temperature"] = temp;
	doc["updateRate"] = (String)appConfig.temperatureUpdateRate;

	serializeJson(doc, data, sizeof(data));
	
	if (client.publish(buf, data)) {
	}
	
	return true;
}


bool publishStatus(void* opaque) {
	char	data[200];
	char	buf[64];

	bool	input0 = false;
	bool	input1 = false;

	input0 = ! digitalRead(PIN_STATUS_0);
	input1 = ! digitalRead(PIN_STATUS_1);

	D(String(F("Status: ")) + String(input0) + String(F(" ")) + String(input1));

	memset(buf, 0, sizeof(buf));
	strncpy(buf, systemID, sizeof(buf));
	strcat(buf, "/status");
	
	blinkLED((void *)0);
	
	StaticJsonDocument<200> doc;

	doc["id"] = systemID;
	doc["status0"] = (String)input0;
	doc["status1"] = (String)input1;
	doc["updateRate"] = (String)appConfig.statusUpdateRate;

//  	serializeJsonPretty(doc, Serial);
//  	Serial.println();

	serializeJson(doc, data, sizeof(data));
	
	if (client.publish(buf, data)) {
	}
	
	return true;
}


bool publishRelays(void* opaque) {
	char	data[200];
	char	buf[64];

	bool relay0 = digitalRead(PIN_RELAY_0);
	bool relay1 = digitalRead(PIN_RELAY_1);

	D(String(F("Relays: ")) + String(relay0) + String(F(" ")) + String(relay1));

	memset(buf, 0, sizeof(buf));
	strncpy(buf, systemID, sizeof(buf));
	strcat(buf, "/relay");
	
	blinkLED((void *)0);
	
	StaticJsonDocument<200> doc;

	doc["id"] = systemID;
	doc["relay0"] = (String)relay0;
	doc["relay1"] = (String)relay1;

	serializeJson(doc, data, sizeof(data));
	
	if (client.publish(buf, data)) {
	}
	
	return true;
}


void callback(char* in_topic, byte* in_message, unsigned int length) {
	String	message;
	char*	token;
	char	buf[64];

	String	device;
	String	command;
	String	args;

	// Convert in_message to a String
	strncpy(buf, (char*)in_message, length);
	buf[length] = 0;
	message = String(buf);
	Serial.println("m: " + message);

	StaticJsonDocument<512> doc;

	DeserializationError error = deserializeJson(doc, message);
	const char* jsonValue;

	if (error) {
		Serial.println(F("Unable to read message"));
		return;
	}

	// Parse the topic into parts.
	// we use systemID/<device>
	const char delimeter[2] = "/";

	// Since we only subscribed to topics matching our systemID, we don't need to check it here.
	token = strtok(in_topic, delimeter);
	Serial.println("1: " + String(token));

	// Now get the device
	token = strtok(NULL, delimeter);
	device = String(token);
	Serial.println("2: " + device);

	// --- Command ---
	jsonValue = doc["command"];
	command = String(jsonValue);

	if(command.length() == 0) {
		return;
	}
	Serial.println("Command: " + command);
 
 	if(command == "fetch") {
	 	if(device == "temperature") {
			publishTemperature((void *)0);
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
			Serial.println("state");
			return;
		}
 	
	 	if(device == "relay") {
			byte relay;
			if(deviceNum.toInt() == 0) relay = PIN_RELAY_0;
			if(deviceNum.toInt() == 1) relay = PIN_RELAY_1;
			if(deviceNum.toInt() > 1) return;
			
			if(state == "on") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " On");

				digitalWrite(relay, 1);
			} else if(state == "off") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Off");

				digitalWrite(relay, 0);
			} else if(state == "pulse") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Pulse");

				digitalWrite(relay, 1);
				timer.in(RELAY_TIMEOUT, clearRelay, (void *)deviceNum.toInt());
			}
		}
	}
}




void rootPage() {
	char	buff[10];
	char	data[200];

	StaticJsonDocument<200> doc;

	bool	input0 = ! digitalRead(PIN_STATUS_0);
	bool	input1 = ! digitalRead(PIN_STATUS_1);
	bool 	relay0 = digitalRead(PIN_RELAY_0);
	bool	relay1 = digitalRead(PIN_RELAY_1);
	String	temp = updateTemperature();

	doc["status0"] = (String)input0;
	doc["status1"] = (String)input1;
	doc["relay0"] = (String)relay0;
	doc["relay1"] = (String)relay1;	
	doc["temperature"] = temp;
	doc["temperatureUpdateRate"] = (String)appConfig.temperatureUpdateRate;
	doc["statusUpdateRate"] = (String)appConfig.statusUpdateRate;
	doc["broker"] = (String)appConfig.MQTTBroker;
	doc["brokerPort"] = (String)appConfig.MQTTPort;

	serializeJsonPretty(doc, Serial);
	Serial.println();

	serializeJson(doc, data, sizeof(data));

	Server.send(200, "application/json", data);
}




bool clearMessage(void* opaque) 
{
	D("");
			
	return true;
}

bool clearDisplay() 
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
	// Compute the systemID -- The unique ID for this devicd. This will be used as the root leaf
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


void welcome() {
	display.clearDisplay();

	display.setCursor(0,0);

	display.print(F("MQTT Agent, V"));
	display.println(VERSION); 
	display.println(); 
	display.println(F("(C) 2020")); 
	display.println(F("Marc D. Spencer")); 

	display.display();

	delay(5000);
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


void mqttConnect() {
	char buf[32];
	char data[200];

	client.setServer(appConfig.MQTTBroker, appConfig.MQTTPort);
	client.setCallback(callback);

	memset(buf, 0, sizeof(buf));
	strncpy(buf, "MQTT-Agent-", sizeof(buf));
	strncat(buf, systemID, sizeof(buf));

	if(client.connect(buf)) {

		D(F("MQTT: Connected."));
			
		happyBlinkTimer = timer.every(HAPPY_PERIOD, blinkLED, (void *)0);

		memset(buf, 0, sizeof(buf));
		strncpy(buf, systemID, sizeof(buf));
		strncat(buf, "/notice", sizeof(buf));
		Serial.println(buf);
		
		StaticJsonDocument<200> doc;
		doc["id"] = systemID;
		doc["status"] = "online";
		serializeJson(doc, data, sizeof(data));
	
		if (client.publish(buf, data)) {
			D(F("MQTT: SystemID OK."));
			blinkLED((void *)0);
		}
		else {
			D(F("MQTT: SystemID Failed."));
		}
	} else {
		D(F("MQTT: Connect failed."));

		timer.cancel(happyBlinkTimer);
	}

}


void mqttSubscribe() {
	if(client.connected()) {
		char buf[32];

		//  client.subscribe((String((char *)topic) + "/relay/0").c_str());
// 		memset(buf, 0, sizeof(buf));
// 		strncpy(buf, systemID, sizeof(buf));
// 		strcat(buf, "/relay/0");
// 		client.subscribe(buf);
// 
// 		memset(buf, 0, sizeof(buf));
// 		strncpy(buf, systemID, sizeof(buf));
// 		strcat(buf, "/relay/1");
// 		client.subscribe(buf);
// 
// 		memset(buf, 0, sizeof(buf));
// 		strncpy(buf, systemID, sizeof(buf));
// 		strcat(buf, "/temperature/0");
// 		client.subscribe(buf);
// 	
// 		memset(buf, 0, sizeof(buf));
// 		strncpy(buf, systemID, sizeof(buf));
// 		strcat(buf, "/status/0");
// 		client.subscribe(buf);
// 	
// 		memset(buf, 0, sizeof(buf));
// 		strncpy(buf, systemID, sizeof(buf));
// 		strcat(buf, "/status/1");
// 		client.subscribe(buf);
		memset(buf, 0, sizeof(buf));
		strncpy(buf, systemID, sizeof(buf));
		strcat(buf, "/+");
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
	setupSystem();
	setupDisplay();

	int is_reset = 0;

	SPIFFS.begin();
	
	AutoConnectConfig acConfig;

	delay(1000);

	pinMode(PIN_RESET_WIFI, INPUT_PULLUP);
	is_reset = digitalRead(PIN_RESET_WIFI);

	pinMode(PIN_STATUS_0, INPUT_PULLUP);
	pinMode(PIN_STATUS_1, INPUT_PULLUP);

	pinMode(PIN_LED, OUTPUT);
	digitalWrite(PIN_LED, 1);

	pinMode(PIN_RELAY_0, OUTPUT);
	digitalWrite(PIN_RELAY_0, 0);

	pinMode(PIN_RELAY_1, OUTPUT);
	digitalWrite(PIN_RELAY_1, 0);

	Serial.begin(115200);
	Serial.println();

	welcome();

	// Should load default config if run for the first time
	Serial.println(F("Loading configuration..."));
	loadConfiguration(CONFIG_FILE, appConfig);

	// Create configuration file
// 	Serial.println(F("Saving configuration..."));
// 	saveConfiguration(CONFIG_FILE, appConfig);

	// Dump config file
	Serial.println(F("Print config file..."));
	printFile(CONFIG_FILE);


	Server.on("/", rootPage);


	acConfig.apid = "OSCR " + String(systemID);
	acConfig.psk = "12345678";
	acConfig.ota = AC_OTA_BUILTIN;
	acConfig.title = String(systemID);
	acConfig.homeUri = "/";
	acConfig.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_DISCONNECT | AC_MENUITEM_RESET;

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

	if (is_reset == LOW) {
		D(F("WiFi AP Configuration"));
		D("");
		D(String(acConfig.apid));
		D("PW: " + String(acConfig.psk));
		acConfig.immediateStart = true;
		acConfig.autoRise = true;
	}

	Portal.config(acConfig);

	if (Portal.begin()) {  
		D("IP: " + WiFi.localIP().toString());
	}

	if (is_reset == LOW)
		return;

	mqttConnect();

	mqttSubscribe();
}


void loop() {
	Portal.handleClient();

	bool isConnected = client.loop();
	if(! isConnected) {
		if(millis() > lastMQTTOnlineCheck) {
			timer.cancel(happyBlinkTimer);
			timer.cancel(temperatureTimer);
			timer.cancel(statusTimer);

			D(F("MQTT: Reconnecting..."));

			mqttConnect();
			mqttSubscribe();

			lastMQTTOnlineCheck = millis() + 30000;
		}
	}
	
	timer.tick();
	
	bool status0 = ! digitalRead(PIN_STATUS_0);
	bool status1 = ! digitalRead(PIN_STATUS_1);

	if((status0 != lastStatus0) || (status1 != lastStatus1)) {
		publishStatus((void *)0);
		lastStatus0 = status0;
		lastStatus1 = status1;
	}
}
