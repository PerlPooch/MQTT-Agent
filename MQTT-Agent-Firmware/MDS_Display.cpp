#include "MDS_Display.h"
#include <Adafruit_SSD1306.h>


MDS_Display::MDS_Display(uint16_t width,
                         uint16_t height,
                         int8_t resetPin,
                         TwoWire* wire,
                         uint8_t lineCount,
                         unsigned long displayTimeout)
: display(width, height, wire, resetPin),
  lineCount(lineCount == 0 ? 1 : (lineCount > MDS_DISPLAY_MAX_LINES ? MDS_DISPLAY_MAX_LINES : lineCount)),
  displayTimeout(displayTimeout)
{
}

MDS_Display::~MDS_Display() {
}

void MDS_Display::setup(bool rotate) {
	Serial.println(F("MDS_Display::setup"));

	if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
		Serial.println(F("SSD1306 allocation failed"));
		for(;;); // Don't proceed, loop forever
	}

	if(rotate) {
		display.setRotation(2);
	}

	clear();

	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setTextWrap(false);
	display.setCursor(0,0);

	display.display();

	for(int i = 0; i < lineCount; i++)
		lines[i] = String();

	nextClearTime = millis() + displayTimeout;
	isSetup = true;
}

bool MDS_Display::clearMessage() {
	D("");
			
	return true;
}

void MDS_Display::clear() {
	display.clearDisplay();
 	display.setCursor(0,0);
 	display.display();
	
	for(int i = 0; i < lineCount; i++)
		lines[i] = String();
}

void MDS_Display::D(String m) {
	if(!isSetup) return;

	for(int i = 0; i < lineCount-1; i++) {
		lines[i] = lines[i + 1];
	}

	lines[lineCount-1] = m;
	
	display.clearDisplay();
	display.setCursor(0,0);
	
	for(int i = 0; i < lineCount; i++) {
		display.println(lines[i]);
	}

	if(m.length() != 0) {
		nextClearTime = millis() + displayTimeout;
	}

	display.display();
}

Adafruit_SSD1306* MDS_Display::getDevice() {
	return &display;
}

void MDS_Display::tick() {
	if(!isSetup) return;

	if(millis() >= nextClearTime) {
		clearMessage();
	
		nextClearTime = millis() + displayTimeout;
	}
}
