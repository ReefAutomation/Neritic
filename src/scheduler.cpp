
#include "scheduler.h"
#include "config.h"
#include "debug.h"
#include <math.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

const Timer* Scheduler::getActiveTimer() {
    if (!isTimeValid()) return nullptr;
    int currentMinutes = getCurrentTimeInMinutes();
    const Timer* best = nullptr;
    int bestMinutes = -1;
    for (const auto& t : _config->timers) {
        if (!isTimerActive(t, 0)) continue;
        int timerMinutes = getTimerMinutes(t);
        if (timerMinutes == -1) continue;
        if (timerMinutes <= currentMinutes && timerMinutes > bestMinutes) {
            bestMinutes = timerMinutes;
            best = &t;
        }
    }
    // If no timer has triggered today, select the last timer of the previous day
    if (!best) {
        int latestMinutes = -1;
        for (const auto& t : _config->timers) {
            if (!isTimerActive(t, 0)) continue;
            int timerMinutes = getTimerMinutes(t);
            if (timerMinutes == -1) continue;
            if (timerMinutes > latestMinutes) {
                latestMinutes = timerMinutes;
                best = &t;
            }
        }
    }
    return best;
}

Scheduler::Scheduler(Configuration* config) {
    _config = config;
    int tzOffset = 0;
    if (_config) tzOffset = _config->getTimezoneOffsetSeconds();
    _timeClient = new NTPClient(_ntpUDP, _config->time.ntpServer.c_str(), tzOffset, NTP_UPDATE_INTERVAL);
}

void Scheduler::begin() {
    _timeClient->begin();
    updateNTP();
}

int Scheduler::getCurrentTimeInMinutes() {
    return timeToMinutes(getCurrentHour(), getCurrentMinute());
}

void Scheduler::update() {
    // Completely disable NTP logic in AP mode (no time sync attempts)
    #if defined(ESP8266)
    bool apMode = (WiFi.getMode() == WIFI_AP);
    #else
    bool apMode = (WiFi.getMode() == WIFI_MODE_AP);
    #endif
    if (!apMode) {
        // If time is not valid, force NTP update every second
        if (!isTimeValid()) {
            if (millis() - _lastNTPUpdate > 1000) {
                updateNTP();
            }
        } else {
            // Update NTP periodically
            if (millis() - _lastNTPUpdate > NTP_UPDATE_INTERVAL) {
                updateNTP();
            }
        }
        // Update time client
        _timeClient->update();
    }
    // Calculate sun times only once per day at midnight or on first update
    static bool sunTimesCalculated = false;
    if (_sunriseMinutes == -1 || (getCurrentHour() == 0 && getCurrentMinute() == 0 && !sunTimesCalculated)) {
        calculateSunTimes();
        sunTimesCalculated = true;
    }
    // Reset flag after midnight
    if (getCurrentHour() != 0 || getCurrentMinute() != 0) {
        sunTimesCalculated = false;
    }
}

void Scheduler::updateNTP() {
    if (_config) {
        String ntpServer = _config->time.ntpServer;
        if (ntpServer.length() == 0 || ntpServer == "null") {
            debugPrintln("No NTP server configured, skipping NTP update.");
            return;
        }
    }
    // Disable NTP update if in AP mode (no internet)
    #if defined(ESP8266)
    if (WiFi.getMode() == WIFI_AP) {
        debugPrintln("In AP mode, skipping NTP update.");
        return;
    }
    #else
    if (WiFi.getMode() == WIFI_MODE_AP) {
        debugPrintln("In AP mode, skipping NTP update.");
        return;
    }
    #endif
    _timeClient->forceUpdate();
    _lastNTPUpdate = millis();
    debugPrintln("NTP time updated");
}

bool Scheduler::isTimeValid() {
    if (_config) {
        String ntpServer = _config->time.ntpServer;
        if (ntpServer.length() == 0 || ntpServer == "null") {
            // Fallback: treat time as valid if NTP is disabled
            return true;
        }
    }
    return _timeClient->isTimeSet();
}

