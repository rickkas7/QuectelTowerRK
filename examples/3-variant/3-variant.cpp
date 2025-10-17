#include "Particle.h"


#include "QuectelTowerRK.h"

SerialLogHandler logHandler(LOG_LEVEL_INFO);

SYSTEM_MODE(SEMI_AUTOMATIC);

#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED); // System thread defaults to on in 6.2.0 and later and this line is not required
#endif

std::chrono::milliseconds checkPeriod = 60s;
unsigned long lastCheck = 0;

char jsonBuf[1024];

void setup() {
    Particle.connect();
}

void loop() {
    if (lastCheck == 0 || millis() - lastCheck >= checkPeriod.count()) {
        lastCheck = millis();

        if (Cellular.ready()) {
            unsigned long start = millis();

            QuectelTowerRK::instance().scanWithCallback([start](QuectelTowerRK::TowerInfo towerInfo) {
                // This code is execute later
                unsigned long duration = millis() - start;

                CellularSignal signal;
                QuectelTowerRK::instance().getSignal(signal);

                Log.info("scan completed in %d ms, strength=%.1f, qual=%.1f", (int)duration, signal.getStrength(), signal.getQuality());

                Log.info("serving: %s", towerInfo.serving.toString().c_str());

                size_t count = towerInfo.neighbors.size();
                for(size_t ii = 0; ii < count; ii++) {
                    Log.info("neighbor %d: %s", (int)(ii + 1), towerInfo.neighbors.at(ii).toString().c_str());
                }

                // This is how you save to a Variant, which is used for extended publish in Device OS 6.2.0 and later.
                Variant obj;
                towerInfo.toVariant(obj);
                
                Log.info("json: %s", obj.toJSON().c_str());
            });


        }
    }
}