#ifndef XSNS_97
#define XSNS_97            97

enum MQX_FLAGS {
  MQXF_HAS_MQ7         = 0x0,
  MQXF_HAS_MQ2         = 0x1,
  MQXF_HAS_PWR_CTL     = 0x2,
  MQXF_MQ7_HEATING     = 0x4,
  MQXF_MQ7_READING     = 0x8,
  MQXF_MQ2_CALIBRATING = 0x10,
};
// MQX_FLAGS flags
uint16_t sns_mqx_flags = 0;

// CH4 gas slope
float snsMqx_CH4Curve[3]  =  {2.3, 0.484, -0.3762};

// CO gas slope
float snsMqx_COCurve[3]  =  {2.0, 0, -0.6926};

#define RL_VALUE                            (10)   // Sensors load resistance kOhms

#define MQX_ADS1115_REG_CONFIG_MODE_CONTIN  (0x0000)  // Continuous conversion mode
#define MQX_ADS1115_REG_POINTER_CONVERT     (0x00)
#define MQX_ADS1115_REG_POINTER_CONFIG      (0x01)

#define MQX_ADS1115_REG_CONFIG_CQUE_NONE    (0x0003)  // Disable the comparator and put ALERT/RDY in high state (default)
#define MQX_ADS1115_REG_CONFIG_CLAT_NONLAT  (0x0000)  // Non-latching comparator (default)
#define MQX_ADS1115_REG_CONFIG_PGA_4_096V   (0x0200)  // +/-4.096V range = Gain 1
#define MQX_ADS1115_REG_CONFIG_CPOL_ACTVLOW (0x0000)  // ALERT/RDY pin is low when active (default)
#define MQX_ADS1115_REG_CONFIG_CMODE_TRAD   (0x0000)  // Traditional comparator with hysteresis (default)
#define MQX_ADS1115_REG_CONFIG_DR_6000SPS   (0x00E0)  // 6000 samples per second
#define MQX_ADS1115_REG_CONFIG_MUX_SINGLE_0 (0x4000)  // Single-ended AIN0

#define MQX_ADS1115_REG_CONFIG_MODE_SINGLE  (0x0100)  // Power-down single-shot mode (default)
#define MQX_ADS1115_CONVERSIONDELAY         (8)       // CONVERSION DELAY (in mS)

#define MQX_ADS1115_MV_4P096                (0.125000)

// MQ7 heat on time in msec
unsigned long sns_mq7_heat_start = 0;

// ADS1115 I2C address
uint8_t snsMqx_ads1115_address = 0;
// ADS1115 I2C addresses map
uint8_t snsMqx_ads1115_addresses[] = { 0x48, 0x49, 0x4A, 0x4B };

#define MQX_CALIBRATION_250MS_LOOPS 100
uint8_t sns_mq2_calibration_loop = MQX_CALIBRATION_250MS_LOOPS;

float sns_mqx_mq7_read_kOhm = 0;
float sns_mqx_mq7_read_kOhm_start = 0;
float sns_mqx_mq7_ppm_reading = 0;
float sns_mqx_mq7_ppm_reading_start = 0;

void snsMqx_Init(void) {
  sns_mqx_flags = MQXF_HAS_MQ2;

  // MQ-7 require separate heat contol pin. If no such pin, no MQ-7 installed
  if (pin[GPIO_MQ7_HEAT] < 99) {
    sns_mqx_flags |= MQXF_HAS_MQ7;
    // Turn on heat mode
    pinMode(pin[GPIO_MQ7_HEAT], OUTPUT);
    snsMqx_MQ7Heat(true);
  }

  // If GPIO_MQ_POWER is set, enable power control
  if (pin[GPIO_MQ_POWER] < 99) {
    sns_mqx_flags |= MQXF_HAS_PWR_CTL;
    // If set, turn power on/off according to the settings
    pinMode(pin[GPIO_MQ_POWER], OUTPUT);
    digitalWrite(pin[GPIO_MQ_POWER], Settings.snsMqx.mqx_powered);
  }
}

void snsMqx_MQ7Heat(bool isHeat) {

  sns_mqx_flags &= ~(MQXF_MQ7_HEATING | MQXF_MQ7_READING);
  sns_mqx_flags |= isHeat ? MQXF_MQ7_HEATING : 0;

  sns_mq7_heat_start = millis();
  digitalWrite(pin[GPIO_MQ7_HEAT], (sns_mqx_flags & MQXF_MQ7_HEATING) ? HIGH : LOW);
}