uint8_t Scheduler::getScheduledBrightness(int8_t presetId, int currentMinutes) {
    int mostRecentMinutes = -1;
    uint8_t mostRecentBrightness = 100;
    for (const auto& t : _config->timers) {
        if (!t.enabled) continue;
        if (t.presetId != presetId) continue;
        int timerMinutes = getTimerMinutes(t);
        if (timerMinutes == -1) continue;
        if (timerMinutes <= currentMinutes && timerMinutes > mostRecentMinutes) {
            mostRecentMinutes = timerMinutes;
            mostRecentBrightness = t.brightness;
        }
    }
    // If no timer has triggered today, select the last timer of the previous day
    if (mostRecentMinutes == -1) {
        int latestMinutes = -1;
        uint8_t latestBrightness = 100;
        for (const auto& t : _config->timers) {
            if (!t.enabled) continue;
            if (t.presetId != presetId) continue;
            int timerMinutes = getTimerMinutes(t);
            if (timerMinutes == -1) continue;
            if (timerMinutes > latestMinutes) {
                latestMinutes = timerMinutes;
                latestBrightness = t.brightness;
            }
        }
        mostRecentBrightness = latestBrightness;
    }
    return mostRecentBrightness;
}

String Scheduler::getCurrentTime() {
    // Get the current epoch time (UTC)
    unsigned long epoch = _timeClient->getEpochTime();
    int tzOffset = 0;
    if (_config) tzOffset = _config->getTimezoneOffsetSeconds();
    epoch += tzOffset;
    // Calculate hours, minutes, seconds in local time
    int hours = (epoch / 3600) % 24;
    int minutes = (epoch / 60) % 60;
    int seconds = epoch % 60;
    char buffer[9];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
    return String(buffer);
}

uint8_t Scheduler::getCurrentHour() {
    unsigned long epoch = _timeClient->getEpochTime();
    int tzOffset = 0;
    if (_config) tzOffset = _config->getTimezoneOffsetSeconds();
    epoch += tzOffset;
    return (epoch / 3600) % 24;
}

uint8_t Scheduler::getCurrentMinute() {
    unsigned long epoch = _timeClient->getEpochTime();
    int tzOffset = 0;
    if (_config) tzOffset = _config->getTimezoneOffsetSeconds();
    epoch += tzOffset;
    return (epoch / 60) % 60;
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
    // For aquariums, all timers are active every day if enabled
    return timer.enabled;
}

int Scheduler::getTimerMinutes(const Timer& timer) {
    int minutes = -1;
    
    switch (timer.type) {
        case TIMER_REGULAR:
            minutes = timeToMinutes(timer.hour, timer.minute);
            break;
            
        case TIMER_SUNRISE:
            if (_sunriseMinutes != -1) {
                minutes = _sunriseMinutes;
            }
            break;
            
        case TIMER_SUNSET:
            if (_sunsetMinutes != -1) {
                minutes = _sunsetMinutes;
            }
            break;
    }
    
    return minutes;
}


int8_t Scheduler::getCurrentScheduledPreset() {
    if (!isTimeValid()) return -1;

    int currentMinutes = timeToMinutes(getCurrentHour(), getCurrentMinute());
    int8_t mostRecentPreset = -1;
    int mostRecentMinutes = -1;
    int minutesInDay = 24 * 60;
    // Find the most recent timer that should have triggered (today)
    for (size_t i = 0; i < _config->timers.size(); i++) {
        if (!isTimerActive(_config->timers[i], 0)) continue;
        int timerMinutes = getTimerMinutes(_config->timers[i]);
        if (timerMinutes == -1) continue;
        if (_config->timers[i].presetId >= _config->getPresetCount()) continue;
        if (timerMinutes <= currentMinutes) {
            if (timerMinutes > mostRecentMinutes) {
                mostRecentMinutes = timerMinutes;
                mostRecentPreset = _config->timers[i].presetId;
            }
        }
    }
    // If no timer has triggered today, select the last timer of the previous day (highest timerMinutes overall)
    if (mostRecentPreset == -1) {
        int latestMinutes = -1;
        int8_t latestPreset = -1;
        for (size_t i = 0; i < _config->timers.size(); i++) {
            if (!isTimerActive(_config->timers[i], 0)) continue;
            int timerMinutes = getTimerMinutes(_config->timers[i]);
            if (timerMinutes == -1) continue;
            if (_config->timers[i].presetId >= _config->getPresetCount()) continue;
            if (timerMinutes > latestMinutes) {
                latestMinutes = timerMinutes;
                latestPreset = _config->timers[i].presetId;
            }
        }
        mostRecentPreset = latestPreset;
    }
    return mostRecentPreset;
}
