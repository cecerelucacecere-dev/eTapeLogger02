//Project Name: CouleeRunoff_02 eTape Code
//Device Name: CouleeRunoff_02
//Device CoreID: e00fce6816fe5cd226cc300b
//Author: Luca Cecere
//Credit to CSU Agricultural Water Quality Program Team
//This is a test to see if pushing will work

#include "Particle.h"

#ifndef SYSTEM_VERSION_v620
SYSTEM_THREAD(ENABLED); // System thread defaults to on in 6.2.0 and later and this line is not required
#endif

SerialLogHandler logHandler;

// Sleep time between cycles (measurement period)
const std::chrono::minutes publishPeriod = 5min;

// Time to turn sleep OFF off before taking a measurement
const std::chrono::seconds sensorWarmup = 10s;

// Time until sleep is turned ON after taking a measurement
const std::chrono::seconds postDelay = 10s;

// The event name to publish with
const char *eventName = "eTape Log 2";
//const char *eventName2 = "Total Log";

const pin_t ETAPE_PIN = A0;     // your eTape on A0
const float VREF      = 3.3f;   // Boron ADC full-scale
const int   NUMSAMPLES = 20;    // Amount of samples taken for smoothing
const int   batchSize = 6;      // Amount of readings in a batch

//declaration of variables
FuelGauge fuel;

int   v = 0;

struct Reading
{                 
    float depth;
    float volts;
    float batterySoc;
    float batteryVolts;
    float cellStrength;
    unsigned long timestamp;
};

Reading currentReading;
retained Reading readings[batchSize]; //3 readings in 3 minutes
retained int readingCount = 0;

//declaration of functions
void takeMeasurement();         // Take a reading
void publishBatch();            // Publish readings to Google SHEETS
void goToSleep();               // Go into sleep mode
void battSettings();            // Configure PMIC
void storeReading();            // batch load readings

void setup() {
    
//begin serial connection
    Serial.begin(9600);
    waitFor(Serial.isConnected, 3000);
    Log.info("Starting eTape logger");
    battSettings();
    
}

void loop() {
    
    // 1) Warmup period
    delay((uint32_t)(sensorWarmup / 1ms));              //Turn sleep OFF

    // 2) Take a measurement
    takeMeasurement();    
    
    // 3) Store reading internally
    storeReading();

    // 4) Post-measurement delay
    delay((uint32_t)(postDelay / 1ms));

    // 5) Publish batch measurment
    if (readingCount >= batchSize){
        publishBatch();
    }
    // 6) Go to sleep
    goToSleep();
}

void takeMeasurement() {
    
    long sum = 0;

    for (int i = 0; i < NUMSAMPLES; i++) {
        v = analogRead(ETAPE_PIN);
        sum += v;
        delay(50);
    }

    float smoothed = (float)sum / (float)NUMSAMPLES;

    currentReading.volts = (smoothed / 4095.0f) * VREF;

    if (currentReading.volts < 0.05f) {
        currentReading.depth = 0.0f;
    }
    else if (currentReading.volts >= 0.05f && currentReading.volts < 0.1f) {
        currentReading.depth = 0.762f;   // small depth placeholder
    }
    else {
        currentReading.depth = (currentReading.volts * 19.8f - 0.2271f) - 2.54f;
    }

    // Update battery State of Charge(SoC)
    currentReading.batterySoc = System.batteryCharge();
    currentReading.batteryVolts = fuel.getVCell();

    CellularSignal sig = Cellular.RSSI();
    currentReading.cellStrength = sig.getStrength();

    currentReading.timestamp = Time.now();
}

void storeReading() {

    if (readingCount < batchSize) {
        readings[readingCount] = currentReading;
        readingCount++;

    }
}
// prepares a JSON-ish array and publishes it
void publishBatch() {
   
    if (!Particle.connected()) {
        Particle.connect();
        // Wait up to 3 minutes for connection
        waitFor(Particle.connected, 3 * 60 * 1000);
    }

    if (Particle.connected()) {
        
        char payload[512];
        payload[0] = '\0';

        strcat(payload, "[");
        
        for (int i = 0; i < readingCount; i++){
            char temp[64];

            snprintf(temp,sizeof(temp),
            "[%lu,%.2f,%.2f,%.2f,%.2f,%.2f]",
            readings[i].timestamp,
            readings[i].depth,
            readings[i].batteryVolts,
            readings[i].cellStrength,
            readings[i].volts,
            readings[i].batterySoc
        );

        strcat(payload, temp);

        if (i < (readingCount - 1)){
            strcat(payload, ",");
        }
        }

        strcat(payload, "]");

        Particle.publish(eventName, payload, PRIVATE);

        readingCount = 0;

    }
}

void goToSleep() {

    SystemSleepConfiguration config;
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)
      .duration(publishPeriod)
      .network(NETWORK_INTERFACE_CELLULAR, SystemSleepNetworkFlag::INACTIVE_STANDBY);

    System.sleep(config);
    // On wake, we drop back into loop() and do the cycle again
}

void battSettings() {
  
  SystemPowerConfiguration conf; 
  conf.powerSourceMaxCurrent(900)    // 5W / 5V = 1000mA. 900mA is the closest PMIC register setting.
      .powerSourceMinVoltage(3880)  
      .batteryChargeCurrent(500)
      .batteryChargeVoltage(4110);  
      
  System.setPowerConfiguration(conf);
}

