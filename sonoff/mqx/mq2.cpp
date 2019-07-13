#include "mq2.h"
#include "../sonoff.h"
#include "../i18n.h"

// Forwards
void AddLog_P(uint8_t loglevel, const char *formatP);
void AddLog_P2(uint8_t loglevel, PGM_P formatP, ...);
uint32_t LocalTime(void);

// CH4 gas slope
float snsMqx_CH4Curve[3]  =  {2.3, 0.484, -0.3762};

MQ2Sensor::MQ2Sensor(SNSMQx_Settings* settings, ADS1115Reader& reader):m_reader(reader) {
    m_settings = settings;
    m_flags = MQ2_NONE;
    m_resistance = 0.0;
    m_ppm = 0.0;
    m_calibration_loop = MQ2_CALIBRATION_250MS_LOOPS;
    m_ppm_loop = MQ2_CALCULATE_PPM_250MS_LOOPS;
}

// Start MQ2 sensor
void MQ2Sensor::start() {
    if (m_settings->mq2_Ro_value < 0.01 || isinf(m_settings->mq2_Ro_value)) {
        calibrate();
    }
}

// A step function to maintain calibration. Return true if reading occur
bool MQ2Sensor::step() {
  if (!isCalibrating()) {
    if (m_ppm_loop-- == 0) {
        m_ppm_loop = MQ2_CALCULATE_PPM_250MS_LOOPS;

        // Start recalibration if no valid Ro value
        if (m_settings->mq2_Ro_value < 0.01 || isinf(m_settings->mq2_Ro_value)) {
            calibrate();
            return false;
        }

        updatePPM();
        return true;
    }
    return false;
  }

  float mq2Resistance = m_reader.calculateResistance(MQ2_CHANNEL);
  if (mq2Resistance <= 0) {
    return true;
  }

  if (m_resistance == 0) {
    m_resistance = mq2Resistance;
  } else {
    m_resistance *= 0.9;                  // applying exponential smoothing, A = 0.1
    m_resistance += 0.1 * mq2Resistance;
  }

  if (m_calibration_loop-- > 0) {
    return true;
  }
  m_flags &= ~MQ2_CALIBRATING;

  char str_tmp[32];
  dtostrf(m_resistance, 6, 2, str_tmp);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("MQX: End MQ-2 calibration. R(Avg): %s"), str_tmp);

  if (m_settings->mq2_kohm_max > 1000) {
    AddLog_P2(LOG_LEVEL_INFO, PSTR("MQX: MQ-2 has invalid rmax. Resetting"));
    setDefaults(m_settings);
  }

  if (m_settings->mq2_kohm_max > m_resistance) {
    if (m_settings->mq2_Ro_value < 0.01) {
      AddLog_P2(LOG_LEVEL_INFO, PSTR("MQX: Unable to calibrate MQ-2: High PPM value"));
    }
    return true;
  }

  m_settings->mq2_kohm_max = m_resistance;
  m_settings->mq2_Ro_value = m_resistance / m_settings->mq2_clean_air_factor;
  m_settings->mq2_Ro_date = LocalTime();

  dtostrf(m_settings->mq2_Ro_value, 6, 2, str_tmp);
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("MQX: New MQ-2 Ro=%s"), str_tmp);
  return true;
}

void MQ2Sensor::updatePPM() {
  m_resistance = m_reader.calculateResistance(MQ2_CHANNEL);
  m_ppm = snsMqx_GetPercentage((float)m_resistance / (float)m_settings->mq2_Ro_value, snsMqx_CH4Curve);

  // If MQ-2 resistance is higher than previous mq2_kohm_max after 12 hours, recalibrate
  if (m_resistance > m_settings->mq2_kohm_max && (LocalTime() - m_settings->mq2_Ro_date) > 43200) {
    calibrate();
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("MQX: Calibrate due to max & time"));
  }
  m_ppm_smoothed = 0.99 * m_ppm_smoothed + 0.01 * m_ppm;

  // Allow m_ppm_smoothed to go down pretty quick
  if (m_ppm_smoothed > m_ppm) {
    m_ppm_smoothed = m_ppm;
  }
}

// Set default settings
void MQ2Sensor::setDefaults(SNSMQx_Settings* settings) {
  // Ro/Rs clean air factors
  settings->mq2_clean_air_factor = 9.83;
  // Set sensor default max kOhm which will prevent recalibration in high PPM envronment
  settings->mq2_kohm_max = 50.0;
  // Calculated Ro resistance based on read max
  settings->mq2_Ro_value = 0;
  // Datest of the last calculated value. Used to deprecate Ro value and generate new one from sensor reading min.
  settings->mq2_Ro_date = 0;
  // MQ2 Alarm
  settings->mq2_warning_level_ppm = 60.0;
  settings->mq2_alarm_level_ppm   = 100.0;
}

void MQ2Sensor::calibrate() {
  if (m_flags & MQ2_CALIBRATING) {
    return;
  }
  m_flags |= MQ2_CALIBRATING;
  m_calibration_loop = MQ2_CALIBRATION_250MS_LOOPS;
  AddLog_P(LOG_LEVEL_DEBUG, PSTR("MQX: Start MQ-2 calibration"));
}
