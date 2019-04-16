#ifndef XSNS_97
#define XSNS_97            97

uint8_t sns_mqx_has_mq7 = 1;
uint8_t sns_mqx_has_mq2 = 1;
uint8_t sns_mqx_has_pwr_control = 1;
uint8_t sns_mqx_is_mq7_heating = 0;

// Calibrate MQ-2 on power on
uint8_t sns_mq2_calibrating = 1;
// ~25 seconds calibration time of MQ-2 & store coundown
#define MQX_CALIBRATION_250MS_LOOPS 100
uint8_t sns_mq2_calibration_loop = MQX_CALIBRATION_250MS_LOOPS;
// Ro value (subject of calibration)
float sns_mq2_Ro_value = 0;


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
      AddLog_P2(LOG_LEVEL_DEBUG, S_LOG_I2C_FOUND_AT, "MQX: ADS1115", snsMqx_ads1115_address);
    }
  }
  // Start sensor calibration after
  if (snsMqx_ads1115_address) {
    snsMqx_CalibrateMQ2();
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

#define RL_VALUE                     (10)   // Sensors load resistance kOhms

float snsMqx_ResistanceCalculation(float raw_adc)
{
  float maxADC = snsMqx_Ads1115GetConversion(2);
  if (raw_adc < 1) {
    raw_adc = 1;
  }
  return (((float)RL_VALUE * (maxADC - raw_adc) / raw_adc));
}

float snsMqx_CH4Curve[3]  =  {2.3, 0.484, -0.3762  };
float snsMqx_GetPercentage(float rs_ro_ratio, float *curve)
{
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GET PCT: rs_ro_ratio=%d, curve[0,1,2]=%d,%d,%d"),
    (int)(rs_ro_ratio * 100.0),
    (int)(curve[0] * 100.0),
    (int)(curve[1] * 100.0),
    (int)(curve[2] * 100.0));

  //Using slope,ratio(y2) and another point(x1,y1) on line we will find
  // gas concentration(x2) using x2 = [((y2-y1)/slope)+x1]
  // as in curves are on logarithmic coordinate, power of 10 is taken to convert result to non-logarithmic.
  float exp_val = ((log(rs_ro_ratio) - curve[1]) / curve[2]) + curve[0];
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR("GET PCT: exp_val = %d"),
    (int)(exp_val * 100.0));
  return pow(10, exp_val);
}

#ifdef USE_WEBSERVER

const char HTTP_MQX_ANALOG[] PROGMEM = "%s{s}%s %s{m}%d (%s kOhm) %s PPM{e}";                               // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>

#define MQX_ADS1115_MV_4P096            0.125000
void snsMqx_Show(void) {

  if (!snsMqx_ads1115_address) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}Detecting 1115...{m}{e}"), mqtt_data);
    return;
  }

  snsMqx_ShowMQ2();
  snsMqx_ShowMQ7();
}

void snsMqx_ShowMQ2(void) {
  if (sns_mq2_calibrating || sns_mq2_Ro_value < .1) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s{s}Calibrating MQ-2...{m}%d{e}"), mqtt_data, sns_mq2_calibration_loop);
    return;
  }

  int mq2_raw = snsMqx_Ads1115GetConversion(0);
  float mq2_kOhm = snsMqx_ResistanceCalculation((float)mq2_raw);
  float mq2_ppm = snsMqx_GetPercentage(mq2_kOhm / sns_mq2_Ro_value, snsMqx_CH4Curve);

  char label[15];
  snprintf_P(label, sizeof(label), "ADS1115(%02x)", snsMqx_ads1115_address);

  char str_res[24];
  dtostrf(mq2_kOhm, 6, 2, str_res);
  char str_ppm[24];
  dtostrf(mq2_ppm, 6, 4, str_ppm);
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, "MQ-2", mq2_raw, str_res, str_ppm);
}

void snsMqx_ShowMQ7(void) {

  char label[15];
  snprintf_P(label, sizeof(label), "ADS1115(%02x)", snsMqx_ads1115_address);
  char str_volt[24];

  int16_t mq7 = snsMqx_Ads1115GetConversion(1);
  int16_t vref = snsMqx_Ads1115GetConversion(2);

  dtostrf(snsMqx_ResistanceCalculation((float)mq7), 6, 2, str_volt);
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, "MQ-7", mq7, str_volt, "-");

  dtostrf((float)vref * MQX_ADS1115_MV_4P096, 5, 1, str_volt);
  snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_MQX_ANALOG, mqtt_data, label, "VREF", vref, str_volt, "-");
}

#endif  // USE_WEBSERVER

// Enable MQ-2 calibration
void snsMqx_CalibrateMQ2(void) {
  sns_mq2_calibrating = 1;
  sns_mq2_calibration_loop = MQX_CALIBRATION_250MS_LOOPS;
  AddLog_P(LOG_LEVEL_DEBUG, PSTR("MQX: Start MQ-2 calibration"));
}

// Update MQ-2 calibration
#define RO_CLEAN_AIR_FACTOR          (9.83)   //(Sensor resistance in clean air)/RO,
                                              //which is derived from the chart in datasheet
float sns_mq2_InitialResistance_value;
void snsMqx_CalibrateMQ2_step(void) {
  if (!sns_mq2_calibrating) {
    return;
  }

  float mq2Resistance = snsMqx_ResistanceCalculation((float)snsMqx_Ads1115GetConversion(0));
  if (mq2Resistance <= 0) {
    return;
  }
  // Reuse sns_mq2_Ro_value for the MQ-2 resistance during the calibration
  if (sns_mq2_Ro_value == 0) {
    sns_mq2_Ro_value = mq2Resistance;
    sns_mq2_InitialResistance_value = mq2Resistance;
  } else {
    sns_mq2_Ro_value *= 0.9;                  // applying exponential smoothing, A = 0.1
    sns_mq2_Ro_value += 0.1 * mq2Resistance;
  }

  if (--sns_mq2_calibration_loop == 0) {
    sns_mq2_calibrating = 0;
    char str_tmp[24];

    dtostrf(sns_mq2_InitialResistance_value, 6, 2, str_tmp);
    snprintf_P(log_data, sizeof(log_data), PSTR("MQX: End MQ-2 calibration. R(Start): %s"), str_tmp);

    dtostrf(sns_mq2_Ro_value, 6, 2, str_tmp);
    snprintf_P(log_data, sizeof(log_data), PSTR("%s, R(Avg): %s"), log_data, str_tmp);

    sns_mq2_Ro_value /= RO_CLEAN_AIR_FACTOR;

    dtostrf(sns_mq2_Ro_value, 6, 2, str_tmp);
    snprintf_P(log_data, sizeof(log_data), PSTR("%s, Ro=%s"), log_data, str_tmp);

    AddLog(LOG_LEVEL_DEBUG);
  }
}

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
    case FUNC_EVERY_250_MSECOND:
      if (sns_mq2_calibrating) {
        snsMqx_CalibrateMQ2_step();
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
