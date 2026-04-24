#include "MDS_Platform.h"
#include "MDS_Display.h"
#include "uiHtml.h"

// ---- Configuration -------------------------------------------------------------------
//
#define USE_STATUS_0
// STATUS_1 and DFPlayer are mutually exclusive
#define USE_STATUS_1
// #define INVERT_STATUS
#define USE_RELAY_0
// RELAY_1 and DFPlayer are mutually exclusive
#define USE_RELAY_1
// #define USE_DFPLAYER
// #define PLAY_TRIGGER_RELAY_0
// #define USE_MIDI
// #define RELAY_POSITIVE_LOGIC
#define RELAY_NEGATIVE_LOGIC
// #define USE_1WIRE_TEMPERATURE
// #define USE_DHT11_TEMPERATURE
// #define ROTATE_DISPLAY
//
// --------------------------------------------------------------------------------------

#if IS_ESP8266
	#include <ESP8266WiFi.h>
	#include <ESP8266WebServer.h>
#elif IS_ESP32
	#include <WiFi.h>
	#include <WebServer.h>
	#include <esp_mac.h>
#endif
#include <WiFiManager.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#ifdef USE_1WIRE_TEMPERATURE
#include <OneWire.h>
#include <DallasTemperature.h>
#endif
#include <arduino-timer.h>
#include <FS.h>
#include <LittleFS.h>
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


#define VERSION				"2.3"
#define ACCESSPOINT_NAME	"MQA"

#define SCREEN_WIDTH		128		// OLED display width, in pixels
#define SCREEN_HEIGHT		32		// OLED display height, in pixels
#define OLED_RESET    		-1		// Reset pin # (or -1 if sharing Arduino reset pin)
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

#define CONFIG_FILE				"/config.json"


#define	DEFAULT_MQTT_PORT		1883
#define	DEFAULT_TEMP_RATE		10
#define	DEFAULT_STATUS_RATE		60
#define	DEFAULT_RELAY_PULSE		500		// Time(ms) for relay pulses

#define	DISPLAY_TIMEOUT			5000	// Time(ms) between each display scroll
#define	DISPLAY_LINES			4		// Number of text lines that fit on the display
#define	HAPPY_PERIOD			5000	// Time(ms) for happy LED blinks
#define	HAPPY_NOMQTT_PERIOD		10000	// Time(ms) for happy LED blinks
#define	DFPLAYER_RESET			3600	// Time(s) between resetting DFPlayer
#define ONLINE_CHECK_PERIOD 	10000	// Time(ms) between checks to see if we're still online
#define REBOOT_DAYS				7		// Reboot every N days
#define WIFI_RETRY_PERIOD		10000UL	// Time(ms) between WiFi connection retries
#define REBOOT_BUTTON_TIME		10000	// Time(ms) WiFi button down to cause Reboot

#ifdef RELAY_POSITIVE_LOGIC
	#define R_LOGIC_HIGH 1
	#define R_LOGIC_LOW 0
#else
	#define R_LOGIC_HIGH 0
	#define R_LOGIC_LOW 1
#endif

#define MS_PER_DAY				(24UL * 60UL * 60UL * 1000UL)

char				systemID[32];		// System ID. Based on the WiFi MAC

#if IS_ESP8266
ESP8266WebServer 	Server;
#elif IS_ESP32
WebServer 			Server;
#endif

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

MDS_Display			mdsDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_RESET, &Wire, DISPLAY_LINES, DISPLAY_TIMEOUT);

Timer<>::Task		happyBlinkTimer;
Timer<>::Task		temperatureTimer;
Timer<>::Task		statusTimer;
#ifdef USE_DFPLAYER
Timer<>::Task		dfplayerTimer;
#endif

auto					timer = timer_create_default();
unsigned long			lastMQTTOnlineCheck;
static unsigned long	rebootPressedTime = 0;
static uint32_t			wifiIdleSince = 0;
static unsigned long	wifiNextRetry = 0;
static bool				wifiEverUp = false;
static bool				wifiProvisioning = false;

static uint32_t	rebootTime = (uint32_t)((uint32_t)REBOOT_DAYS * MS_PER_DAY);

