#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "config.h"

class Scheduler {
public:
    Scheduler(Configuration* config);
    
    void begin();
    void update();
    
    bool isTimeValid();
    String getCurrentTime();
    uint8_t getCurrentHour();
    uint8_t getCurrentMinute();
    
    void calculateSunTimes();
    String getSunriseTime();
    String getSunsetTime();
    
    int8_t checkTimers();  // Returns preset ID to apply, -1 if none
    int8_t getCurrentScheduledPreset();  // Returns preset that should be active on boot
    
private:
    Configuration* _config;
    WiFiUDP _ntpUDP;
    NTPClient* _timeClient;
    
    uint32_t _lastNTPUpdate = 0;
    uint32_t _lastTimerCheck = 0;
    
    int _sunriseMinutes = -1;  // Minutes since midnight
    int _sunsetMinutes = -1;
    
    void updateNTP();
    int calculateSunriseMinutes();
    int calculateSunsetMinutes();
    int timeToMinutes(uint8_t hour, uint8_t minute);
    bool isTimerActive(const Timer& timer, uint8_t dayOfWeek);
    int getTimerMinutes(const Timer& timer);
};

#endif
