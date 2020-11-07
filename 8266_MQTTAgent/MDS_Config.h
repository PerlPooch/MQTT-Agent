#ifndef MDS_CONFIG
#define MDS_CONFIG
 
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>

#define CONFIG_DOC_SIZE		512



class MDS_Config {
	public:
        MDS_Config();
        ~MDS_Config();
        void 					setup();
		void					load();
		void					save();
		void					print();
		void					setSetConfigCallback(std::function<void(StaticJsonDocument<CONFIG_DOC_SIZE>&)> callback);
		void					setGetConfigCallback(std::function<void(StaticJsonDocument<CONFIG_DOC_SIZE>&)> callback);

	private:
		std::function<void(StaticJsonDocument<CONFIG_DOC_SIZE>&)> setDataCallback;
		std::function<void(StaticJsonDocument<CONFIG_DOC_SIZE>&)> getDataCallback;
};

#endif