struct AppConfig {
	char		MQTTBroker[64];
	uint16_t	MQTTPort;
	uint16_t	temperatureUpdateRate;
	uint16_t	statusUpdateRate;
	uint16_t	relayPulseDuration;
};
AppConfig appConfig = {
	"",
	DEFAULT_MQTT_PORT,
	DEFAULT_TEMP_RATE,
	DEFAULT_STATUS_RATE,
	DEFAULT_RELAY_PULSE
};

struct temperature_t {
	char	address[24];	// "XX:XX:XX:XX:XX:XX:XX:XX" + '\0'
	float	tempF;
	bool	valid;
} ;

// === Upload/Update
static volatile bool shouldReboot = false;

// === Display
void D(String m);

static void handleUploadPage();
static void handleUploadPost();
static void handleUploadStream();

// === UI
static void uiPage();
DynamicJsonDocument getStatusAsJSON();

// === MQTT publish helpers
bool blinkLED(void* opaque);
bool publishHumidity(void* opaque);
bool publishPlayStatus(void* opaque, int item, String status);

// === WiFi Manager
static 					WiFiManager wm;
static bool 			wmShouldSave = false;
static bool				wmParamsAdded = false;

static void wmSaveCallback() {
	wmShouldSave = true;
}

static char wmMqttPort[6];     // "65535" + NUL
static char wmTempRate[6];
static char wmStatusRate[6];
static char wmRelayPulse[6];

static WiFiManagerParameter pMqttBroker(
	"mqttBroker",
	"MQTT Broker",
	appConfig.MQTTBroker,
	sizeof(appConfig.MQTTBroker)
);

static WiFiManagerParameter pMqttPort(
	"mqttPort",
	"MQTT Port",
	wmMqttPort,
	sizeof(wmMqttPort)
);

static WiFiManagerParameter pTempRate(
	"tempRate",
	"Temperature Update Rate (s)",
	wmTempRate,
	sizeof(wmTempRate)
);

static WiFiManagerParameter pStatusRate(
	"statusRate",
	"Status Update Rate (s)",
	wmStatusRate,
	sizeof(wmStatusRate)
);

static WiFiManagerParameter pRelayDuration(
	"relayDuration",
	"Relay Pulse (ms)",
	wmRelayPulse,
	sizeof(wmRelayPulse)
);

static uint16_t parseU16(const char* s, uint16_t def) {
	if(!s || !*s) return def;

	char* end = nullptr;
	unsigned long v = strtoul(s, &end, 10);
	if(end == s) return def;
	if(v > 65535UL) return def;
	return (uint16_t)v;
}

static void normalizeAppConfigDefaults() {
	if(appConfig.MQTTPort == 0) appConfig.MQTTPort = DEFAULT_MQTT_PORT;
	if(appConfig.temperatureUpdateRate == 0) appConfig.temperatureUpdateRate = DEFAULT_TEMP_RATE;
	if(appConfig.statusUpdateRate == 0) appConfig.statusUpdateRate = DEFAULT_STATUS_RATE;
	if(appConfig.relayPulseDuration == 0) appConfig.relayPulseDuration = DEFAULT_RELAY_PULSE;
}

static void makeDeviceTopic(char* out, size_t outSize, const char* device) {
	snprintf(out, outSize, "spencer/%s/%s", systemID, device);
}

bool lastStatus0 = false, lastStatus1 = false;
bool reset_is_down = false;