void snsMqx_MQ7Heat_step(void) {
  unsigned long diff = millis() - sns_mq7_heat_start;
  bool isReading = sns_mqx_flags & MQXF_MQ7_READING;
  if (sns_mqx_flags & MQXF_MQ7_HEATING) {
    if (diff > 60000) {
      snsMqx_MQ7Heat(false);
    }
  } else {
    if (!isReading && diff > 80000) {
      sns_mqx_flags |= MQXF_MQ7_READING;
      sns_mqx_mq7_read_kOhm = 0;
    }
    if (diff > 90000) {
      snsMqx_MQ7Heat(true);
      // Recalibrate MQ7
      snsMqx_MQ7_calibrate();
    }
  }
  if (isReading) {
    snsMqx_MQ7_read();
  }
}
// Read last sensor value to calculate Ro
void snsMqx_MQ7_calibrate(void) {
  if (Settings.snsMqx.mq7_kohm_max > sns_mqx_mq7_read_kOhm) {
    // If Ro value was not calculated, print warning
    if (Settings.snsMqx.mq7_Ro_value < 0.1) {
      AddLog_P2(LOG_LEVEL_INFO, PSTR("Unable to calibrate MQ7. Too high CO PPM concentration"));
    }
    return;
  }
  Settings.snsMqx.mq7_kohm_max = sns_mqx_mq7_read_kOhm;
  Settings.snsMqx.mq7_Ro_value = sns_mqx_mq7_read_kOhm / Settings.snsMqx.mq7_clean_air_factor;
  Settings.snsMqx.mq7_Ro_date = LocalTime();

  char str_ro[24];
  dtostrf(Settings.snsMqx.mq7_Ro_value, 6, 2, str_ro);
  char str_kohm[24];
  dtostrf(Settings.snsMqx.mq7_kohm_max, 6, 2, str_kohm);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("MQX: New MQ-7 Ro=%s kOhm derived from Rs=%s kOhm"), str_ro, str_kohm);
}

void snsMqx_MQ7_read(void) {
  int mq7_raw = snsMqx_Ads1115GetConversion(1);
  float mq7Resistance = snsMqx_ResistanceCalculation((float)mq7_raw);
  if (sns_mqx_mq7_read_kOhm < 0.01) {
    sns_mqx_mq7_read_kOhm = mq7Resistance;
    sns_mqx_mq7_read_kOhm_start = mq7Resistance;
  } else {
    sns_mqx_mq7_read_kOhm *= 0.9;
    sns_mqx_mq7_read_kOhm += 0.1 * mq7Resistance;
  }
}

void snsMqx_Ads1115Detect(void)
{
  if (snsMqx_ads1115_address) {
    return;
  }

  uint16_t buffer;
  for (uint8_t i = 0; i < sizeof(snsMqx_ads1115_addresses); i++) {
    uint8_t address = snsMqx_ads1115_addresses[i];
    if (I2cValidRead16(&buffer, address, MQX_ADS1115_REG_POINTER_CONVERT) &&
        I2cValidRead16(&buffer, address, MQX_ADS1115_REG_POINTER_CONFIG)) {
      snsMqx_ads1115_address = address;
      snsMqx_Ads1115StartComparator(i, MQX_ADS1115_REG_CONFIG_MODE_CONTIN);
      AddLog_P2(LOG_LEVEL_DEBUG, S_LOG_I2C_FOUND_AT, "MQX: ADS1115", snsMqx_ads1115_address);
    }
  }
  // Start sensor calibration after
  if (snsMqx_ads1115_address) {
    snsMqx_CalibrateMQ2();
  }
}

void snsMqx_Ads1115StartComparator(uint8_t channel, uint16_t mode)
{
  // Start with default values
  uint16_t config = mode |
                    MQX_ADS1115_REG_CONFIG_CQUE_NONE    | // Comparator enabled and asserts on 1 match
                    MQX_ADS1115_REG_CONFIG_CLAT_NONLAT  | // Non Latching mode
                    MQX_ADS1115_REG_CONFIG_PGA_4_096V   | // ADC Input voltage range (Gain)
                    MQX_ADS1115_REG_CONFIG_CPOL_ACTVLOW | // Alert/Rdy active low   (default val)
                    MQX_ADS1115_REG_CONFIG_CMODE_TRAD   | // Traditional comparator (default val)
                    MQX_ADS1115_REG_CONFIG_DR_6000SPS;    // 6000 samples per second

  // Set single-ended input channel
  config |= (MQX_ADS1115_REG_CONFIG_MUX_SINGLE_0 + (0x1000 * channel));

  // Write config register to the ADC
  I2cWrite16(snsMqx_ads1115_address, MQX_ADS1115_REG_POINTER_CONFIG, config);
}

