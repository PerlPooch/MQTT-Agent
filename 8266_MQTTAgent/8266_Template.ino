// #include <ESP8266WiFi.h>
// #include <ESP8266WebServer.h>
// #include <AutoConnect.h>
#include <arduino-timer.h>
#include <FS.h>
#include "MDS_Display.h"
#include "MDS_Config.h"

#include "MDS_Autoconfig.h"

#define PROJECT				"Unknown"
#define VERSION				"0.0"

#define PIN_RESET_WIFI 		12
#define PIN_LED 			16



auto				timer = timer_create_default();
MDS_Display			display;
MDS_Autoconfig		autoConfig;
MDS_Config			config;

struct AppConfig {
	char		MQTTBroker[64];
	uint16_t	MQTTPort;
	uint16_t	temperatureUpdateRate;
	uint16_t	statusUpdateRate;
};
AppConfig appConfig;



void setConfigurationCB(const JsonDocument &doc) {
	strlcpy(appConfig.MQTTBroker, doc["MQTTBroker"], sizeof(appConfig.MQTTBroker));
	appConfig.MQTTPort = doc["MQTTPort"];
	appConfig.temperatureUpdateRate = doc["temperatureUpdateRate"];
	appConfig.statusUpdateRate = doc["statusUpdateRate"];
}


void getConfigurationCB(JsonDocument &doc) {
	doc["MQTTBroker"]				= appConfig.MQTTBroker;
	doc["MQTTPort"] 				= appConfig.MQTTPort;
	doc["temperatureUpdateRate"] 	= appConfig.temperatureUpdateRate;
	doc["statusUpdateRate"] 		= appConfig.statusUpdateRate;
}


// TODO: Objectize the configuration data
void savePortalConfigurationCB(String broker, String port) {
	Serial.println(F("savePortalConfigurationCB()"));

	strlcpy(appConfig.MQTTBroker, broker.c_str(), broker.length() + 1);
	appConfig.MQTTPort = (uint16_t)port.toInt();

	config.save();
	config.print();
}


void rootPage() {
	autoConfig.getWebServer()->send(200, "text/plain", "Hello World");
}


void welcome() {
	display.clear();

	display.getDevice()->println(PROJECT); 
	display.getDevice()->print(F(", V"));
	display.getDevice()->println(VERSION); 
	display.getDevice()->println(); 
	display.getDevice()->println(F("(C) 2020")); 
	display.getDevice()->println(F("Marc D. Spencer")); 

	display.getDevice()->display();

	delay(5000);
	display.getDevice()->clearDisplay();
	display.getDevice()->display();
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


void setup() {
	delay(1000);

	// Set up Serial Port
	Serial.begin(115200);
	Serial.println();

	// Set up Display
	display.setup();
	
	// Set up autoConfig (and WebServer)
	autoConfig.setup();
	autoConfig.setDisplay(display);

	config.setup();
	config.setSetConfigCallback(setConfigurationCB);
	config.setGetConfigCallback(getConfigurationCB);

	welcome();
	
	// See if we are in WiFi configuration mode
	pinMode(PIN_RESET_WIFI, INPUT_PULLUP);
	bool shouldResetWiFI = ! digitalRead(PIN_RESET_WIFI);
	Serial.println("shouldResetWiFI?: " + String(shouldResetWiFI ? "true" : "false"));
	
	// Load saved configuration
	Serial.println(F("Loading configuration..."));
	config.load();

	// tell autoConfig about the configuration
	// TODO: Objectize Configuration
	autoConfig.setConfigMQTTBroker(String(appConfig.MQTTBroker));
	autoConfig.setConfigMQTTPort(String(appConfig.MQTTPort));
	
	autoConfig.setSaveConfigCallback(savePortalConfigurationCB);

	autoConfig.AutoConnectConfigSetup(shouldResetWiFI);

	pinMode(PIN_LED, OUTPUT);
	digitalWrite(PIN_LED, 1);

	// Dump config file
	Serial.println(F("Print config file..."));
	config.print();

	autoConfig.getWebServer()->on("/", rootPage);

	if (autoConfig.getPortal()->begin()) {  
		display.D("IP: " + WiFi.localIP().toString());
	}
}


void loop() {
	autoConfig.tick();
	timer.tick();
	display.tick();
}


