#include "scheduler.h"
#include "debug.h"
#include <math.h>

Scheduler::Scheduler(Configuration* config) {
    _config = config;
    _timeClient = new NTPClient(_ntpUDP, _config->time.ntpServer.c_str(), 
                                _config->time.timezoneOffset * 3600, NTP_UPDATE_INTERVAL);
}

void Scheduler::begin() {
    _timeClient->begin();
    updateNTP();
}

void Scheduler::update() {
    // Update NTP periodically
    if (millis() - _lastNTPUpdate > NTP_UPDATE_INTERVAL) {
        updateNTP();
    }
    
    // Update time client
    _timeClient->update();
    
    // Calculate sun times once per day at midnight or on first update
    if (_sunriseMinutes == -1 || (getCurrentHour() == 0 && getCurrentMinute() == 0)) {
        calculateSunTimes();
    }
}

void Scheduler::updateNTP() {
    _timeClient->forceUpdate();
    _lastNTPUpdate = millis();
    debugPrintln("NTP time updated");
}

bool Scheduler::isTimeValid() {
    return _timeClient->isTimeSet();
}

String Scheduler::getCurrentTime() {
    return _timeClient->getFormattedTime();
}

uint8_t Scheduler::getCurrentHour() {
    return _timeClient->getHours();
}

uint8_t Scheduler::getCurrentMinute() {
    return _timeClient->getMinutes();
}

uint8_t Scheduler::getCurrentDayOfWeek() {
    return _timeClient->getDay();
}

// Simplified sunrise calculation using sine approximation
void Scheduler::calculateSunTimes() {
    if (_config->time.latitude == 0.0 && _config->time.longitude == 0.0) {
        // Default times if location not set
        _sunriseMinutes = 6 * 60;  // 6:00 AM
        _sunsetMinutes = 18 * 60;  // 6:00 PM
        return;
    }
    
    _sunriseMinutes = calculateSunriseMinutes();
    _sunsetMinutes = calculateSunsetMinutes();
    
    debugPrint("Sunrise: ");
    debugPrint(_sunriseMinutes / 60);
    debugPrint(":");
    debugPrintln(_sunriseMinutes % 60);
    debugPrint("Sunset: ");
    debugPrint(_sunsetMinutes / 60);
    debugPrint(":");
    debugPrintln(_sunsetMinutes % 60);
}

int Scheduler::calculateSunriseMinutes() {
    // Simplified calculation - in production, use a proper astronomy library
    // This is a basic approximation
    float lat = _config->time.latitude * PI / 180.0;
    
    // Day of year approximation
    unsigned long epochTime = _timeClient->getEpochTime();
    int dayOfYear = (epochTime / 86400) % 365;
    
    // Solar declination approximation
    float declination = 0.409 * sin(2 * PI / 365.0 * dayOfYear - 1.39);
    
    // Hour angle
    float hourAngle = acos(-tan(lat) * tan(declination));
    
    // Sunrise time in hours
    float sunriseHour = 12.0 - (hourAngle * 12.0 / PI);
    
    // Convert to minutes since midnight
    int minutes = (int)(sunriseHour * 60);
    
    // Clamp to reasonable values
    if (minutes < 4 * 60) minutes = 4 * 60;   // Not before 4 AM
    if (minutes > 10 * 60) minutes = 10 * 60;  // Not after 10 AM
    
    return minutes;
}

int Scheduler::calculateSunsetMinutes() {
    // Simplified calculation
    float lat = _config->time.latitude * PI / 180.0;
    
    unsigned long epochTime = _timeClient->getEpochTime();
    int dayOfYear = (epochTime / 86400) % 365;
    
    float declination = 0.409 * sin(2 * PI / 365.0 * dayOfYear - 1.39);
    float hourAngle = acos(-tan(lat) * tan(declination));
    
    // Sunset time in hours
    float sunsetHour = 12.0 + (hourAngle * 12.0 / PI);
    
    int minutes = (int)(sunsetHour * 60);
    
    // Clamp to reasonable values
    if (minutes < 16 * 60) minutes = 16 * 60;  // Not before 4 PM
    if (minutes > 22 * 60) minutes = 22 * 60;  // Not after 10 PM
    
    return minutes;
}

