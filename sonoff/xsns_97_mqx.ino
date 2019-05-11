#include "mqx/mq7.h"
#include "mqx/mq2.h"
#include "mqx/ads1115.h"

#ifndef XSNS_97
#define XSNS_97            97

// A MQ7 sensor instance
MQ7Sensor *mgx_mq7_sensor = NULL;

// A MQ2 sensor instance
MQ2Sensor *mgx_mq2_sensor = NULL;

// ADS1115 reader
ADS1115Reader *mgx_ads = NULL;

bool mqx_has_power_ctl = false;


void snsMqx_Init(void) {
  mgx_mq2_sensor = new MQ2Sensor(&Settings.snsMqx, *mgx_ads);
  mgx_mq2_sensor->start();

  // MQ-7 require separate heat contol pin. If no such pin, no MQ-7 installed
  if (pin[GPIO_MQ7_HEAT] < 99) {
    mgx_mq7_sensor = new MQ7Sensor(&Settings.snsMqx, *mgx_ads);
  }

  // If GPIO_MQ_POWER is set, enable power control
  if (pin[GPIO_MQ_POWER] < 99) {
    mqx_has_power_ctl = true;
    // If set, turn power on/off according to the settings
    pinMode(pin[GPIO_MQ_POWER], OUTPUT);
    digitalWrite(pin[GPIO_MQ_POWER], Settings.snsMqx.mqx_powered);
  }
}

#ifdef USE_WEBSERVER

void snsMqx_Show(void) {
  if (!mgx_ads->initialized()) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}Detecting 1115...{m}{e}"), mqtt_data);
    return;
  }

  if (mgx_mq2_sensor) {
    snsMqx_ShowMQ2();
  }

  if (mgx_mq7_sensor) {
    snsMqx_ShowMQ7();
  }
}

void snsMqx_ShowMQ2(void) {
  if (mgx_mq2_sensor->isCalibrating()) {
    char str_cal[24];
    dtostrf(mgx_mq2_sensor->resistance(), 6, 2, str_cal);
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}Calibrating MQ-2...{m}Counting:%d, %s kOhm{e}"), mqtt_data, mgx_mq2_sensor->calibrationLoop(), str_cal);
    return;
  }

  if (Settings.snsMqx.mq2_Ro_value < 0.01 || isinf(Settings.snsMqx.mq2_Ro_value)) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}MQ-2 failed to calculate Ro{m}{e}"), mqtt_data);
    return;
  }

  char label[15];
  snprintf_P(label, sizeof(label), "MQ-2 (%02x)", mgx_ads->address());

  char str_res[24];
  dtostrf(mgx_mq2_sensor->resistance(), 6, 2, str_res);
  char str_ppm[24];
  dtostrf(mgx_mq2_sensor->ppmReading(), 6, 4, str_ppm);
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR( "%s{s}%s{m}Rs=%s kOhm, CH4=%s PPM{e}"), mqtt_data, label, str_res, str_ppm);
}

void snsMqx_ShowMQ7(void) {

  char label[15];
  snprintf_P(label, sizeof(label), "MQ-7 (%02x)", mgx_ads->address());

  char status[32];
  snprintf_P(status, sizeof(status), "%s %s",
    mgx_mq7_sensor->isHeating() ? "Heat" : "Cool",
    mgx_mq7_sensor->isReading() ? "Read" : ""
  );

  char str_resistance[24];
  dtostrf(mgx_mq7_sensor->resistance(), 6, 2, str_resistance);

  unsigned long diff = millis() - mgx_mq7_sensor->heaterStarted();
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}%s %s{m}%d, Rs=%s kOhm{e}"), mqtt_data, label, status, diff / 1000, str_resistance);

  if (Settings.snsMqx.mq7_Ro_value < 0.01) {
    return;
  }
  // Rs(1ppm) = 443 kOhm
  // Ro = ~ 15.653
  // Rs/Ro = 28.3

  char str_ppm1[24];
  dtostrf(mgx_mq7_sensor->ppmAtStart(), 6, 4, str_ppm1);
  char str_ppm2[24];
  dtostrf(mgx_mq7_sensor->ppmAtEnd(), 6, 4, str_ppm2);

  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}%s{m}CO=%s .. %s PPM{e}"), mqtt_data, label, str_ppm1, str_ppm2);
}

