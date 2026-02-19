#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "config.h"
#include "esp_sntp.h"
#include <string>
#include <time.h>

class Scheduler {
public:
  // Returns a pointer to the active timer for the current time, or nullptr if
  // none
  const Timer *getActiveTimer();
  Scheduler(Configuration *config);

  void begin();
  void update();

  bool isTimeValid();
  std::string getCurrentTime();
  uint8_t getCurrentHour();
  uint8_t getCurrentMinute();

  void calculateSunTimes();
  std::string getSunriseTime();
  std::string getSunsetTime();

  // Returns the brightness for the given presetId and time (minutes), or 100 if
  // not found
  uint8_t getScheduledBrightness(int8_t presetId, int currentMinutes);
  int8_t
  getCurrentScheduledPreset(); // Returns preset that should be active on boot
  // Returns the current time in minutes since midnight
  int getCurrentTimeInMinutes();
  int timeToMinutes(uint8_t hour, uint8_t minute);
  int getTimerMinutes(const Timer &timer);

  Configuration *_config;

  uint64_t _lastNTPUpdate = 0;
  volatile bool _timeValid = false;
  static Scheduler *_instance;

  int _sunriseMinutes = -1;
  int _sunsetMinutes = -1;

  void updateNTP();
  int calculateSunriseMinutes();
  int calculateSunsetMinutes();
  bool isTimerActive(const Timer &timer, uint8_t dayOfWeek);
};

#endif