static String getBuildDate() {
	const char *date = __DATE__;	// "Mmm dd yyyy"
	const char *time = __TIME__;	// "hh:mm:ss"

	char month[4];
	char day[3];
	char year[5];
	char iso[20];

	month[0] = date[0];
	month[1] = date[1];
	month[2] = date[2];
	month[3] = '\0';

	day[0] = (date[4] == ' ') ? '0' : date[4];
	day[1] = date[5];
	day[2] = '\0';

	year[0] = date[7];
	year[1] = date[8];
	year[2] = date[9];
	year[3] = date[10];
	year[4] = '\0';

	const char *monthNum = "00";

	if(strcmp(month, "Jan") == 0) monthNum = "01";
	else if(strcmp(month, "Feb") == 0) monthNum = "02";
	else if(strcmp(month, "Mar") == 0) monthNum = "03";
	else if(strcmp(month, "Apr") == 0) monthNum = "04";
	else if(strcmp(month, "May") == 0) monthNum = "05";
	else if(strcmp(month, "Jun") == 0) monthNum = "06";
	else if(strcmp(month, "Jul") == 0) monthNum = "07";
	else if(strcmp(month, "Aug") == 0) monthNum = "08";
	else if(strcmp(month, "Sep") == 0) monthNum = "09";
	else if(strcmp(month, "Oct") == 0) monthNum = "10";
	else if(strcmp(month, "Nov") == 0) monthNum = "11";
	else if(strcmp(month, "Dec") == 0) monthNum = "12";

	snprintf(iso, sizeof(iso), "%s-%s-%sT%.8s", year, monthNum, day, time);

	return String(iso);
}

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
			digitalWrite(PIN_RELAY_0, R_LOGIC_LOW);
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
	File file = LittleFS.open(filename, "r");
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

#if IS_ESP8266
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
#elif IS_ESP32
	File root = fs.open(dirname);
	if(!root || !root.isDirectory()) {
		Serial.println(F("    <not a directory>"));
		return;
	}

	for(File f = root.openNextFile(); f; f = root.openNextFile()) {
		Serial.print(F("    "));
		Serial.print(f.name());
		Serial.print(F(" ("));
		Serial.print((uint32_t)f.size());
		Serial.println(F("b)"));
	}
#endif
}

bool loadConfiguration(const char *filename, AppConfig &config) {
	File file = LittleFS.open(filename, "r");

	if (file) {
		StaticJsonDocument<512> doc;

		DeserializationError error = deserializeJson(doc, file);

		if (error) {
			Serial.println(F("Unable to read configuration"));
			file.close();
			return false;
		} else {
			const char* broker = doc["MQTTBroker"] | "";
			strlcpy(config.MQTTBroker, broker, sizeof(config.MQTTBroker));

			config.MQTTPort = doc["MQTTPort"] | DEFAULT_MQTT_PORT;

			config.temperatureUpdateRate = doc["temperatureUpdateRate"] | DEFAULT_TEMP_RATE;

			config.statusUpdateRate = doc["statusUpdateRate"] | DEFAULT_STATUS_RATE;

			config.relayPulseDuration = doc["relayPulseDuration"] | DEFAULT_RELAY_PULSE;
			if(config.relayPulseDuration < 100) config.relayPulseDuration = 100;
			if(config.relayPulseDuration > 60000) config.relayPulseDuration = 60000;
		} 

		file.close();
		return true;
	} else {
		Serial.println(F("Unable to open configuration."));
		return false;
	}
}


void saveConfiguration(const char *filename, const AppConfig &config) {
	LittleFS.remove(filename);

	File file = LittleFS.open(filename, "w");
	if (!file) {
		Serial.println(F("Unable to create configuration file"));
		return;
	}

	StaticJsonDocument<512> doc;

	doc["MQTTBroker"] = config.MQTTBroker;
	doc["MQTTPort"] = config.MQTTPort;
	doc["temperatureUpdateRate"] = config.temperatureUpdateRate;
	doc["statusUpdateRate"] = config.statusUpdateRate;
	doc["relayPulseDuration"] = config.relayPulseDuration;

	serializeJsonPretty(doc, Serial);

	if (serializeJson(doc, file) == 0) {
		Serial.println(F("Unable to write configuration file"));
	}

	file.close();
}

void configModeCallback(WiFiManager *myWM) {
  Serial.println("WiFi: Failed to Connect.");
  Serial.println("WiFi: Portal " + String(myWM->getConfigPortalSSID()));

  D("Failed to Connect.");
  D(" ");
  D(String(myWM->getConfigPortalSSID()));
}


