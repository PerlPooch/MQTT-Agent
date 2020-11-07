#include "MDS_Autoconfig.h"
#include <Adafruit_SSD1306.h>

#define AUX_SETTING_URI		"/mqtt_setting"
#define AUX_SAVE_URI		"/mqtt_save"
#define AUX_CLEAR_URI		"/mqtt_clear"


MDS_Autoconfig::MDS_Autoconfig() {
}

MDS_Autoconfig::~MDS_Autoconfig() {
}

void MDS_Autoconfig::setup() {
	Serial.println(F("MDS_Autoconfig::setup"));
// 	display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
	Server = new ESP8266WebServer;
	Portal = new AutoConnect(*Server);
	
	setMAC();
}


void MDS_Autoconfig::setMAC()
{
	// Compute the systemID -- The unique ID for this devicd. This will be used as the root leaf
	// for the MQTT topic
	String mac = WiFi.macAddress();
	mac.getBytes((byte *)systemID, sizeof(systemID));
}


void MDS_Autoconfig::setAutoConnectConfig(AutoConnectConfig &acConfig) {
	acConfig.apid = "OSCR " + String(systemID);
	acConfig.psk = "12345678";
	acConfig.ota = AC_OTA_BUILTIN;
	acConfig.title = String(systemID);
	acConfig.homeUri = "/";
	acConfig.menuItems = AC_MENUITEM_CONFIGNEW | AC_MENUITEM_DISCONNECT | AC_MENUITEM_RESET;
}


void MDS_Autoconfig::checkForReset(bool shouldResetWiFI, AutoConnectConfig &acConfig) {
	Serial.println(F("checkForReset()"));

	if (shouldResetWiFI == true) {
		Serial.println(F("WiFi AP Configuration"));
		display.D(F("WiFi AP Configuration"));
		display.D("");
		display.D(String(acConfig.apid));
		display.D("PW: " + String(acConfig.psk));
		acConfig.immediateStart = true;
		acConfig.autoRise = true;
	}
}


void MDS_Autoconfig::AutoConnectConfigSetup(bool shouldResetWiFI) {
	Serial.println("AutoConnectConfigSetup()" + shouldResetWiFI);

	AutoConnectConfig acConfig;

	setAutoConnectConfig(acConfig);
	
	if(Portal->load(FPSTR(CUSTOM_SETTINGS))) {
		AutoConnectAux& mqtt_setting = *Portal->aux(AUX_SETTING_URI);

		AutoConnectInput& broker = mqtt_setting.getElement<AutoConnectInput>("mqttserver");
		broker.value = getConfigMQTTBroker();

 		AutoConnectInput& port = mqtt_setting.getElement<AutoConnectInput>("mqttport");
 		port.value = getConfigMQTTPort();

		Portal->on(AUX_SAVE_URI, std::bind(&MDS_Autoconfig::saveMQTTParams, this,
                          std::placeholders::_1,
                          std::placeholders::_2));
    } else {
		Serial.println("load error");
	}

	checkForReset(shouldResetWiFI, acConfig);

	Portal->config(acConfig);
}


String MDS_Autoconfig::saveMQTTParams(AutoConnectAux& aux, PageArgument& args) {
	AutoConnectAux* mqtt_setting = Portal->aux(Portal->where());
	AutoConnectInput& serverInput = mqtt_setting->getElement<AutoConnectInput>("mqttserver");
	AutoConnectInput& portInput = mqtt_setting->getElement<AutoConnectInput>("mqttport");

	String serverValue = serverInput.value;
	String portValue = portInput.value;

	setConfigMQTTBroker(serverValue);
	setConfigMQTTPort(portValue);

	Serial.println(F("saveMQTTParams() Configuration updated."));

	saveConfig();
	
	AutoConnectText&  result = aux["parameters"].as<AutoConnectText>();
	result.value = "Broker: " + serverValue + ":" + portValue;

	return String("");
}


void MDS_Autoconfig::saveConfig() {
	saveCallback(getConfigMQTTBroker(), getConfigMQTTPort());
}

void MDS_Autoconfig::tick() {
	Portal->handleClient();
}

// ----------------

AutoConnect* MDS_Autoconfig::getPortal() {
	return Portal;
}

ESP8266WebServer* MDS_Autoconfig::getWebServer() {
	return Server;
}

char* MDS_Autoconfig::getSystemID() {
	return systemID;
}

void MDS_Autoconfig::setConfigMQTTBroker(String b) {
	config_MQTTBroker = b;
}

String MDS_Autoconfig::getConfigMQTTBroker() {
	return config_MQTTBroker;
}

void MDS_Autoconfig::setConfigMQTTPort(String p) {
	config_MQTTPort = p;
}

String MDS_Autoconfig::getConfigMQTTPort() {
	return config_MQTTPort;
}

void MDS_Autoconfig::setSaveConfigCallback(std::function<void(String, String)> callback) {
	saveCallback = callback;
}

void MDS_Autoconfig::setDisplay(MDS_Display& d) {
	display = d;
}