int16_t snsMqx_Ads1115GetConversion(uint8_t channel)
{
  snsMqx_Ads1115StartComparator(channel, MQX_ADS1115_REG_CONFIG_MODE_SINGLE);
  // Wait for the conversion to complete
  delay(MQX_ADS1115_CONVERSIONDELAY);
  // Read the conversion results
  I2cRead16(snsMqx_ads1115_address, MQX_ADS1115_REG_POINTER_CONVERT);

  snsMqx_Ads1115StartComparator(channel, MQX_ADS1115_REG_CONFIG_MODE_CONTIN);
  delay(MQX_ADS1115_CONVERSIONDELAY);
  // Read the conversion results
  uint16_t res = I2cRead16(snsMqx_ads1115_address, MQX_ADS1115_REG_POINTER_CONVERT);
  return (int16_t)res;
}

float snsMqx_ResistanceCalculation(float raw_adc)
{
  float maxADC = snsMqx_Ads1115GetConversion(2);
  if (raw_adc < 1) {
    raw_adc = 1;
  }
  return (((float)RL_VALUE * (maxADC - raw_adc) / raw_adc));
}

float snsMqx_GetPercentage(float rs_ro_ratio, float *curve)
{
  //Using slope,ratio(y2) and another point(x1,y1) on line we will find
  // gas concentration(x2) using x2 = [((y2-y1)/slope)+x1]
  // as in curves are on logarithmic coordinate, power of 10 is taken to convert result to non-logarithmic.
  float exp_val = (((log(rs_ro_ratio) / 2.302585) - curve[1]) / curve[2]) + curve[0];
  return pow(10, exp_val);
}

#ifdef USE_WEBSERVER

const char HTTP_MQX_ANALOG[] PROGMEM = "%s{s}%s %s{m}%d (%s kOhm) %s PPM{e}";                               // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>

void snsMqx_Show(void) {

  if (!snsMqx_ads1115_address) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}Detecting 1115...{m}{e}"), mqtt_data);
    return;
  }

  snsMqx_ShowMQ2();
  if (sns_mqx_flags & MQXF_HAS_MQ7) {
    snsMqx_ShowMQ7();
  }
}

void getMQ2PPM(float* result) {
  int mq2_raw = snsMqx_Ads1115GetConversion(0);
  result[0] = snsMqx_ResistanceCalculation((float)mq2_raw);
  result[1] = snsMqx_GetPercentage(result[0] / Settings.snsMqx.mq2_Ro_value, snsMqx_CH4Curve);
}

void snsMqx_ShowMQ2(void) {
  bool isCalibrating = sns_mqx_flags & MQXF_MQ2_CALIBRATING;
  if (isCalibrating || Settings.snsMqx.mq2_Ro_value < .1) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}Calibrating MQ-2...{m}%d{e}"), mqtt_data, sns_mq2_calibration_loop);
    return;
  }
  if (Settings.snsMqx.mq2_Ro_value < 0.01) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}MQ-2 failed to calculate Ro{m}{e}"), mqtt_data);
    snsMqx_CalibrateMQ2();
    return;
  }

  float results[2];
  getMQ2PPM((float*)&results);

  // If MQ-2 resistance is higher than previous mq2_kohm_max after 2 minutes, recalibrate
  if (results[0] > Settings.snsMqx.mq2_kohm_max && (LocalTime() - Settings.snsMqx.mq2_Ro_date) > 120) {
    snsMqx_CalibrateMQ2();
  }

  char label[15];
  snprintf_P(label, sizeof(label), "MQ-2 (%02x)", snsMqx_ads1115_address);

  char str_res[24];
  dtostrf(results[0], 6, 2, str_res);
  char str_ppm[24];
  dtostrf(results[1], 6, 4, str_ppm);
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, "", 0, str_res, str_ppm);
}