static bool wifiBegin(bool forcePortal) {
	wmShouldSave = false;

	normalizeAppConfigDefaults();

	uint16_t portForUi		= appConfig.MQTTPort;
	uint16_t tempForUi		= appConfig.temperatureUpdateRate;
	uint16_t statusForUi	= appConfig.statusUpdateRate;
	uint16_t relayPDForUI	= appConfig.relayPulseDuration;

	snprintf(wmMqttPort,   sizeof(wmMqttPort),   "%u", (unsigned)portForUi);
	snprintf(wmTempRate,   sizeof(wmTempRate),   "%u", (unsigned)tempForUi);
	snprintf(wmStatusRate, sizeof(wmStatusRate), "%u", (unsigned)statusForUi);
	snprintf(wmRelayPulse, sizeof(wmRelayPulse), "%u", (unsigned)relayPDForUI);

	// Fresh manager state each time we enter provisioning
	wm.setSaveConfigCallback(wmSaveCallback);
	wm.setBreakAfterConfig(true);
	wm.setConnectTimeout(20);
	wm.setConfigPortalTimeout(180);
	
	if(!wmParamsAdded) {
		wm.addParameter(&pMqttBroker);
		wm.addParameter(&pMqttPort);
		wm.addParameter(&pTempRate);
		wm.addParameter(&pStatusRate);
		wm.addParameter(&pRelayDuration);
		wmParamsAdded = true;
	}	

	WiFi.mode(WIFI_STA);

	char apName[32];
	snprintf(apName, sizeof(apName), "%s %s", ACCESSPOINT_NAME, systemID);
	const char* apPass = "12345678";

	static const char wmHead[] PROGMEM = R"rawliteral(
<style>
:root{--bg:#000000;--text:#fff;--card:#20202080;--border:rgba(43,14,161,.75);--shadow:0 12px 30px rgba(0,0,0,.10);--radius:8px;}
html,body{height:100%;}
body{margin:0;color:var(--text);background:var(--bg);font-family:Helvetica,Arial,sans-serif;}
/* card-ish main container */
main, .wrap, .content, form{max-width:560px;margin:4px auto;padding:8px;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);}
button{padding:12px;}
.wrap{display:block;}
h1,h2,h3{margin:0 0 14px 0;font-size:18px;font-weight:600;background:inherit;}
h3{font-size:12px;font-weight:300;}
input,button,select{font:inherit;}
button,input[type=submit]{border-radius:var(--radius);cursor:pointer;background-color:#c4c4c4;color:#000;font-weight:600}
</style>
	)rawliteral";

	wm.setCustomHeadElement(wmHead);
	wm.setDarkMode(true);
	wm.setTitle("Agent Configuration");
	wm.setAPCallback(configModeCallback);
 	wm.setDebugOutput(false);

	bool ok = false;
	if(forcePortal) {
		Serial.println(F("WiFi: Start Config Portal"));
		ok = wm.startConfigPortal(apName, apPass);
	} else {
		Serial.println(F("WiFi: Auto Connect"));
		D(F("Connecting ..."));
		ok = wm.autoConnect(apName, apPass);
	}

	if (!ok) {
		Serial.println(F("WiFiManager: failed to connect, portal exited."));
		return false;
	}

	D(F("Connected."));

	// Persist config model if portal saved
	if (wmShouldSave) {
		strlcpy(appConfig.MQTTBroker,
		        pMqttBroker.getValue(),
		        sizeof(appConfig.MQTTBroker));

		appConfig.MQTTPort =
			parseU16(pMqttPort.getValue(), DEFAULT_MQTT_PORT);

		appConfig.temperatureUpdateRate =
			parseU16(pTempRate.getValue(), DEFAULT_TEMP_RATE);

		appConfig.statusUpdateRate =
			parseU16(pStatusRate.getValue(), DEFAULT_STATUS_RATE);

		// parseU16 limits the upper bound to 65535
		appConfig.relayPulseDuration =
			parseU16(pRelayDuration.getValue(), DEFAULT_RELAY_PULSE);
		if(appConfig.relayPulseDuration < 100) appConfig.relayPulseDuration = 100;
		if(appConfig.relayPulseDuration > 60000) appConfig.relayPulseDuration = 60000;

		saveConfiguration(CONFIG_FILE, appConfig);
	}

	return true;
}


static bool wifiReady() {
// Serial.print("wifiReady() ");
// 	wl_status_t st = (wl_status_t)WiFi.status();
// 	Serial.printf("WiFi: Retry. st=%s mode=%d.\n",
// 		wifiStatusStr(st),
// 		(int)WiFi.getMode());

	if(WiFi.status() != WL_CONNECTED) return false;
	IPAddress ip = WiFi.localIP();

	return (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0);
}

