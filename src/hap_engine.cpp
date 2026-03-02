#include "hap_engine.h"

#include "esp_log.h"
#include "esp_mac.h"

#if defined(ENABLE_NATIVE_HOMEKIT)
extern "C" {
#include <hap.h>
#include <hap_apple_chars.h>
#include <hap_apple_servs.h>
}
#include <cstdio>
#include <cstring>
#endif

static const char *TAG = "hap_engine";

#if defined(ENABLE_NATIVE_HOMEKIT)
static bool s_initialized = false;
static bool s_started = false;
static const char *s_status = "not-initialized";
static bool s_lastPower = false;
static uint8_t s_lastBrightness = 0;
static HapEnginePowerWriteCallback s_powerWriteCallback = nullptr;
static HapEngineBrightnessWriteCallback s_brightnessWriteCallback = nullptr;
static hap_acc_t *s_accessory = nullptr;
static hap_serv_t *s_lightService = nullptr;
static hap_char_t *s_onChar = nullptr;
static hap_char_t *s_brightnessChar = nullptr;
static char s_serialNum[13] = "DG0000000000";

static int hapIdentify(hap_acc_t *acc) {
  (void)acc;
  ESP_LOGI(TAG, "HomeKit accessory identified");
  return HAP_SUCCESS;
}

static int hapLightbulbWrite(hap_write_data_t write_data[], int count,
                             void *serv_priv, void *write_priv) {
  (void)serv_priv;
  (void)write_priv;

  int ret = HAP_SUCCESS;
  for (int i = 0; i < count; i++) {
    hap_write_data_t *write = &write_data[i];
    *(write->status) = HAP_STATUS_VAL_INVALID;

    const char *uuid = hap_char_get_type_uuid(write->hc);
    if (!uuid) {
      ret = HAP_FAIL;
      continue;
    }

    if (!std::strcmp(uuid, HAP_CHAR_UUID_ON)) {
      if (s_powerWriteCallback && s_powerWriteCallback(write->val.b)) {
        *(write->status) = HAP_STATUS_SUCCESS;
      }
    } else if (!std::strcmp(uuid, HAP_CHAR_UUID_BRIGHTNESS)) {
      int rawBrightness = write->val.i;
      if (rawBrightness < 0) {
        rawBrightness = 0;
      }
      if (rawBrightness > 100) {
        rawBrightness = 100;
      }
      if (s_brightnessWriteCallback &&
          s_brightnessWriteCallback((uint8_t)rawBrightness)) {
        *(write->status) = HAP_STATUS_SUCCESS;
      }
    } else {
      *(write->status) = HAP_STATUS_RES_ABSENT;
    }

    if (*(write->status) == HAP_STATUS_SUCCESS) {
      hap_char_update_val(write->hc, &(write->val));
    } else {
      ret = HAP_FAIL;
    }
  }

  return ret;
}
#endif

