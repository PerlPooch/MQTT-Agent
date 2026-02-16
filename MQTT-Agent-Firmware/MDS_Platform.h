#pragma once

#if defined(ARDUINO_ARCH_ESP32)
  #define IS_ESP32 1
  #define IS_ESP8266 0
#elif defined(ARDUINO_ARCH_ESP8266)
  #define IS_ESP32 0
  #define IS_ESP8266 1
#else
  #error "Unsupported platform"
#endif

#if defined(ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C6)
	#define IS_ESP32C6 1
#else
	#define IS_ESP32C6 0
#endif
