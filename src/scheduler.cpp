#include "scheduler.h"
#include "config.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "network.h"
#include <math.h>
#include <string>
#include <time.h>

static const char *TAG = "scheduler";

static unsigned long getEpochTime() { return (unsigned long)time(NULL); }

Scheduler::Scheduler(Configuration *config) {
  _config = config;
  Scheduler::_instance = this;
}

void Scheduler::begin() {
  if (!_config)
    return;
  if (_config->time.ntpServer.empty() || _config->time.ntpServer == "null")
    return;
  esp_sntp_stop();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, _config->time.ntpServer.c_str());
  sntp_set_sync_interval(NTP_UPDATE_INTERVAL);
  // Register SNTP sync notification callback
  sntp_set_time_sync_notification_cb([](struct timeval *tv) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI("scheduler", "SNTP sync notification: system time set to %s", buf);
    // Set persistent time valid flag
    if (Scheduler::_instance)
      Scheduler::_instance->_timeValid = true;
  });
  esp_sntp_init();
  _lastNTPUpdate = (uint64_t)(esp_timer_get_time() / 1000ULL);
  ESP_LOGI(TAG, "SNTP initialized with server: %s",
           _config->time.ntpServer.c_str());
}

Scheduler *Scheduler::_instance = nullptr;
void Scheduler::updateNTP() {
  if (!_config)
    return;
  if (_config->time.ntpServer.empty() || _config->time.ntpServer == "null")
    return;
  if (!networkIsStaConnected())
    return;
  esp_sntp_stop();
  esp_sntp_setservername(0, _config->time.ntpServer.c_str());
  esp_sntp_init();
  _lastNTPUpdate = (uint64_t)(esp_timer_get_time() / 1000ULL);
  ESP_LOGI(TAG, "NTP updated");
}

bool Scheduler::isTimeValid() {
  if (_config &&
      (_config->time.ntpServer.empty() || _config->time.ntpServer == "null")) {
    return true;
  }
  return _timeValid;
}

uint8_t Scheduler::getCurrentHour() {
  long tzOffset = _config ? _config->getTimezoneOffsetSeconds() : 0;
  unsigned long epoch = getEpochTime() + (unsigned long)tzOffset;
  return (epoch / 3600) % 24;
}

uint8_t Scheduler::getCurrentMinute() {
  long tzOffset = _config ? _config->getTimezoneOffsetSeconds() : 0;
  unsigned long epoch = getEpochTime() + (unsigned long)tzOffset;
  return (epoch / 60) % 60;
}

int Scheduler::getCurrentTimeInMinutes() {
  return timeToMinutes(getCurrentHour(), getCurrentMinute());
}

std::string Scheduler::getCurrentTime() {
  long tzOffset = _config ? _config->getTimezoneOffsetSeconds() : 0;
  unsigned long epoch = getEpochTime() + (unsigned long)tzOffset;
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", (int)((epoch / 3600) % 24),
           (int)((epoch / 60) % 60), (int)(epoch % 60));
  return std::string(buf);
}

int Scheduler::timeToMinutes(uint8_t hour, uint8_t minute) {
  return (int)hour * 60 + (int)minute;
}

bool Scheduler::isTimerActive(const Timer &timer, uint8_t /* dayOfWeek */) {
  return timer.enabled;
}

int Scheduler::getTimerMinutes(const Timer &timer) {
  switch (timer.type) {
  case TIMER_REGULAR:
    return timeToMinutes(timer.hour, timer.minute);
  case TIMER_SUNRISE:
    return _sunriseMinutes;
  case TIMER_SUNSET:
    return _sunsetMinutes;
  default:
    return -1;
  }
}

void Scheduler::update() {
  static bool sunCalcDone = false;
  if (_sunriseMinutes == -1 ||
      (getCurrentHour() == 0 && getCurrentMinute() == 0 && !sunCalcDone)) {
    calculateSunTimes();
    sunCalcDone = true;
  }
  if (getCurrentHour() != 0 || getCurrentMinute() != 0) {
    sunCalcDone = false;
  }
}

void Scheduler::calculateSunTimes() {
  if (!_config ||
      (_config->time.latitude == 0.0 && _config->time.longitude == 0.0)) {
    _sunriseMinutes = 6 * 60;
    _sunsetMinutes = 18 * 60;
    return;
  }
  _sunriseMinutes = calculateSunriseMinutes();
  _sunsetMinutes = calculateSunsetMinutes();
}