bool hapEngineInit(const HapEngineConfig &config,
                   HapEnginePowerWriteCallback powerWriteCallback,
                   HapEngineBrightnessWriteCallback brightnessWriteCallback) {
#if !defined(ENABLE_NATIVE_HOMEKIT)
  (void)config;
  (void)powerWriteCallback;
  (void)brightnessWriteCallback;
  return false;
#else
  s_powerWriteCallback = powerWriteCallback;
  s_brightnessWriteCallback = brightnessWriteCallback;

  if (s_started) {
    return true;
  }

  if (!config.accessoryName || !config.accessoryName[0] || !config.setupCode ||
      !config.setupCode[0] || !config.setupId || !config.setupId[0]) {
    s_status = "invalid-config";
    ESP_LOGE(TAG, "Missing required HomeKit setup configuration");
    return false;
  }

  if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS) {
    s_status = "hap-init-failed";
    ESP_LOGE(TAG, "hap_init failed");
    return false;
  }
  s_initialized = true;
  s_status = "initialized";

  uint8_t staMac[6] = {0};
  if (esp_read_mac(staMac, ESP_MAC_WIFI_STA) == ESP_OK) {
    snprintf(s_serialNum, sizeof(s_serialNum), "DG%02X%02X%02X%02X%02X",
             staMac[1], staMac[2], staMac[3], staMac[4], staMac[5]);
  }

  hap_acc_cfg_t cfg = {
      .name = (char *)config.accessoryName,
      .model = (char *)"DeepGlow-Light",
      .manufacturer = (char *)"DeepGlow",
      .serial_num = s_serialNum,
      .fw_rev = (char *)"1.0.0",
      .hw_rev = (char *)"1.0",
      .pv = (char *)"1.1.0",
      .cid = HAP_CID_LIGHTING,
      .identify_routine = hapIdentify,
  };

  s_accessory = hap_acc_create(&cfg);
  if (!s_accessory) {
    s_status = "accessory-create-failed";
    ESP_LOGE(TAG, "hap_acc_create failed");
    return false;
  }

  uint8_t productData[] = {'D', 'G', 'L', 'O', 'W', 'H', 'K', '1'};
  if (hap_acc_add_product_data(s_accessory, productData,
                               sizeof(productData)) != HAP_SUCCESS) {
    ESP_LOGW(TAG, "Failed to add HomeKit product data");
  }

  s_lightService = hap_serv_lightbulb_create(true);
  if (!s_lightService) {
    s_status = "service-create-failed";
    ESP_LOGE(TAG, "hap_serv_lightbulb_create failed");
    return false;
  }

  int ret = hap_serv_add_char(s_lightService, hap_char_name_create((char *)config.accessoryName));
  ret |= hap_serv_add_char(s_lightService, hap_char_brightness_create(100));
  if (ret != HAP_SUCCESS) {
    s_status = "service-char-failed";
    ESP_LOGE(TAG, "Failed to add lightbulb characteristics");
    return false;
  }

  hap_serv_set_write_cb(s_lightService, hapLightbulbWrite);
  hap_acc_add_serv(s_accessory, s_lightService);
  hap_add_accessory(s_accessory);

  s_onChar = hap_serv_get_char_by_uuid(s_lightService, HAP_CHAR_UUID_ON);
  s_brightnessChar =
      hap_serv_get_char_by_uuid(s_lightService, HAP_CHAR_UUID_BRIGHTNESS);

  hap_set_setup_code(config.setupCode);
  if (hap_set_setup_id(config.setupId) != HAP_SUCCESS) {
    s_status = "setup-id-failed";
    ESP_LOGE(TAG, "Failed to set HomeKit setup ID");
    return false;
  }

  if (hap_start() != HAP_SUCCESS) {
    s_status = "hap-start-failed";
    ESP_LOGE(TAG, "hap_start failed");
    return false;
  }

  if (hap_update_config_number() != HAP_SUCCESS) {
    ESP_LOGW(TAG, "Failed to trigger HomeKit mDNS re-announce");
  }

  s_started = true;
  s_status = "running";
  ESP_LOGI(TAG, "Native HomeKit accessory started: %s", config.accessoryName);
  return true;
#endif
}

void hapEngineLoop() {
}

void hapEngineNotifyState(bool power, uint8_t brightnessPercent) {
#if defined(ENABLE_NATIVE_HOMEKIT)
  if (!s_started) {
    return;
  }

  if (power == s_lastPower && brightnessPercent == s_lastBrightness) {
    return;
  }

  s_lastPower = power;
  s_lastBrightness = brightnessPercent;

  if (s_onChar) {
    hap_val_t val;
    val.b = power;
    hap_char_update_val(s_onChar, &val);
  }

  if (s_brightnessChar) {
    hap_val_t val;
    val.i = brightnessPercent;
    hap_char_update_val(s_brightnessChar, &val);
  }
#else
  (void)power;
  (void)brightnessPercent;
#endif
}

const char *hapEngineStatus() {
#if !defined(ENABLE_NATIVE_HOMEKIT)
  return "not-compiled";
#else
  return s_status;
#endif
}