static const char* wifiStatusStr(wl_status_t st) {
	switch(st) {
		case WL_IDLE_STATUS:     return "IDLE";
		case WL_NO_SSID_AVAIL:   return "NO_SSID";
		case WL_SCAN_COMPLETED:  return "SCAN_DONE";
		case WL_CONNECTED:       return "CONNECTED";
		case WL_CONNECT_FAILED:  return "CONNECT_FAILED";
		case WL_CONNECTION_LOST: return "CONNECTION_LOST";
		case WL_DISCONNECTED:    return "DISCONNECTED";
		default:                 return "UNKNOWN";
	}
}

static void wifiConnect() {
	WiFi.mode(WIFI_STA);
	WiFi.begin();
}

static void wifiRetryTick() {
	if(wifiReady()) {
		wifiIdleSince = 0;
		return;
	}

	uint32_t now = millis();
	if((int32_t)(now - wifiNextRetry) < 0) return;

// 	Serial.println(F("WiFi: wifiRetryTick()"));
	
	wl_status_t st = (wl_status_t)WiFi.status();
	IPAddress ip = WiFi.localIP();

	Serial.printf("WiFi: Retry. st=%s mode=%d ip=%s.\n",
		wifiStatusStr(st),
		(int)WiFi.getMode(),
		ip.toString().c_str());

	// Track how long we've been stuck in IDLE
	if(st == WL_IDLE_STATUS) {
		if(!wifiIdleSince) wifiIdleSince = now;
	} else if(st == WL_NO_SSID_AVAIL) {
		Serial.println(F("WiFi: No SSID. Giving up."));
		D("Timeout."); 
		rebootTime = 0; // very in the past. Reboot.
	} else {
		wifiIdleSince = 0;
	}

	// Case 1: "CONNECTED but no IP" => bounce DHCP by cycling link
	if (st == WL_CONNECTED && ip == IPAddress(0, 0, 0, 0)) {
		Serial.println(F("WiFi: Connected, No-IP -> disconnect + reconnect."));
		WiFi.disconnect(false);
		delay(200);
		wifiConnect();
		wifiNextRetry = now + 2000UL;
		return;
	}

	// Case 2: If we've been IDLE too long, force a reconnect
	if(wifiIdleSince && (uint32_t)(now - wifiIdleSince) > 15000UL) {
		Serial.println(F("WiFi: IDLE -> reconnecting."));
		wifiIdleSince = 0;

		WiFi.disconnect(false);
		delay(200);

		wifiConnect();

		wifiNextRetry = now + 5000UL;
		return;
	}

	wifiConnect();
	wifiNextRetry = now + WIFI_RETRY_PERIOD;
}

static void handleUploadPage()
{
	if(!wifiEverUp) return;

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

	Server.send(200, "text/html", uploadHtml);
}

static void handleUploadPost()
{
	// Called after upload completes (success or failure)
	if (Update.hasError()) {
		String msg = "Update failed.\n";
		Server.send(500, "text/plain", msg);
		return;
	}

	Server.send(200, "text/plain", "Update OK. Rebooting...\n");
	shouldReboot = true; // reboot in loop after response is sent
}

static void handleUploadStream()
{
	HTTPUpload &upload = Server.upload();

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
#ifdef USE_1WIRE_TEMPERATURE
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
#endif
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
	
	digitalWrite(relayPin, R_LOGIC_LOW);
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
		r.tempF = 0.0f;
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
	makeDeviceTopic(baseTopic, sizeof(baseTopic), "temperature");

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
#ifdef USE_1WIRE_TEMPERATURE
		D(String(F("T")) + String(i) + F(": ") + String(t.tempF, 1));
#endif
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

	makeDeviceTopic(buf, sizeof(buf), "humidity");
	
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

	makeDeviceTopic(buf, sizeof(buf), "status");
	
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

	makeDeviceTopic(buf, sizeof(buf), "reboot");
	
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

	makeDeviceTopic(buf, sizeof(buf), "relay");
	
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

	makeDeviceTopic(buf, sizeof(buf), "trigger");
	
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
		} else if(device == "relay") {
 			appConfig.relayPulseDuration = rate.toInt();
			if(appConfig.relayPulseDuration < 100) appConfig.relayPulseDuration = 100;
			if(appConfig.relayPulseDuration > 60000) appConfig.relayPulseDuration = 60000;
 			D("Set-rate RDur: " + String(appConfig.relayPulseDuration));
 			saveConfiguration(CONFIG_FILE, appConfig);
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

				digitalWrite(relay, R_LOGIC_HIGH);
			} else if(state == "off") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Off");

				digitalWrite(relay, R_LOGIC_LOW);
			} else if(state == "pulse") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Pulse");

				digitalWrite(relay, R_LOGIC_HIGH);
				timer.in(appConfig.relayPulseDuration, clearRelay, (void *)deviceNum.toInt());
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
		digitalWrite(PIN_RELAY_0, R_LOGIC_HIGH);
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
	doc["build"] = getBuildDate();

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
			snprintf(key, sizeof(key), "temperature-%s", idStripped);

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
#if defined(USE_RELAY_0) || defined(USE_RELAY_1)
	doc["relayPulseDuration"] = (String)appConfig.relayPulseDuration;
