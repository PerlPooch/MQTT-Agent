#ifndef MDS_AUTOCONFIG
#define MDS_AUTOCONFIG

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include "MDS_Display.h"

static const char CUSTOM_SETTINGS[] PROGMEM = R"raw(
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



class MDS_Autoconfig {
	public:
        MDS_Autoconfig();
        ~MDS_Autoconfig();
        void 					setup();
		void 					setMAC();
		void 					AutoConnectConfigSetup(bool shouldResetWiFI);
		void					setAutoConnectConfig(AutoConnectConfig &acConfig);
		String					saveMQTTParams(AutoConnectAux& aux, PageArgument& args);
		void					checkForReset(bool shouldResetWiFI, AutoConnectConfig &acConfig);
		void					saveConfig();
		AutoConnect* 			getPortal();
		ESP8266WebServer*		getWebServer();
		char*					getSystemID();
		void					setConfigMQTTBroker(String b);
		String					getConfigMQTTBroker();
		void					setConfigMQTTPort(String p);
		String					getConfigMQTTPort();
		void					setDisplay(MDS_Display& d);
		void					setSaveConfigCallback(std::function<void(String, String)> callback);
		void					tick();

	private:
		ESP8266WebServer*		Server;
		AutoConnect*			Portal;
		char					systemID[32];		// System ID. Based on the WiFi MAC
		MDS_Display				display;
		String					config_MQTTBroker;
		String					config_MQTTPort;
		std::function<void(String, String)> saveCallback;
};


#endif