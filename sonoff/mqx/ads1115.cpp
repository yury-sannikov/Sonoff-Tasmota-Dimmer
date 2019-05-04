#include "ads1115.h"
#include "../sonoff.h"
#include "../i18n.h"

// Forward declarations
bool I2cWrite16(uint8_t addr, uint8_t reg, uint16_t val);
bool I2cValidRead16(uint16_t *data, uint8_t addr, uint8_t reg);
void AddLog_P2(uint8_t loglevel, PGM_P formatP, ...);
uint16_t I2cRead16(uint8_t addr, uint8_t reg);

// ADS1115 I2C addresses map
uint8_t snsMqx_ads1115_addresses[4] = { 0x48, 0x49, 0x4A, 0x4B };

ADS1115Reader::ADS1115Reader() {
  m_address = 0;
  m_referenceVoltage = 0;
}

uint8_t ADS1115Reader::detectAddress() {
  if (m_address) {
    return m_address;
  }

  uint16_t buffer;
  for (uint8_t i = 0; i < sizeof(snsMqx_ads1115_addresses); i++) {
    uint8_t address = snsMqx_ads1115_addresses[i];
    if (I2cValidRead16(&buffer, address, MQX_ADS1115_REG_POINTER_CONVERT) &&
        I2cValidRead16(&buffer, address, MQX_ADS1115_REG_POINTER_CONFIG)) {
      m_address = address;
      startComparator(i, MQX_ADS1115_REG_CONFIG_MODE_CONTIN);
      AddLog_P2(LOG_LEVEL_DEBUG, S_LOG_I2C_FOUND_AT, "MQX: ADS1115", address);
    }
  }

  return m_address;
}

void ADS1115Reader::startComparator(uint8_t channel, uint16_t mode) {
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
  I2cWrite16(m_address, MQX_ADS1115_REG_POINTER_CONFIG, config);
}


int16_t ADS1115Reader::getConversion(uint8_t channel) {
  startComparator(channel, MQX_ADS1115_REG_CONFIG_MODE_SINGLE);
  // Wait for the conversion to complete
  delay(MQX_ADS1115_CONVERSIONDELAY);
  // Read the conversion results
  I2cRead16(m_address, MQX_ADS1115_REG_POINTER_CONVERT);

  startComparator(channel, MQX_ADS1115_REG_CONFIG_MODE_CONTIN);
  delay(MQX_ADS1115_CONVERSIONDELAY);
  // Read the conversion results
  uint16_t res = I2cRead16(m_address, MQX_ADS1115_REG_POINTER_CONVERT);
  return (int16_t)res;
}

float ADS1115Reader::calculateResistance(int channel)
{
  int16_t raw_adc = getConversion(channel);

  if (raw_adc < 1) {
    return 0;
  }

  if (m_referenceVoltage == 0) {
    m_referenceVoltage = getConversion(VREF_CHANNEL);
  }

  return (((float)RL_VALUE * ((float)m_referenceVoltage - (float)raw_adc) / (float)raw_adc));
}


float snsMqx_GetPercentage(float rs_ro_ratio, float *curve)
{
  if (rs_ro_ratio < 0.01) {
    return 0.0;
  }
  //Using slope,ratio(y2) and another point(x1,y1) on line we will find
  // gas concentration(x2) using x2 = [((y2-y1)/slope)+x1]
  // as in curves are on logarithmic coordinate, power of 10 is taken to convert result to non-logarithmic.
  float exp_val = (((log(rs_ro_ratio) / 2.302585) - curve[1]) / curve[2]) + curve[0];
  return pow(10, exp_val);
}