void snsMqx_ShowMQ7(void) {
  char label[15];
  snprintf_P(label, sizeof(label), "MQ-7 (%02x)", snsMqx_ads1115_address);

  char status[32];
  snprintf_P(status, sizeof(status), "%s %s",
    sns_mqx_flags & MQXF_MQ7_HEATING ? "Heat" : "Cool",
    sns_mqx_flags & MQXF_MQ7_READING ? "(R)" : ""
  );

  char str_resistance[24];
  dtostrf(sns_mqx_mq7_read_kOhm, 6, 2, str_resistance);

  char str_resistance_st[24];
  dtostrf(sns_mqx_mq7_read_kOhm_start, 6, 2, str_resistance_st);

  char str_res[64];
  snprintf_P(str_res, sizeof(str_res), "[%s %s]", str_resistance_st, str_resistance);

  unsigned long diff = millis() - sns_mq7_heat_start;
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, status, diff / 1000, str_res, "-");

  if (Settings.snsMqx.mq7_Ro_value < 0.01) {
    return;
  }
  // Rs(1ppm) = 443 kOhm
  // Ro = ~ 15.653
  // Rs/Ro = 28.3
  sns_mqx_mq7_ppm_reading_start = snsMqx_GetPercentage(sns_mqx_mq7_read_kOhm_start / Settings.snsMqx.mq7_Ro_value, snsMqx_COCurve);
  sns_mqx_mq7_ppm_reading = snsMqx_GetPercentage(sns_mqx_mq7_read_kOhm / Settings.snsMqx.mq7_Ro_value, snsMqx_COCurve);

  char str_ppm1[24];
  dtostrf(sns_mqx_mq7_ppm_reading_start, 6, 4, str_ppm1);
  char str_ppm2[24];
  dtostrf(sns_mqx_mq7_ppm_reading, 6, 4, str_ppm2);
  snprintf_P(str_res, sizeof(str_res), "%s .. %s", str_ppm1, str_ppm2);
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, "PPM", 0, "-", str_res);

}

#endif  // USE_WEBSERVER

// Enable MQ-2 calibration
void snsMqx_CalibrateMQ2(void) {
  sns_mqx_flags |= MQXF_MQ2_CALIBRATING;
  sns_mq2_calibration_loop = MQX_CALIBRATION_250MS_LOOPS;
  AddLog_P(LOG_LEVEL_DEBUG, PSTR("MQX: Start MQ-2 calibration"));
}

float sns_mq2_Resistance = 0;
void snsMqx_CalibrateMQ2_step(void) {
  if (sns_mqx_flags & MQXF_MQ2_CALIBRATING == 0) {
    return;
  }

  float mq2Resistance = snsMqx_ResistanceCalculation((float)snsMqx_Ads1115GetConversion(0));
  if (mq2Resistance <= 0) {
    return;
  }
  if (sns_mq2_Resistance == 0) {
    sns_mq2_Resistance = mq2Resistance;
  } else {
    sns_mq2_Resistance *= 0.9;                  // applying exponential smoothing, A = 0.1
    sns_mq2_Resistance += 0.1 * mq2Resistance;
  }

  if (--sns_mq2_calibration_loop > 0) {
    return;
  }
  sns_mqx_flags &= ~MQXF_MQ2_CALIBRATING;

  char str_tmp[32];
  dtostrf(sns_mq2_Resistance, 6, 2, str_tmp);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("MQX: End MQ-2 calibration. R(Avg): %s"), str_tmp);

  if (Settings.snsMqx.mq2_kohm_max > 1000) {
    AddLog_P2(LOG_LEVEL_INFO, PSTR("MQX: MQ-2 has invalid rmax. Resetting"));
    snsMqx_ResetMQ2_settings();
  }

  if (Settings.snsMqx.mq2_kohm_max > sns_mq2_Resistance) {
    if (Settings.snsMqx.mq2_Ro_value < 0.01) {
      AddLog_P2(LOG_LEVEL_INFO, PSTR("MQX: Unable to calibrate MQ-2: High PPM value"));
    }
    return;
  }

  Settings.snsMqx.mq2_kohm_max = sns_mq2_Resistance;
  Settings.snsMqx.mq2_Ro_value = sns_mq2_Resistance / Settings.snsMqx.mq2_clean_air_factor;
  Settings.snsMqx.mq2_Ro_date = LocalTime();

  dtostrf(Settings.snsMqx.mq2_Ro_value, 6, 2, str_tmp);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("MQX: New MQ-2 Ro=%s"), str_tmp);
}

#define MQX_SETTINGS_MAGIC (422)

void snsMqx_CheckSettings (void) {
  if (Settings.snsMqx.hdr_magic == MQX_SETTINGS_MAGIC) {
    return;
  }

  memset(&Settings.snsMqx, 0, sizeof(Settings.snsMqx));
  Settings.snsMqx.hdr_magic = MQX_SETTINGS_MAGIC;
  Settings.snsMqx.mqx_powered = 1;

  snsMqx_ResetMQ2_settings();
  snsMqx_ResetMQ7_settings();
}

