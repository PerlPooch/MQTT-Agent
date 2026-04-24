#ifndef MDS_SDD1306_DISPLAY
#define MDS_SDD1306_DISPLAY
 
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define MDS_DISPLAY_MAX_LINES	8

class MDS_Display {
	public:
        MDS_Display(uint16_t width,
                    uint16_t height,
                    int8_t resetPin,
                    TwoWire* wire,
                    uint8_t lineCount,
                    unsigned long displayTimeout);
        ~MDS_Display();
        void				setup(bool rotate);
        bool				clearMessage();
        void				clear();
        void				D(String m);
		Adafruit_SSD1306*	getDevice();
		void				tick();

	private:
		Adafruit_SSD1306	display;
		bool				isSetup = false;
		String				lines[MDS_DISPLAY_MAX_LINES];
		uint8_t				lineCount;
		unsigned long		displayTimeout;
		unsigned long		nextClearTime;
};

#endif