String Scheduler::getSunriseTime() {
    if (_sunriseMinutes == -1) return "N/A";
    
    char buffer[6];
    sprintf(buffer, "%02d:%02d", _sunriseMinutes / 60, _sunriseMinutes % 60);
    return String(buffer);
}

String Scheduler::getSunsetTime() {
    if (_sunsetMinutes == -1) return "N/A";
    
    char buffer[6];
    sprintf(buffer, "%02d:%02d", _sunsetMinutes / 60, _sunsetMinutes % 60);
    return String(buffer);
}

int Scheduler::timeToMinutes(uint8_t hour, uint8_t minute) {
    return hour * 60 + minute;
}

bool Scheduler::isTimerActive(const Timer& timer, uint8_t dayOfWeek) {
    if (!timer.enabled) return false;
    
    // Check if today is enabled (bit 0 = Sunday, bit 6 = Saturday)
    uint8_t dayBit = 1 << dayOfWeek;
    return (timer.days & dayBit) != 0;
}

int Scheduler::getTimerMinutes(const Timer& timer) {
    int minutes = -1;
    
    switch (timer.type) {
        case TIMER_REGULAR:
            minutes = timeToMinutes(timer.hour, timer.minute);
            break;
            
        case TIMER_SUNRISE:
            if (_sunriseMinutes != -1) {
                minutes = _sunriseMinutes + timer.offset;
            }
            break;
            
        case TIMER_SUNSET:
            if (_sunsetMinutes != -1) {
                minutes = _sunsetMinutes + timer.offset;
            }
            break;
    }
    
    return minutes;
}

int8_t Scheduler::checkTimers() {
    if (!isTimeValid()) return -1;
    
    // Check only once per minute
    uint32_t now = millis();
    if (now - _lastTimerCheck < 60000) return -1;
    _lastTimerCheck = now;
    
    int currentMinutes = timeToMinutes(getCurrentHour(), getCurrentMinute());
    uint8_t currentDay = getCurrentDayOfWeek();
    
    // Check all timers
    for (int i = 0; i < MAX_TIMERS + MAX_SUN_TIMERS; i++) {
        if (!isTimerActive(_config->timers[i], currentDay)) continue;
        
        int timerMinutes = getTimerMinutes(_config->timers[i]);
        if (timerMinutes == -1) continue;
        
        // Trigger if current time matches timer time
        if (currentMinutes == timerMinutes) {
            debugPrint("Timer triggered: ");
            debugPrintln(i);
            return _config->timers[i].presetId;
        }
    }
    
    return -1;
}

int8_t Scheduler::getBootPreset() {
    if (!isTimeValid()) return -1;
    
    int currentMinutes = timeToMinutes(getCurrentHour(), getCurrentMinute());
    uint8_t currentDay = getCurrentDayOfWeek();
    
    int8_t mostRecentPreset = -1;
    int mostRecentMinutes = -1;
    
    // Find the most recent timer that should have triggered
    for (int i = 0; i < MAX_TIMERS + MAX_SUN_TIMERS; i++) {
        if (!isTimerActive(_config->timers[i], currentDay)) continue;
        
        int timerMinutes = getTimerMinutes(_config->timers[i]);
        if (timerMinutes == -1) continue;
        
        // Timer should have triggered if it's before current time
        if (timerMinutes <= currentMinutes) {
            if (timerMinutes > mostRecentMinutes) {
                mostRecentMinutes = timerMinutes;
                mostRecentPreset = _config->timers[i].presetId;
            }
        }
    }
    
    if (mostRecentPreset != -1) {
        debugPrint("Boot preset from timer: ");
        debugPrintln(mostRecentPreset);
    }
    
    return mostRecentPreset;
}
