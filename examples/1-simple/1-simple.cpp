#include "Particle.h"


#include "QuectelTowerRK.h"

SerialLogHandler logHandler(LOG_LEVEL_INFO);

SYSTEM_MODE(SEMI_AUTOMATIC);

#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED); // System thread defaults to on in 6.2.0 and later and this line is not required
#endif

std::chrono::milliseconds checkPeriod = 60s;
unsigned long lastCheck = 0;

void setup() {
    Particle.connect();
}

void loop() {
    if (lastCheck == 0 || millis() - lastCheck >= checkPeriod.count()) {
        lastCheck = millis();

        if (Cellular.ready()) {
            QuectelTowerRK::TowerInfo towerInfo;

            int res = QuectelTowerRK::instance().scanBlocking(towerInfo);
            if (res == SYSTEM_ERROR_NONE) {
                towerInfo.log("towerInfo", LOG_LEVEL_INFO);
            }
            else {
                Log.info("scan error %d", res);
            }
        }
    }
}