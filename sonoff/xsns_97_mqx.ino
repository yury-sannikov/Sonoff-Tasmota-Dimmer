#ifndef XSNS_97
#define XSNS_97            97

uint8_t sns_mqx_has_mq7 = 1;
uint8_t sns_mqx_has_mq2 = 1;
uint8_t sns_mqx_has_pwr_control = 1;
uint8_t sns_mqx_is_mq7_heating = 0;

void snsMqx_Init(void) {
  // Assume we always have MQ2 (settings?)
  sns_mqx_has_mq2 = 1;
  sns_mqx_has_mq7 = 0;
  sns_mqx_has_pwr_control = 0;
  sns_mqx_is_mq7_heating = 0;

  // MQ-7 require separate heat contol pin. If no such pin, no MQ-7 installed
  if (pin[GPIO_MQ7_HEAT] < 99) {
    sns_mqx_has_mq7 = 1;
    sns_mqx_is_mq7_heating = 1;
    // Turn on heat mode
    pinMode(pin[GPIO_MQ7_HEAT], OUTPUT);
    digitalWrite(pin[GPIO_MQ7_HEAT], 1);
  }

  // If GPIO_MQ_POWER is set, enable power control
  if (pin[GPIO_MQ_POWER] < 99) {
    sns_mqx_has_pwr_control = 1;
    // If set, turn power on by default
    pinMode(pin[GPIO_MQ_POWER], OUTPUT);
    digitalWrite(pin[GPIO_MQ_POWER], 1);
  }
}

uint8_t snsMqx_ads1115_address = 0;
uint8_t snsMqx_ads1115_addresses[] = { 0x48, 0x49, 0x4A, 0x4B };

#define MQX_ADS1115_REG_CONFIG_MODE_CONTIN  (0x0000)  // Continuous conversion mode
#define MQX_ADS1115_REG_POINTER_CONVERT     (0x00)
#define MQX_ADS1115_REG_POINTER_CONFIG      (0x01)

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
      AddLog_P2(LOG_LEVEL_DEBUG, S_LOG_I2C_FOUND_AT, "MQx ADS1115", snsMqx_ads1115_address);
    }
  }
}

#define MQX_ADS1115_REG_CONFIG_CQUE_NONE    (0x0003)  // Disable the comparator and put ALERT/RDY in high state (default)
#define MQX_ADS1115_REG_CONFIG_CLAT_NONLAT  (0x0000)  // Non-latching comparator (default)
#define MQX_ADS1115_REG_CONFIG_PGA_4_096V   (0x0200)  // +/-4.096V range = Gain 1
#define MQX_ADS1115_REG_CONFIG_CPOL_ACTVLOW (0x0000)  // ALERT/RDY pin is low when active (default)
#define MQX_ADS1115_REG_CONFIG_CMODE_TRAD   (0x0000)  // Traditional comparator with hysteresis (default)
#define MQX_ADS1115_REG_CONFIG_DR_6000SPS   (0x00E0)  // 6000 samples per second
#define MQX_ADS1115_REG_CONFIG_MUX_SINGLE_0 (0x4000)  // Single-ended AIN0

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

#define MQX_ADS1115_REG_CONFIG_MODE_SINGLE  (0x0100)  // Power-down single-shot mode (default)
#define MQX_ADS1115_CONVERSIONDELAY         (8)       // CONVERSION DELAY (in mS)

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

#ifdef USE_WEBSERVER

const char HTTP_MQX_ANALOG[] PROGMEM = "%s{s}%s %s{m}%d (%s mV){e}";                               // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>
//const char HTTP_SNS_ANALOG2[] PROGMEM = "%s{s}%s " D_ANALOG_INPUT "%d{m}%d (%s mV){e}";                               // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>

#define MQX_ADS1115_MV_4P096            0.125000
void snsMqx_Show(void) {
  char label[15];
  snprintf_P(label, sizeof(label), "ADS1115(%02x)", snsMqx_ads1115_address);
  char str_volt[24];

  int16_t mq2 = snsMqx_Ads1115GetConversion(0);
  int16_t mq7 = snsMqx_Ads1115GetConversion(1);
  int16_t vref = snsMqx_Ads1115GetConversion(2);

  dtostrf((float)mq2 * MQX_ADS1115_MV_4P096, 5, 1, str_volt);
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, "MQ-2", mq2, str_volt);

  dtostrf((float)mq7 * MQX_ADS1115_MV_4P096, 5, 1, str_volt);
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, "MQ-7", mq7, str_volt);

  dtostrf((float)vref * MQX_ADS1115_MV_4P096, 5, 1, str_volt);
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, "VREF", vref, str_volt);

}
#endif  // USE_WEBSERVER


/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns97(uint8_t function)
{
  bool result = false;

  switch (function) {
    case FUNC_INIT:
      snsMqx_Init();
      break;
    case FUNC_PREP_BEFORE_TELEPERIOD:
      snsMqx_Ads1115Detect();
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
