#ifndef MQX_SETTINGS
#define MQX_SETTINGS

typedef struct {
  uint16_t hdr_magic;

  // Non-zero if MQ2/MQ7 sensors should be powered up
  uint8_t  mqx_powered;

  // Ro/Rs clean air factors
  float    mq2_clean_air_factor;
  float    mq7_clean_air_factor;

  // Maximum sensor value. Updated in the clean air environment
  // or while initial burning out
  float    mq2_kohm_max;
  float    mq7_kohm_max;

  // Calculated Ro resistance based on read max
  float    mq2_Ro_value;
  float    mq7_Ro_value;

  // Datest of the last calculated value. Used to deprecate Ro value and generate new one from sensor reading min.
  uint32_t mq7_Ro_date;
  uint32_t mq2_Ro_date;

  // MQ2 Alarm
  float    mq2_warning_level_ppm;
  float    mq2_alarm_level_ppm;

  // MQ7 Alarm
  float    mq7_warning_level_ppm;
  float    mq7_alarm_level_ppm;
  float    mq7_alarm_off_delta;

} SNSMQx_Settings;

#endif //MQX_SETTINGS