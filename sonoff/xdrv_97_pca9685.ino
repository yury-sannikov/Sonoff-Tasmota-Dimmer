/*
  xdrv_97_pca9685.ino - Support for I2C PCA9685 12bit 16 pin hardware PWM driver

*/

#ifdef USE_I2C
#ifdef USE_PCA9685_DIMMER


#define PCA9685_DIMMER_REG_MODE1           0x00
#define PCA9685_DIMMER_REG_MODE2           0x01
#define PCA9685_DIMMER_REG_LED0_ON_L       0x06
#define PCA9685_DIMMER_REG_PRE_SCALE       0xFE

#define PCA9685_BIT_INVRT_POS              4
#define PCA9685_BIT_OUTDRV_POS             2


#ifndef USE_PCA9685_DIMMER_FREQ
  #define USE_PCA9685_DIMMER_FREQ 1000
#endif


#ifndef USE_PCA9685_DIMMER_INVERT_OUTPUT
  // Use non-inverted output by default
  #define USE_PCA9685_DIMMER_INVERT_OUTPUT 0
#endif

#ifndef USE_PCA9685_DIMMER_TOTEM_OUTPUT
  // Configure as a totem pole structure by default
  #define USE_PCA9685_DIMMER_TOTEM_OUTPUT 1
#endif


enum PCA9685_BOOT_EVENT {
  PCA9685_BOOT_NONE = 0,
  // Settings has been reset during boot
  PCA9685_BOOT_SETTINGS = 1,
  // Module initialization was failed during boot
  PCA9685_BOOT_FAIL = 2
};

uint8_t PCA9685Dimmer_boot_events = PCA9685_BOOT_NONE;

uint8_t PCA9685Dimmer_detected = 0;


//DEL IT! uint16_t PCA9685Dimmer_freq = USE_PCA9685_DIMMER_FREQ;
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

void PCA9685Dimmer_InitDevices(void) {
  devices_present = 1;
  light_type = LT_BASIC;

}

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
  uint8_t mode1 = 0x80;
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1, mode1);

  uint8_t mode2 = I2cRead8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE2);
  // Clear & set INVRT bit
  mode2 &= ~(1 << PCA9685_BIT_INVRT_POS);
  if (Settings.pca685_dimmer.cfg.inv_out) {
    mode2 |= 1 << PCA9685_BIT_INVRT_POS;
  }

  // Clear & set OUTDRV
  mode2 &= ~(1 << PCA9685_BIT_OUTDRV_POS);
  if (Settings.pca685_dimmer.cfg.totem_out) {
    mode2 |= 1 << PCA9685_BIT_OUTDRV_POS;
  }
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE2, mode2);

  PCA9685Dimmer_SetPWMfreq(Settings.pca685_dimmer.cfg.freq);
  // for (uint8_t pin=0;pin<16;pin++) {
  //   PCA9685Dimmer_SetPWM(pin,0,false);
  //   PCA9685Dimmer_pin_pwm_value[pin] = 0;
  // }
  snprintf_P(log_data, sizeof(mqtt_data), PSTR("{\"PCA9685\":{\"RESET\":\"OK\",\"mode1\":%d,\"mode2\":%d}}"), mode1, mode2);
  AddLog(LOG_LEVEL_DEBUG);

}

void PCA9685Dimmer_SetPWMfreq(uint16_t freq) {
/*
 7.3.5 from datasheet
 prescale value = round(25000000/(4096*freq))-1;
 */

  if (freq <= 23 || freq >= 1527) {
   freq = 50;
  }

  uint8_t pre_scale_osc = round(25000000.0 / (4096.0 * freq)) - 1;
  if (1526 <= freq) {
    pre_scale_osc = 0xFF; // force setting for 24hz because rounding causes 1526 to be 254
  }
  uint8_t current_mode1 = I2cRead8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1); // read current value of MODE1 register
  uint8_t sleep_mode1 = (current_mode1 & 0x7F) | 0x10; // Determine register value to put PCA to sleep
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1, sleep_mode1); // Let's sleep a little
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_PRE_SCALE, pre_scale_osc); // Set the pre-scaler
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1, current_mode1 | 0xA0); // Reset MODE1 register to original state and enable auto increment
}

#define SETTINGS_MAGIC 0xA0DE

void PCA9685Dimmer_CheckSettings (void) {
  uint16_t freq = Settings.pca685_dimmer.cfg.freq;

  if (freq <= 23 || freq >= 1527) {
    Settings.pca685_dimmer.cfg.freq = USE_PCA9685_DIMMER_FREQ;
  }

  if (Settings.pca685_dimmer.hdr_magic == SETTINGS_MAGIC) {
    return;
  }
  // Init settings
  Settings.pca685_dimmer.hdr_magic = SETTINGS_MAGIC;
  Settings.pca685_dimmer.cfg.freq = USE_PCA9685_DIMMER_FREQ;
  Settings.pca685_dimmer.cfg.inv_out = USE_PCA9685_DIMMER_INVERT_OUTPUT;
  Settings.pca685_dimmer.cfg.totem_out = USE_PCA9685_DIMMER_TOTEM_OUTPUT;

  // Indicate that settings has been reset
  PCA9685Dimmer_boot_events |= PCA9685_BOOT_SETTINGS;
}