void snsMqx_ResetMQ2_settings (void) {
  // Ro/Rs clean air factors
  Settings.snsMqx.mq2_clean_air_factor = 9.83;
  // Set sensor default max kOhm which will prevent recalibration in high PPM envronment
  Settings.snsMqx.mq2_kohm_max = 50.0;
  // Calculated Ro resistance based on read max
  Settings.snsMqx.mq2_Ro_value = 0;
  // Datest of the last calculated value. Used to deprecate Ro value and generate new one from sensor reading min.
  Settings.snsMqx.mq2_Ro_date = 0;
  // MQ2 Alarm
  Settings.snsMqx.mq2_warning_level_ppm = 15.0;
  Settings.snsMqx.mq2_alarm_level_ppm   = 30.0;
}

void snsMqx_ResetMQ7_settings (void) {
  // Ro/Rs clean air factors
  Settings.snsMqx.mq2_clean_air_factor = 9.83;

  // Set sensor default max kOhm which will prevent recalibration in high PPM envronment
  Settings.snsMqx.mq2_kohm_max = 50.0;

  // Calculated Ro resistance based on read max
  Settings.snsMqx.mq2_Ro_value = 0;

  // Datest of the last calculated value. Used to deprecate Ro value and generate new one from sensor reading min.
  Settings.snsMqx.mq7_Ro_date = 0;

  // MQ7 Alarm
  Settings.snsMqx.mq7_warning_level_ppm = 9.0;
  Settings.snsMqx.mq7_alarm_level_ppm = 40.0;
  // Since MQ7 goes low pretty slow, a decreasing delta considered as
  // removed alarm state. If it stops going down, alarm should be raised again
  Settings.snsMqx.mq7_alarm_off_delta = 0.1;
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
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s\"flags\":\"0x%x\""), mqtt_data, sns_mqx_flags);
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"pwr\":\"%d\""), mqtt_data, Settings.snsMqx.mqx_powered);
  if (snsMqx_ads1115_address == 0) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"error\":\"no ads1115\""), mqtt_data);
  } else {
    float result[2] = {0.0, 0.0};

    if (Settings.snsMqx.mq2_Ro_value > 0.01) {
      getMQ2PPM((float*)&result);
    }

    TIME_T tm;
    // MQ2
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"MQ2\":{"), mqtt_data);
    snsMqx_Telemetry_addFloat("value",  result[1], true);
    snsMqx_Telemetry_addFloat("raw",  result[0], false);
    snsMqx_Telemetry_addFloat("caf",  Settings.snsMqx.mq2_clean_air_factor, false);
    snsMqx_Telemetry_addFloat("rmax",  Settings.snsMqx.mq2_kohm_max, false);
    snsMqx_Telemetry_addFloat("ro",  Settings.snsMqx.mq2_Ro_value, false);
    BreakTime(Settings.snsMqx.mq2_Ro_date, tm);
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"rot\":\"%d.%d.%d %d:%d\""), mqtt_data, 1970 + tm.year, tm.month, tm.day_of_month, tm.hour, tm.minute);
    snsMqx_Telemetry_addFloat("warnl",  Settings.snsMqx.mq2_warning_level_ppm, false);
    snsMqx_Telemetry_addFloat("alrml",  Settings.snsMqx.mq2_alarm_level_ppm, false);
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s}"), mqtt_data);

    if (sns_mqx_flags & MQXF_HAS_MQ7) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"MQ7\":{"), mqtt_data);
      snsMqx_Telemetry_addFloat("value",  sns_mqx_mq7_ppm_reading, true);
      snsMqx_Telemetry_addFloat("delta",  sns_mqx_mq7_ppm_reading - sns_mqx_mq7_ppm_reading_start, false);
      snsMqx_Telemetry_addFloat("raw",  sns_mqx_mq7_read_kOhm, false);
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

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns97(uint8_t function)
{
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      snsMqx_CheckSettings();
      snsMqx_Init();
      break;
    case FUNC_PREP_BEFORE_TELEPERIOD:
      snsMqx_Ads1115Detect();
      break;
    case FUNC_EVERY_50_MSECOND:
      snsMqx_MQ7Heat_step();
      break;
    case FUNC_EVERY_250_MSECOND:
      if (sns_mqx_flags & MQXF_MQ2_CALIBRATING) {
        snsMqx_CalibrateMQ2_step();
      }
      break;
    case FUNC_JSON_APPEND:
      snsMqx_Telemetry();
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
