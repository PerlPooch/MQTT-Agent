#ifndef MDS_SDD1306_DISPLAY
#define MDS_SDD1306_DISPLAY
 
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <arduino-timer.h>


#define	DISPLAY_LINES		4		// Number of text lines that fit on the display
#define SCREEN_WIDTH		128		// OLED display width, in pixels
#define SCREEN_HEIGHT		32		// OLED display height, in pixels
#define OLED_RESET    		-1		// Reset pin # (or -1 if sharing Arduino reset pin)
#define	DISPLAY_TIMEOUT		5000	// Time(ms) between each display scroll

struct Screen {
	String		lines[DISPLAY_LINES];
};

class MDS_Display {
	public:
        MDS_Display();
        ~MDS_Display();
        void				setup();
        bool				clearMessage();
        void				clear();
        void				D(String m);
		Adafruit_SSD1306*	getDevice();
		void				tick();

	private:
		Adafruit_SSD1306	display;
		Screen				screen;
		unsigned long		nextClearTime;
};

#endif