void PCA9685Dimmer_LogBootEvents(void) {
  // Report boot event if any
  if (PCA9685Dimmer_boot_events == PCA9685_BOOT_NONE) {
    return;
  }
  snprintf_P(log_data, sizeof(log_data), PSTR("PCA9685: "));

  if (PCA9685Dimmer_boot_events & PCA9685_BOOT_SETTINGS) {
    snprintf_P(log_data, sizeof(log_data), PSTR("%s Settings has been reset"), log_data);
  }

  if (PCA9685Dimmer_boot_events & PCA9685_BOOT_FAIL) {
    snprintf_P(log_data, sizeof(log_data), PSTR("%s !! chip init failed !!"), log_data);
  }

  AddLog(LOG_LEVEL_ERROR);

  PCA9685Dimmer_boot_events = PCA9685_BOOT_NONE;
}


//////////////////////////////////////////////////////////////////////
////////////////////////// Command handling //////////////////////////
//////////////////////////////////////////////////////////////////////

enum PCA9685Dimmer_Commands {
  // Set or retrieve PCA9685 settings
  PCA9685_CMND_SETUP
};

const char g_PCA9685Dimmer_Commands[] PROGMEM =
  "P9SETUP";

boolean PCA9685Dimmer_CheckParamCount(uint16_t count) {
  char *q = XdrvMailbox.data;
  for (; *q; count -= (*q++ == ','));
  return count == 1;
}


void PCA9685Dimmer_CommandSetupPrint(void) {
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"FREQ\":%d,\"INVRT\":%d,\"OUTDRV\":%s}"),
    Settings.pca685_dimmer.cfg.freq,
    Settings.pca685_dimmer.cfg.inv_out,
    (Settings.pca685_dimmer.cfg.totem_out ? "TOTEM" : "ODRAIN")
  );
}

void PCA9685Dimmer_CommandSetup(void) {
  if (!PCA9685Dimmer_CheckParamCount(3)) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR(D_JSON_ERROR));
    return;
  }

  char sub_string[XdrvMailbox.data_len + 1];
  char* freqStr = subStr(sub_string, XdrvMailbox.data, ",", 1);
  char* invStr = subStr(sub_string, XdrvMailbox.data, ",", 2);
  char* outDrvStr = subStr(sub_string, XdrvMailbox.data, ",", 3);

  Settings.pca685_dimmer.cfg.freq = atoi(freqStr);
  Settings.pca685_dimmer.cfg.inv_out = atoi(invStr);
  Settings.pca685_dimmer.cfg.totem_out = atoi(outDrvStr);

  PCA9685Dimmer_CheckSettings();
  PCA9685Dimmer_Reset();
  PCA9685Dimmer_CommandSetupPrint();
}


boolean PCA9685Dimmer_Command(void)
{
  char command[CMDSZ];
  int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic, g_PCA9685Dimmer_Commands);
  if (-1 == command_code) {
    return false;
  }
  snprintf_P(log_data, sizeof(log_data), PSTR("PCA9685: cmd %s"), XdrvMailbox.topic);
  AddLog(LOG_LEVEL_DEBUG);

  switch(command_code) {
    case PCA9685_CMND_SETUP:
      if (strlen(XdrvMailbox.data) == 0) {
        PCA9685Dimmer_CommandSetupPrint();
      } else {
        PCA9685Dimmer_CommandSetup();
      }
      return true;
    default:
      return false;
  }
  return false;
}

#define XDRV_97                     97
boolean Xdrv97(byte function)
{
  if (!i2c_flg) {
      return false;
  }
  if (PCA9685_DIMMER != Settings.module) {
    return false;
  }
  boolean result = false;
  uint8_t init_attempts = 5;

  switch (function) {
    case FUNC_SETTINGS_OVERRIDE:
      PCA9685Dimmer_CheckSettings();
      return true;

    case FUNC_MODULE_INIT:
      // Try to init PCA9685 and reset it to the initial state
      while (--init_attempts > 0 && !PCA9685Dimmer_Detect()) delay(25);

      if (init_attempts == 0) {
        PCA9685Dimmer_boot_events |= PCA9685_BOOT_FAIL;
      }
      PCA9685Dimmer_InitDevices();
      result = true;
      break;

    case FUNC_EVERY_SECOND:
      PCA9685Dimmer_LogBootEvents();
      if (!PCA9685Dimmer_detected) {
        PCA9685Dimmer_Detect();
        PCA9685Dimmer_InitDevices();
        snprintf_P(log_data, sizeof(log_data), PSTR("PCA9685: Delayed init success"));
      }
      break;
    case FUNC_COMMAND:
      result = PCA9685Dimmer_Command();
      break;
    default:
      break;
  }
  return result;
}

#endif // USE_PCA9685_DIMMER
#endif // USE_IC2