#endif  // USE_WEBSERVER

#define MQX_SETTINGS_MAGIC (504)

void snsMqx_CheckSettings (void) {
  if (Settings.snsMqx.hdr_magic == MQX_SETTINGS_MAGIC) {
    return;
  }

  memset(&Settings.snsMqx, 0, sizeof(Settings.snsMqx));
  Settings.snsMqx.hdr_magic = MQX_SETTINGS_MAGIC;
  Settings.snsMqx.mqx_powered = 1;


  MQ2Sensor::setDefaults(&Settings.snsMqx);
  MQ7Sensor::setDefaults(&Settings.snsMqx);
}



void snsMqx_Telemetry_addFloat(const char* key, float value, bool first) {
  char str_val[32];
  dtostrf(value, 0, 2, str_val);
  if (first) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s\"%s\":\"%s\""), mqtt_data, key, str_val);
  } else {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"%s\":\"%s\""), mqtt_data, key, str_val);
  }
}

void snsMqx_Telemetry(void) {
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"MQX\":{"), mqtt_data);
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s\"has_pwctl\": %s"), mqtt_data, mqx_has_power_ctl ? "true" : "false");
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"pwr\":\"%d\""), mqtt_data, Settings.snsMqx.mqx_powered);
  if (!mgx_ads->initialized()) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"error\":\"no ads1115\""), mqtt_data);
  } else {
    TIME_T tm;
    if (mgx_mq2_sensor) {
      // MQ2
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"MQ2\":{"), mqtt_data);
      snsMqx_Telemetry_addFloat("value",  mgx_mq2_sensor->ppmReading(), true);
      snsMqx_Telemetry_addFloat("raw",  mgx_mq2_sensor->resistance(), false);
      snsMqx_Telemetry_addFloat("caf",  Settings.snsMqx.mq2_clean_air_factor, false);
      snsMqx_Telemetry_addFloat("rmax",  Settings.snsMqx.mq2_kohm_max, false);
      snsMqx_Telemetry_addFloat("ro",  Settings.snsMqx.mq2_Ro_value, false);
      BreakTime(Settings.snsMqx.mq2_Ro_date, tm);
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"rot\":\"%d.%d.%d %d:%d\""), mqtt_data, 1970 + tm.year, tm.month, tm.day_of_month, tm.hour, tm.minute);
      snsMqx_Telemetry_addFloat("warnl",  Settings.snsMqx.mq2_warning_level_ppm, false);
      snsMqx_Telemetry_addFloat("alrml",  Settings.snsMqx.mq2_alarm_level_ppm, false);
      snsMqx_Telemetry_addFloat("sml", mgx_mq2_sensor->ppmReadingSmoothed(), false);
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s}"), mqtt_data);
    }

    if (mgx_mq7_sensor) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"MQ7\":{"), mqtt_data);
      snsMqx_Telemetry_addFloat("value",  mgx_mq7_sensor->ppmAtEnd(), true);
      snsMqx_Telemetry_addFloat("delta",  mgx_mq7_sensor->ppmAtEnd() - mgx_mq7_sensor->ppmAtStart(), false);
      snsMqx_Telemetry_addFloat("raw",  mgx_mq7_sensor->resistance(), false);
      snsMqx_Telemetry_addFloat("caf",  Settings.snsMqx.mq7_clean_air_factor, false);
      snsMqx_Telemetry_addFloat("rmax",  Settings.snsMqx.mq7_kohm_max, false);
      snsMqx_Telemetry_addFloat("ro",  Settings.snsMqx.mq7_Ro_value, false);
      BreakTime(Settings.snsMqx.mq7_Ro_date, tm);
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"rot\":\"%d.%d.%d %d:%d\""), mqtt_data, 1970 + tm.year, tm.month, tm.day_of_month, tm.hour, tm.minute);
      snsMqx_Telemetry_addFloat("warnl",  Settings.snsMqx.mq7_warning_level_ppm, false);
      snsMqx_Telemetry_addFloat("alrml",  Settings.snsMqx.mq7_alarm_level_ppm, false);
      snsMqx_Telemetry_addFloat("alrmd",  Settings.snsMqx.mq7_alarm_off_delta, false);
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s}"), mqtt_data);
    }
  }
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s}"), mqtt_data);
}