#endif
#if defined(USE_DHT11_TEMPERATURE)
	String	humid = updateHumidity();
	doc["humidity"] = humid;
#endif
	config.trim();
	doc["config"] = config;
	doc["uptime"] = millis();

	return doc;
}

static void uiPage() {
	if(!wifiEverUp) return;

	Server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
	Server.sendHeader("Pragma", "no-cache");
	Server.sendHeader("Expires", "0");
	Server.sendHeader("X-Content-Type-Options", "nosniff");
	Server.sendHeader("X-Frame-Options", "DENY");
	Server.sendHeader("Referrer-Policy", "no-referrer");
	Server.sendHeader(
		"Content-Security-Policy",
		"default-src 'self'; "
		"script-src 'self' 'unsafe-inline'; "
		"style-src 'self' 'unsafe-inline'; "
		"img-src 'self' data:; "
		"connect-src 'self'; "
		"object-src 'none'; "
		"base-uri 'none'; "
		"frame-ancestors 'none'; "
		"form-action 'self'"
	);
	
	Server.send_P(200, "text/html; charset=utf-8", uiHtml);
}

void rootPage() {
	if(!wifiEverUp) return;
	
	char	data[512];

	memset(data, 0, sizeof(data));

	if(Server.hasArg(F("statusUpdateRate"))) {
		appConfig.statusUpdateRate = Server.arg(F("statusUpdateRate")).toInt();
		D("Set-rate Status: " + String(appConfig.statusUpdateRate));
		saveConfiguration(CONFIG_FILE, appConfig);
		timer.cancel(statusTimer);
		if(appConfig.statusUpdateRate > 0)
			statusTimer = timer.every(appConfig.statusUpdateRate * 1000, publishStatus, (void *)0);
	}

#if defined(USE_RELAY_0) || defined(USE_RELAY_1)
	if(Server.hasArg(F("relayPulseDuration"))) {
		long pulse = Server.arg(F("relayPulseDuration")).toInt();
		pulse = constrain(pulse, 100L, 60000L);
		appConfig.relayPulseDuration = (uint16_t)pulse;
		D("Set-rate RDur: " + String(appConfig.relayPulseDuration));
		saveConfiguration(CONFIG_FILE, appConfig);
	}
#endif

	if(Server.hasArg(F("temperatureUpdateRate"))) {
		appConfig.temperatureUpdateRate = Server.arg(F("temperatureUpdateRate")).toInt();
		D("Set-rate Temp: " + String(appConfig.temperatureUpdateRate));
		saveConfiguration(CONFIG_FILE, appConfig);
		timer.cancel(temperatureTimer);
		if(appConfig.temperatureUpdateRate > 0)
			temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000, publishTemperature, (void *)0);
	}	
	
	if(Server.hasArg(F("broker"))) 	{
		String s = Server.arg(F("broker"));	// expects "ip:port" or "ip"
		s.trim();

		String host = s;
		uint16_t port = 1883;

		int colon = s.indexOf(':');
		if (colon >= 0) {
			host = s.substring(0, colon);

			String p = s.substring(colon + 1);
			p.trim();
			if (p.length() > 0)
				port = (uint16_t)p.toInt();
		}

		host.trim();

		if (host.length() > 0) {
			memset(appConfig.MQTTBroker, 0, sizeof(appConfig.MQTTBroker));
			host.toCharArray(appConfig.MQTTBroker, sizeof(appConfig.MQTTBroker));

			appConfig.MQTTPort = port;

			D(String(F("Set-broker: ")) + host + F(":") + String(appConfig.MQTTPort));

			saveConfiguration(CONFIG_FILE, appConfig);
		} else {
			memset(appConfig.MQTTBroker, 0, sizeof(appConfig.MQTTBroker));
			appConfig.MQTTPort = port;

			D(String(F("Set-broker: none")));;

			saveConfiguration(CONFIG_FILE, appConfig);
			
			shouldReboot = true;
		}
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

				digitalWrite(relay, R_LOGIC_HIGH);
			} else if(state == "off") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Off");

				digitalWrite(relay, R_LOGIC_LOW);
			} else if(state == "pulse") {
				blinkLED((void *)0);
				D("Set Relay " + deviceNum + " Pulse");

				digitalWrite(relay, R_LOGIC_HIGH);
				timer.in(appConfig.relayPulseDuration, clearRelay, (void *)deviceNum.toInt());
			}
	}	
	
	DynamicJsonDocument doc = getStatusAsJSON();

	blinkLED((void *)0);
	
	serializeJsonPretty(doc, Serial);
	Serial.println();

	serializeJsonPretty(doc, data, sizeof(data));

	Server.send(200, "application/json", data);
}


