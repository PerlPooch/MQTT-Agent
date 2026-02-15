#include "MDS_Display.h"
#include <Adafruit_SSD1306.h>


MDS_Display::MDS_Display() {
}

MDS_Display::~MDS_Display() {
}

void MDS_Display::setup() {
	Serial.println(F("MDS_Display::setup"));
	display = Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

	if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
		Serial.println(F("SSD1306 allocation failed"));
		for(;;); // Don't proceed, loop forever
	}

	clear();

	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setTextWrap(false);
	display.setCursor(0,0);

	display.display();

	for(int i = 0; i < DISPLAY_LINES; i++)
		screen.lines[i] = String();

	nextClearTime = millis() + DISPLAY_TIMEOUT;
}

bool MDS_Display::clearMessage() {
	D("");
			
	return true;
}

void MDS_Display::clear() {
	display.clearDisplay();
 	display.setCursor(0,0);
 	display.display();
	
	for(int i = 0; i < DISPLAY_LINES; i++)
		screen.lines[i] = String();
}

void MDS_Display::D(String m) {
	for(int i = 0; i < DISPLAY_LINES-1; i++) {
		screen.lines[i] = screen.lines[i + 1];
	}

	screen.lines[DISPLAY_LINES-1] = m;
	
	display.clearDisplay();
	display.setCursor(0,0);
	
	for(int i = 0; i < DISPLAY_LINES; i++) {
		display.println(screen.lines[i]);
	}

	if(m.length() != 0) {
		nextClearTime = millis() + DISPLAY_TIMEOUT;
	}

	display.display();
}

Adafruit_SSD1306* MDS_Display::getDevice() {
	return &display;
}

void MDS_Display::tick() {
	if(millis() >= nextClearTime) {
		clearMessage();
	
		nextClearTime = millis() + DISPLAY_TIMEOUT;
	}
}