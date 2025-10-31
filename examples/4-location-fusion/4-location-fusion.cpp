#include "Particle.h"

#include "LocationFusionRK.h"
#include "QuectelTowerRK.h"

SerialLogHandler logHandler(LOG_LEVEL_TRACE);

SYSTEM_MODE(SEMI_AUTOMATIC);

#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED); // System thread defaults to on in 6.2.0 and later and this line is not required
#endif

void locEnhancedCallback(const Variant &variant);

void setup() {
    
    LocationFusionRK::instance()
        .withAddTower(true)
        .withAddWiFi(true)
        .withPublishPeriodic(5min)
        .withLocEnhancedHandler(locEnhancedCallback)
        .withAddToEventHandler(QuectelTowerRK::addToEventHandler)
        .setup();

#if Wiring_WiFi 
    WiFi.on();
#endif // Wiring_WiFi

    Particle.connect();
}

void loop() {
}

void locEnhancedCallback(const Variant &variant) {
    Variant locEnhanced = variant.get("loc-enhanced");
    
    Log.info("locEnhancedCallback %s", locEnhanced.toJSON().c_str());

    // Fields in locEnhanced:
    // - h_acc horizontal accuracy (meters)
    // - lat latitude
    // - lon longitude
}
