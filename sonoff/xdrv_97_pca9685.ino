/*
  xdrv_97_pca9685.ino - Support for I2C PCA9685 12bit 16 pin hardware PWM driver

*/

#ifdef USE_I2C
#ifdef USE_PCA9685_DIMMER


#define PCA9685_DIMMER_REG_MODE1           0x00
#define PCA9685_DIMMER_REG_LED0_ON_L       0x06
#define PCA9685_DIMMER_REG_PRE_SCALE       0xFE

#ifndef USE_PCA9685_DIMMER_FREQ
  #define USE_PCA9685_DIMMER_FREQ 1000
#endif

uint8_t PCA9685Dimmer_detected = 0;
uint16_t PCA9685Dimmer_freq = USE_PCA9685_DIMMER_FREQ;
// uint16_t PCA9685Dimmer_pin_pwm_value[16];



// void PCA9685Dimmer_SetPWM_Reg(uint8_t pin, uint16_t on, uint16_t off) {
//   uint8_t led_reg = PCA9685Dimmer_REG_LED0_ON_L + 4 * pin;
//   uint32_t led_data = 0;
//   I2cWrite8(USE_PCA9685_DIMMER_ADDR, led_reg, on);
//   I2cWrite8(USE_PCA9685_DIMMER_ADDR, led_reg+1, (on >> 8));
//   I2cWrite8(USE_PCA9685_DIMMER_ADDR, led_reg+2, off);
//   I2cWrite8(USE_PCA9685_DIMMER_ADDR, led_reg+3, (off >> 8));
// }

// void PCA9685Dimmer_SetPWM(uint8_t pin, uint16_t pwm, bool inverted) {
//   if (4096 == pwm) {
//     PCA9685Dimmer_SetPWM_Reg(pin, 4096, 0); // Special use additional bit causes channel to turn on completely without PWM
//   } else {
//     PCA9685Dimmer_SetPWM_Reg(pin, 0, pwm);
//   }
//   PCA9685Dimmer_pin_pwm_value[pin] = pwm;
// }

// bool PCA9685Dimmer_Command(void)
// {
//   boolean serviced = true;
//   boolean validpin = false;
//   uint8_t paramcount = 0;
//   if (XdrvMailbox.data_len > 0) {
//     paramcount=1;
//   } else {
//     serviced = false;
//     return serviced;
//   }
//   char sub_string[XdrvMailbox.data_len];
//   for (uint8_t ca=0;ca<XdrvMailbox.data_len;ca++) {
//     if ((' ' == XdrvMailbox.data[ca]) || ('=' == XdrvMailbox.data[ca])) { XdrvMailbox.data[ca] = ','; }
//     if (',' == XdrvMailbox.data[ca]) { paramcount++; }
//   }
//   UpperCase(XdrvMailbox.data,XdrvMailbox.data);

//   if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET"))  {  PCA9685Dimmer_Reset(); return serviced; }

//   if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"STATUS"))  { PCA9685Dimmer_OutputTelemetry(false); return serviced; }

//   if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"PWMF")) {
//     if (paramcount > 1) {
//       uint16_t new_freq = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
//       if ((new_freq >= 24) && (new_freq <= 1526)) {
//         PCA9685Dimmer_SetPWMfreq(new_freq);
//         snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"PCA9685\":{\"PWMF\":%i, \"Result\":\"OK\"}}"),new_freq);
//         return serviced;
//       }
//     } else { // No parameter was given for setfreq, so we return current setting
//       snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"PCA9685\":{\"PWMF\":%i}}"),PCA9685Dimmer_freq);
//       return serviced;
//     }
//   }
//   if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"PWM")) {
//     if (paramcount > 1) {
//       uint8_t pin = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
//       if (paramcount > 2) {
//         if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 3), "ON")) {
//           PCA9685Dimmer_SetPWM(pin, 4096, false);
//           snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"PCA9685\":{\"PIN\":%i,\"PWM\":%i}}"),pin,4096);
//           serviced = true;
//           return serviced;
//         }
//         if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 3), "OFF")) {
//           PCA9685Dimmer_SetPWM(pin, 0, false);
//           snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"PCA9685\":{\"PIN\":%i,\"PWM\":%i}}"),pin,0);
//           serviced = true;
//           return serviced;
//         }
//         uint16_t pwm = atoi(subStr(sub_string, XdrvMailbox.data, ",", 3));
//         if ((pin >= 0 && pin <= 15) && (pwm >= 0 && pwm <= 4096)) {
//           PCA9685Dimmer_SetPWM(pin, pwm, false);
//           snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"PCA9685\":{\"PIN\":%i,\"PWM\":%i}}"),pin,pwm);
//           serviced = true;
//           return serviced;
//         }
//       }
//     }
//   }
//   return serviced;
// }