void snsMqx_check_alarm_for(float value, int gasType, float alarmLevel, float warningLevel) {
  if (isinf(value)) {
    return;
  }

  if (value >= alarmLevel) {
    Siren_SetStatusGas(sirenAlarm, gasType, true);
  } else {
    // Set warning if above warning or clear
    if (value > warningLevel) {
      Siren_SetStatusGas(sirenWarning, gasType, true);
    } else {
      Siren_SetStatusGas(sirenOff, gasType, true);
    }

  }
}

void snsMqx_check_alarm(void) {
  if (mgx_mq2_sensor) {
    if (!isinf(mgx_mq2_sensor->ppmReadingSmoothed())) {
      snsMqx_check_alarm_for(mgx_mq2_sensor->ppmReadingSmoothed(),
        gasFlammable,
        Settings.snsMqx.mq2_alarm_level_ppm,
        Settings.snsMqx.mq2_warning_level_ppm);
    }
  }

  if (mgx_mq7_sensor) {
    if (!isinf(mgx_mq7_sensor->ppmAtEnd())) {
      snsMqx_check_alarm_for(mgx_mq7_sensor->ppmAtEnd(),
        gasCO,
        Settings.snsMqx.mq7_alarm_level_ppm,
        Settings.snsMqx.mq7_warning_level_ppm);
    }
  }
}

bool snsMqx_Command(void)
{
  bool serviced = true;

  switch (XdrvMailbox.payload) {
    case 0:
      snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_SENSOR_INDEX_SVALUE, XSNS_97, "Sensors Off");
      //Settings.snsMqx.mqx_powered
      break;
    case 1:
      snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_SENSOR_INDEX_SVALUE, XSNS_97, "Sensors On");
      break;
    case 100:
      MQ2Sensor::setDefaults(&Settings.snsMqx);
      snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_SENSOR_INDEX_SVALUE, XSNS_97, "MQ2 Reset");
      break;
    case 101:
      MQ7Sensor::setDefaults(&Settings.snsMqx);
      snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_SENSOR_INDEX_SVALUE, XSNS_97, "MQ7 Reset");
      break;
    default:
      snprintf_P(mqtt_data, sizeof(mqtt_data), S_JSON_SENSOR_INDEX_SVALUE, XSNS_97, "100/101 will reset MQ 2/7");
  }

  return serviced;
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns97(uint8_t function)
{
  if (!i2c_flg) {
    return false;
  }

  if (SENSOR_SIREN != Settings.module) {
    return false;
  }

  bool result = false;

  switch (function) {
    case FUNC_INIT:
      mgx_mq7_sensor = NULL;
      mgx_mq2_sensor = NULL;
      mgx_ads = new ADS1115Reader();
      mqx_has_power_ctl = false;
    break;
    case FUNC_PREP_BEFORE_TELEPERIOD:
      if (!mgx_ads->initialized()) {
        if (mgx_ads->detectAddress()) {
          snsMqx_CheckSettings();
          snsMqx_Init();

          if (mgx_mq2_sensor) {
            mgx_mq2_sensor->start();
          }
          if (mgx_mq7_sensor) {
            mgx_mq7_sensor->start();
          }
        }
      }
      break;

    case FUNC_EVERY_250_MSECOND:
      if (!Settings.snsMqx.mqx_powered) {
        return result;
      }
      if (mgx_mq7_sensor) {
        // Do not run MQ7 sensor step if MQ2 is calibrating.
        if (!mgx_mq2_sensor || !mgx_mq2_sensor->isCalibrating()) {
          if (mgx_mq7_sensor->step()) {
            // Exit if reading occur
            return result;
          }
        }
      }
      if (mgx_mq2_sensor) {
        // No not run MQ2 step if MQ7 is reading
        if (!mgx_mq7_sensor || !mgx_mq7_sensor->isReading()) {
          mgx_mq2_sensor->step();
        }
      }
      break;
    case FUNC_JSON_APPEND:
      snsMqx_Telemetry();
      break;
    case FUNC_EVERY_SECOND:
      snsMqx_check_alarm();
      break;
    case FUNC_COMMAND_SENSOR:
      if (XSNS_97 == XdrvMailbox.index) {
        result = snsMqx_Command();
      }
      break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_APPEND:
        snsMqx_Show();
        break;
#endif  // USE_WEBSERVER
  }
  return result;
}

#endif //XSNS_97