int Scheduler::calculateSunriseMinutes() {
  float lat = (float)(_config->time.latitude * M_PI / 180.0);
  int dayOfYear = (int)((getEpochTime() / 86400) % 365);
  float dec = 0.409f * sinf(2.0f * (float)M_PI / 365.0f * dayOfYear - 1.39f);
  float cosHA = -tanf(lat) * tanf(dec);
  if (cosHA < -1.0f)
    cosHA = -1.0f;
  if (cosHA > 1.0f)
    cosHA = 1.0f;
  float ha = acosf(cosHA);
  int m = (int)((12.0f - ha * 12.0f / (float)M_PI) * 60.0f);
  if (m < 4 * 60)
    m = 4 * 60;
  if (m > 10 * 60)
    m = 10 * 60;
  return m;
}

int Scheduler::calculateSunsetMinutes() {
  float lat = (float)(_config->time.latitude * M_PI / 180.0);
  int dayOfYear = (int)((getEpochTime() / 86400) % 365);
  float dec = 0.409f * sinf(2.0f * (float)M_PI / 365.0f * dayOfYear - 1.39f);
  float cosHA = -tanf(lat) * tanf(dec);
  if (cosHA < -1.0f)
    cosHA = -1.0f;
  if (cosHA > 1.0f)
    cosHA = 1.0f;
  float ha = acosf(cosHA);
  int m = (int)((12.0f + ha * 12.0f / (float)M_PI) * 60.0f);
  if (m < 16 * 60)
    m = 16 * 60;
  if (m > 22 * 60)
    m = 22 * 60;
  return m;
}

std::string Scheduler::getSunriseTime() {
  if (_sunriseMinutes == -1)
    return "N/A";
  int m = (_sunriseMinutes < 0)
              ? 0
              : (_sunriseMinutes > 1439 ? 1439 : _sunriseMinutes);
  int h = m / 60, mm = m % 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, mm);
  return std::string(buf);
}

std::string Scheduler::getSunsetTime() {
  if (_sunsetMinutes == -1)
    return "N/A";
  int m = (_sunsetMinutes < 0)
              ? 0
              : (_sunsetMinutes > 1439 ? 1439 : _sunsetMinutes);
  int h = m / 60, mm = m % 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, mm);
  return std::string(buf);
}

int8_t Scheduler::getCurrentScheduledPreset() {
  if (!isTimeValid())
    return -1;
  int cur = getCurrentTimeInMinutes();
  int8_t best = -1;
  int bestM = -1;
  for (size_t i = 0; i < _config->timers.size(); i++) {
    if (!isTimerActive(_config->timers[i], 0))
      continue;
    int m = getTimerMinutes(_config->timers[i]);
    if (m == -1)
      continue;
    if (_config->timers[i].presetId >= _config->getPresetCount())
      continue;
    if (m <= cur && m > bestM) {
      bestM = m;
      best = (int8_t)_config->timers[i].presetId;
    }
  }
  if (best == -1) {
    int latM = -1;
    for (size_t i = 0; i < _config->timers.size(); i++) {
      if (!isTimerActive(_config->timers[i], 0))
        continue;
      int m = getTimerMinutes(_config->timers[i]);
      if (m == -1)
        continue;
      if (_config->timers[i].presetId >= _config->getPresetCount())
        continue;
      if (m > latM) {
        latM = m;
        best = (int8_t)_config->timers[i].presetId;
      }
    }
  }
  return best;
}

const Timer *Scheduler::getActiveTimer() {
  if (!isTimeValid())
    return nullptr;
  int cur = getCurrentTimeInMinutes();
  const Timer *best = nullptr;
  int bestM = -1;
  for (const auto &t : _config->timers) {
    if (!isTimerActive(t, 0))
      continue;
    int m = getTimerMinutes(t);
    if (m == -1)
      continue;
    if (m <= cur && m > bestM) {
      bestM = m;
      best = &t;
    }
  }
  if (!best) {
    int latM = -1;
    for (const auto &t : _config->timers) {
      if (!isTimerActive(t, 0))
        continue;
      int m = getTimerMinutes(t);
      if (m == -1)
        continue;
      if (m > latM) {
        latM = m;
        best = &t;
      }
    }
  }
  return best;
}

uint8_t Scheduler::getScheduledBrightness(int8_t presetId, int currentMinutes) {
  int mostRecentM = -1;
  uint8_t mostRecentBrightness = 100;
  for (const auto &t : _config->timers) {
    if (!t.enabled || t.presetId != (uint8_t)presetId)
      continue;
    int m = getTimerMinutes(t);
    if (m == -1)
      continue;
    if (m <= currentMinutes && m > mostRecentM) {
      mostRecentM = m;
      mostRecentBrightness = t.brightness;
    }
  }
  if (mostRecentM == -1) {
    int latM = -1;
    for (const auto &t : _config->timers) {
      if (!t.enabled || t.presetId != (uint8_t)presetId)
        continue;
      int m = getTimerMinutes(t);
      if (m != -1 && m > latM) {
        latM = m;
        mostRecentBrightness = t.brightness;
      }
    }
  }
  return mostRecentBrightness;
}
