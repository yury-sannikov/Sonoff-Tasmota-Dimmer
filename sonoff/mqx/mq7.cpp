#include "mq7.h"
#include "ads1115.h"
#include "../sonoff.h"
#include "../i18n.h"
#include "../sonoff_template.h"

// Externs
extern uint8_t pin[GPIO_MAX];
void AddLog_P2(uint8_t loglevel, PGM_P formatP, ...);
uint32_t LocalTime(void);

// CO gas slope
float snsMqx_COCurve[3]  =  {2.0, 0, -0.6926};


MQ7Sensor::MQ7Sensor(SNSMQx_Settings* settings, ADS1115Reader& reader):m_reader(reader) {
    m_settings = settings;
    m_flags = MQ7_NONE;
    m_heat_start_mills = 0;
    m_resistance_current = 0.0;
    m_resistance_start = 0.0;
    m_ppm_current = 0.0;
    m_ppm_start = 0.0;
}

void MQ7Sensor::start() {
    pinMode(pin[GPIO_MQ7_HEAT], OUTPUT);
    setHeat(true);
}


void MQ7Sensor::setHeat(bool isHeat) {
  m_flags &= ~(MQ7_HEATING | MQ7_READING);
  m_flags |= isHeat ? MQ7_HEATING : 0;

  m_heat_start_mills = millis();
  digitalWrite(pin[GPIO_MQ7_HEAT], (m_flags & MQ7_HEATING) ? HIGH : LOW);
}

 bool MQ7Sensor::step() {
  unsigned long diff = millis() - m_heat_start_mills;
  bool isReading = (m_flags & MQ7_READING);
  if (m_flags & MQ7_HEATING) {
    if (diff > 60000) {
      setHeat(false);
    }
  } else {
    if (!isReading && diff > 80000) {
      m_flags |= MQ7_READING;
      m_resistance_current = 0;
    }
    if (diff > 90000) {
      setHeat(false);
      // Recalibrate MQ7
      calibrate();
      // Update PPM Readings
      updatePPM();
    }
  }
  if (isReading) {
    updateResistance();
    return true;
  }
  return false;
}

void MQ7Sensor::calibrate() {
  if (isinf(m_settings->mq7_Ro_value)) {
    m_settings->mq7_Ro_value = 0.0;
    m_settings->mq7_Ro_date = 0;
  }

  if (m_settings->mq7_kohm_max > m_resistance_current) {
    // If Ro value was not calculated, print warning
    if (m_settings->mq7_Ro_value < 0.1) {
      AddLog_P2(LOG_LEVEL_INFO, PSTR("Unable to calibrate MQ7. Too high CO PPM concentration"));
    }
    return;
  }
  // Recalibrate every 12 hours
  if ((LocalTime() - m_settings->mq7_Ro_date) < 43200) {
    return;
  }
  m_settings->mq7_kohm_max = m_resistance_current;
  m_settings->mq7_Ro_value = m_resistance_current / m_settings->mq7_clean_air_factor;
  m_settings->mq7_Ro_date = LocalTime();

  char str_ro[24];
  dtostrf(m_settings->mq7_Ro_value, 6, 2, str_ro);
  char str_kohm[24];
  dtostrf(m_settings->mq7_kohm_max, 6, 2, str_kohm);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("MQX: New MQ-7 Ro=%s kOhm derived from Rs=%s kOhm"), str_ro, str_kohm);
}

void MQ7Sensor::updateResistance(void) {
  float mq7Resistance = m_reader.calculateResistance(MQ7_CHANNEL);

  if (m_resistance_current < 0.01) {
    m_resistance_current = mq7Resistance;
    m_resistance_start = mq7Resistance;
  } else {
    m_resistance_current *= 0.9;
    m_resistance_current += 0.1 * mq7Resistance;
  }
}

void MQ7Sensor::setDefaults(SNSMQx_Settings* settings) {
  // Ro/Rs clean air factors
  settings->mq7_clean_air_factor = 28.3;

  // Set sensor default max kOhm which will prevent recalibration in high PPM envronment
  settings->mq7_kohm_max = 90.0;

  // Calculated Ro resistance based on read max
  settings->mq7_Ro_value = 0;

  // Datest of the last calculated value. Used to deprecate Ro value and generate new one from sensor reading min.
  settings->mq7_Ro_date = 0;

  // MQ7 Alarm
  settings->mq7_warning_level_ppm = 15.0;
  settings->mq7_alarm_level_ppm = 50.0;
  // Since MQ7 goes low pretty slow, a decreasing delta considered as
  // removed alarm state. If it stops going down, alarm should be raised again
  settings->mq7_alarm_off_delta = 0.1;
}

void MQ7Sensor::updatePPM() {
    m_ppm_current = snsMqx_GetPercentage(m_resistance_current /  m_settings->mq7_Ro_value, snsMqx_COCurve);
    m_ppm_start = snsMqx_GetPercentage(m_resistance_start /  m_settings->mq7_Ro_value, snsMqx_COCurve);
}