void D(String m)
{
#ifdef DEBUG_D
	Serial.println("D: " + m);
#endif

	mdsDisplay.D(m);
}

static void setupSystem() {
	uint8_t mac[6] = {0};

#if IS_ESP32
	esp_read_mac(mac, ESP_MAC_BASE);
#else
	WiFi.macAddress(mac);
#endif

	snprintf(systemID, sizeof(systemID),
		"%02X:%02X:%02X:%02X:%02X:%02X",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}


void setupDisplay()
{
#if IS_ESP32
	Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
#endif
#ifdef ROTATE_DISPLAY
	mdsDisplay.setup(true);
#else
	mdsDisplay.setup(false);
#endif
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


void welcome() {
	Adafruit_SSD1306* display = mdsDisplay.getDevice();

	display->clearDisplay();
	display->setCursor(0,0);

	for(int i = 0; i < 3; i++) {
		digitalWrite(PIN_LED, 0);
		delay(50);
		digitalWrite(PIN_LED, 1);
		delay(300);
	}

	Serial.print(F("MQTT Agent, V "));
	Serial.print(VERSION); 
	Serial.print(" (");
	Serial.print(getBuildDate());
	Serial.println(F("). Copyright (C) 2026, Marc D. Spencer")); 

	display->print(F("MQTT Agent, V"));
	display->println(VERSION);
	display->println();
	display->println(F("(C) 2026"));
	display->println(F("Marc D. Spencer"));

	display->display();

	digitalWrite(PIN_LED, 0);
	delay(5000);
	digitalWrite(PIN_LED, 1);

	display->clearDisplay();
	display->display();
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

	snprintf(buf, sizeof(buf), "MQTT-Agent-%s", systemID);

	if(client.connect(buf)) {
		D(F("MQTT: Connected."));
		Serial.println(String(F("MQTT: Connected ")) + String(buf));

		if (WiFi.status() == WL_CONNECTED) {
			long rssi = WiFi.RSSI();
			D(String(F("RSSI: ")) + String(rssi) + String(F(" dBm")));
		}
			
		happyBlinkTimer = timer.every(HAPPY_PERIOD, blinkLED, (void *)0);

		makeDeviceTopic(buf, sizeof(buf), "notice");
		
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
		temperatureTimer = timer.every(appConfig.temperatureUpdateRate * 1000, publishTemperature, (void*)0);
	
	if (appConfig.statusUpdateRate > 0)
		statusTimer = timer.every(appConfig.statusUpdateRate * 1000, publishStatus, (void*)0);
}


void setup() {
	delay(100);
	
#if defined (USE_RELAY_0) || defined (PLAY_TRIGGER_RELAY_0)
	pinMode(PIN_RELAY_0, OUTPUT);
	digitalWrite(PIN_RELAY_0, R_LOGIC_LOW);
#endif

#ifdef USE_RELAY_1
	pinMode(PIN_RELAY_1, OUTPUT);
	digitalWrite(PIN_RELAY_1, R_LOGIC_LOW);
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

	setupSystem();
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

// ----------- LittleFS -----------------------------------------------------------

	bool restored = false;
	bool fsOk = false;

#if IS_ESP32
	// ESP32: allow format-on-fail to recover cleanly
	fsOk = LittleFS.begin(true);
#else
	// ESP8266: begin() only; format is a separate call if you want it
	fsOk = LittleFS.begin();
#endif

	if(!fsOk) {
		Serial.println(F("LittleFS: Mount Failed"));
		return;
	} else {
		Serial.println(F("LittleFS: OK"));
		listDir(LittleFS, "/");
	}

	// Should load default config if run for the first time
	Serial.print(F("Loading configuration ... "));
	if(! loadConfiguration(CONFIG_FILE, appConfig)) {
		Serial.print(F("Saving default configuration ... "));
	 	saveConfiguration(CONFIG_FILE, appConfig);
	}
	Serial.println(F("Done."));

	// Dump config file
 	Serial.println(F("Print config file..."));
 	printFile(CONFIG_FILE);

// ----------- WiFi -----------------------------------------------------------

	Server.on("/", rootPage);
	Server.on("/ui", uiPage);

	// Reset button logic: if held, force portal + clear stored WiFi creds
	bool forcePortal = false;
	if (is_reset == LOW) {
		Serial.println(F("WiFi: Reset requested. Clearing WiFi credentials."));
		wm.resetSettings();
		WiFi.disconnect(true);
		delay(200);
		forcePortal = true;
		wifiProvisioning = true;
	}
	
	// Start WiFi (autoConnect unless forced)
	bool ok = wifiBegin(forcePortal);
	if (ok) {
		if (wifiReady()) {
			Serial.print(F("WiFi IP: "));
			Serial.println(WiFi.localIP());
			wifiEverUp = true;
			wifiProvisioning = false;
		} else {
			Serial.println(F("WiFi: connected/portal done; STA not up yet"));
			wifiEverUp = false;
			wifiProvisioning = true;
		}

		Server.begin();
	} else {
		Serial.println(F("WiFi: Connect failed. Retrying."));
		wifiNextRetry = millis() + 1000UL;
		wifiEverUp = false;
		wifiProvisioning = false;
	}
	
	// Bail if we failed WiFi.
	if(!wifiEverUp) 
		return;
		
	// Bail out of rest of setup if reset held
	if (is_reset == LOW)
		return;
	
// ----------- Upload -------------------------------------------------------

Server.on("/upload", HTTP_GET, handleUploadPage);
Server.on("/upload", HTTP_POST, handleUploadPost, handleUploadStream);

// -----------        -------------------------------------------------------

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

void loop() {
	if (millis() >= rebootTime) {
		reboot();
	}
	// Button Down
	if (rebootPressedTime > 0 && (long)(millis() - rebootPressedTime) >= 0) {
		reboot();
	}

	mdsDisplay.tick();
	
	if (wifiProvisioning) {
		if (wifiReady()) {
			wifiProvisioning = false;
			wifiEverUp = true;
		}

		return;
	}
	
	// only here:
	wifiRetryTick();
	
	bool wasUp = wifiEverUp;

	if(!wifiReady()) {
		wifiEverUp = false;

		// Optional: only log on transition
		if(wasUp) {
			D(F("WiFi: Unavailable"));
			Serial.println(F("WiFi: Unavailable"));
		}

		return;
	}

	// WiFi is up
	if(!wasUp) {
		D(F("WiFi: Available"));
		Serial.println(F("WiFi: Available"));
		wifiEverUp = true;
		wifiProvisioning = false;
#if IS_ESP8266
		WiFi.setAutoConnect(true);
		WiFi.setAutoReconnect(true);
#elif IS_ESP32
		WiFi.setAutoReconnect(true);
#endif
 		lastMQTTOnlineCheck = 0; // force immediate MQTT attempt
	}

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

	Server.handleClient();
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