// void PCA9685Dimmer_OutputTelemetry(bool telemetry) {
//   if (0 == PCA9685Dimmer_detected) { return; }  // We do not do this if the PCA9685 has not been detected
//   snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"" D_JSON_TIME "\":\"%s\",\"PCA9685\": {"), GetDateAndTime(DT_LOCAL).c_str());
//   snprintf_P(mqtt_data,sizeof(mqtt_data), PSTR("%s\"PWM_FREQ\":%i,"),mqtt_data,PCA9685Dimmer_freq);
//   for (uint8_t pin=0;pin<16;pin++) {
//     snprintf_P(mqtt_data,sizeof(mqtt_data), PSTR("%s\"PWM%i\":%i,"),mqtt_data,pin,PCA9685Dimmer_pin_pwm_value[pin]);
//   }
//   snprintf_P(mqtt_data,sizeof(mqtt_data),PSTR("%s\"END\":1}}"),mqtt_data);
//   if (telemetry) {
//     MqttPublishPrefixTopic_P(TELE, PSTR(D_RSLT_SENSOR), Settings.flag.mqtt_sensor_retain);
//   }
// }


bool PCA9685Dimmer_Detect(void)
{
  if (PCA9685Dimmer_detected) {
    return true;
  }

  uint8_t buffer;

  if (I2cValidRead8(&buffer, USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1)) {
    I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1, 0x20);
    if (I2cValidRead8(&buffer, USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1)) {
      if (0x20 == buffer) {
        PCA9685Dimmer_detected = 1;
        snprintf_P(log_data, sizeof(log_data), S_LOG_I2C_FOUND_AT, "PCA9685", USE_PCA9685_DIMMER_ADDR);
        AddLog(LOG_LEVEL_DEBUG);
        PCA9685Dimmer_Reset(); // Reset the controller
        return true;
      }
    }
  }
  return false;
}

void PCA9685Dimmer_Reset(void)
{
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1, 0x80);
  PCA9685Dimmer_SetPWMfreq(USE_PCA9685_DIMMER_FREQ);
  // for (uint8_t pin=0;pin<16;pin++) {
  //   PCA9685Dimmer_SetPWM(pin,0,false);
  //   PCA9685Dimmer_pin_pwm_value[pin] = 0;
  // }
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"PCA9685\":{\"RESET\":\"OK\"}}"));
}

void PCA9685Dimmer_SetPWMfreq(double freq) {
/*
 7.3.5 from datasheet
 prescale value = round(25000000/(4096*freq))-1;
 */
  if (freq > 23 && freq < 1527) {
   PCA9685Dimmer_freq = freq;
  } else {
   PCA9685Dimmer_freq = 50;
  }
  uint8_t pre_scale_osc = round(25000000 / (4096 * PCA9685Dimmer_freq)) - 1;
  if (1526 == PCA9685Dimmer_freq) {
    pre_scale_osc=0xFF; // force setting for 24hz because rounding causes 1526 to be 254
  }
  uint8_t current_mode1 = I2cRead8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1); // read current value of MODE1 register
  uint8_t sleep_mode1 = (current_mode1 & 0x7F) | 0x10; // Determine register value to put PCA to sleep
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1, sleep_mode1); // Let's sleep a little
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_PRE_SCALE, pre_scale_osc); // Set the pre-scaler
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1, current_mode1 | 0xA0); // Reset MODE1 register to original state and enable auto increment
}


#define XDRV_97                     97
boolean Xdrv97(byte function)
{
  if (!i2c_flg) {
      return false;
  }
  if (KRIDA_DIMMER == Settings.module) {
    return false;
  }
  boolean result = false;
  uint8_t init_attempts = 5;

  switch (function) {
    case FUNC_MODULE_INIT:
      // Try to init PCA9685 and reset it to the initial state
      // Doin
      while (--init_attempts > 0 && !PCA9685Dimmer_Detect()) {
        delay(25);
      }
      // PCA9685Dimmer_Init();
      break;
    // case FUNC_EVERY_SECOND:
    //   PCA9685Dimmer_Detect();
    //   break;
    // case FUNC_COMMAND:
    //   if (XDRV_97 == XdrvMailbox.index) {
    //       PCA9685Dimmer_Command();
    //   }
    //   break;
    default:
      break;
  }
  return result;
}

#endif // USE_PCA9685_DIMMER
#endif // USE_IC2
