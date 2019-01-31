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

#define PCA9685_DIMMER_CHANNELS            16

#define PCA9685_DIMMER_I2C_BUFFERS         3

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
uint32_t PCA9685Dimmer_twi_errors = 0;


struct I2C_Bucket_t {
  // start bit position, 0..15
  uint8_t   pos:4;
  // bucket size: 0..7
  uint8_t   len:3;
  uint8_t   unused:1;
};

// Max 3 I2C buckets: 7 + 7 + 2
typedef struct I2C_Bucket_t I2C_Bucket_Array[PCA9685_DIMMER_I2C_BUFFERS];

typedef struct {
  // Current dimmer value phase shift
  uint16_t   m_shift;
  // Current dimmer value
  uint16_t   m_value;
  // Current dimmer value target
  uint16_t   m_target;
  // Curent velocity of m_value change
  uint8_t    m_velocity;
} PCA9685Dimmer_Channel;

// A low-level data structure used by 50ms timer to update PCA9685 PWM registers and
// perform dimming/color transitions
struct {
  // A bit mask of channels in fade transition. Do not update directly, use . instead
  uint16_t                m_acting;
  // An I2C buckets array created from m_acting value
  I2C_Bucket_Array        m_buckets;
  // Channel information
  PCA9685Dimmer_Channel   m_channel[PCA9685_DIMMER_CHANNELS];
} PCA9685Dimmer_Channels;

/////////////// Acting & Buckets handling
#define SEQUENCE_LENGTH(channels, start, mask) ((sizeof(int) * 8) - __builtin_clz(channels & ~mask) - start)

void PCA9685Dimmer_makeBuckets(struct I2C_Bucket_t* buckets, uint16_t channels) {
  memset(buckets, 0, sizeof(I2C_Bucket_Array));

  for (int i = 0; channels && i < PCA9685_DIMMER_I2C_BUFFERS; ++i) {
    int start = __builtin_ffs(channels) - 1;
    int len = SEQUENCE_LENGTH(channels, start, 0);
    // Mask high order bits until whole sequence fits into max 7 len bucket
    // lengt might be significantly below 7
    int16_t mask = 0x8000;
    while (len >= 8) {
      len = SEQUENCE_LENGTH(channels, start, mask);
      mask |= mask >> 1;
    }
    buckets[i].pos = start;
    buckets[i].len = len;
    // mask bucketed bits
    mask = (1ul << (start + len)) - 1;
    channels &= ~mask;
  }
}

void PCA9685Dimmer_updateActing(uint16_t acting) {
  PCA9685Dimmer_makeBuckets(PCA9685Dimmer_Channels.m_buckets, acting);
  PCA9685Dimmer_Channels.m_acting = acting;
}



void PCA9685Dimmer_InitDevices(void) {
  devices_present = 1;
  light_type = LT_BASIC;

  // Turn off all channels
  memset(&PCA9685Dimmer_Channels, 0, sizeof(PCA9685Dimmer_Channels));

  // Next 50ms cycle PCA9685 will be updated with zero values
  PCA9685Dimmer_updateActing(0xFFFF);

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
        PCA9685Dimmer_InitDevices();
        return true;
      }
    }
  }
  return false;
}

void PCA9685Dimmer_Reset(void)
{
  // 0x80 - reset
  // 0x20 - enable register autoincrement
  uint8_t mode1 = 0xA0;
  I2cWrite8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE1, mode1);

  uint8_t mode2 = I2cRead8(USE_PCA9685_DIMMER_ADDR, PCA9685_DIMMER_REG_MODE2);

  // Clear OCH bit causing outputs change on STOP command
  mode2 &= ~(1 << 3);

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
////////////////////////// Low level dimming /////////////////////////
//////////////////////////////////////////////////////////////////////

boolean PCA9685Dimmer_Advance(void) {
  // Do nothing if none of the channels should be updated
  if (PCA9685Dimmer_Channels.m_acting == 0) {
    return false;
  }

  // Update acting bits & advance dimmers
  uint16_t acting = 0;
  for (int i = 0; i < PCA9685_DIMMER_CHANNELS; ++i) {
    PCA9685Dimmer_Channel* ch = &PCA9685Dimmer_Channels.m_channel[i];
    // If value match target, do nothing
    if (ch->m_value == ch->m_target) {
      continue;
    }

    // If velocity value is zero (and values does not match), set value to target
    if (ch->m_velocity == 0) {
      ch->m_value = ch->m_target;
    } else {
      // Get diff between current value and target value
      int32_t diff = (int32_t)ch->m_value - (int32_t)ch->m_target;
      int32_t delta = ch->m_velocity;
      // If diff between target and value is less-eq that delta, update value to the target
      if (abs(diff) <= delta) {
        ch->m_value = ch->m_target;
      } else {
        // A case, where value far from target. Move value toward the target
        ch->m_value = diff > 0 ? ch->m_value - delta : ch->m_value + delta;
        // Set acting bit in the position
        acting |= (1 << i);
      }
    }
  }


  // Update PCA9685 values using buckets to avoid 32 byte I2C buffer limit
  for (int bucket = 0; bucket < PCA9685_DIMMER_I2C_BUFFERS; ++bucket) {
    if (PCA9685Dimmer_Channels.m_buckets[bucket].len == 0) {
      break;
    }

    int pos =  PCA9685Dimmer_Channels.m_buckets[bucket].pos;
    int len =  PCA9685Dimmer_Channels.m_buckets[bucket].len;
    // Start transmission
    Wire.beginTransmission((uint8_t)USE_PCA9685_DIMMER_ADDR);
    // Select start address based on a bucket start
    Wire.write(PCA9685_DIMMER_REG_LED0_ON_L + (pos * 4));

    // Mode1 AI flag should be enabled
    for (int i = pos; i < (pos + len); ++i) {
      uint16_t value = PCA9685Dimmer_Channels.m_channel[i].m_value;
      uint16_t start = PCA9685Dimmer_Channels.m_channel[i].m_shift;

      if (value >= 0x1000) {
        // Special case, turn on, no PWM
        value = 0;
        start = 0x1000;
      } else if (value == 0) {
        // Special case, turn off, no PWM
        value = 0x1000;
        start = 0;
      } else {
        value += start;
        if (value >= 0x1000) {
          value &= 0xFFF;
        }
      }

      Wire.write(start);
      Wire.write((start >> 8));
      Wire.write(value);
      Wire.write((value >> 8));
    }
    // Send stop signal
    if (Wire.endTransmission(true) != 0) {
      ++PCA9685Dimmer_twi_errors;
    }
  }

  PCA9685Dimmer_updateActing(acting);

  return true;
}

//////////////////////////////////////////////////////////////////////
////////////////////////// Command handling //////////////////////////
//////////////////////////////////////////////////////////////////////

enum PCA9685Dimmer_Commands {
  // Set or retrieve PCA9685 settings
  PCA9685_CMND_SETUP,
  // get/set raw value for the channel
  PCA9685_CMND_RAW
};

const char g_PCA9685Dimmer_Commands[] PROGMEM =
  "P9SETUP|P9RAW";

boolean PCA9685Dimmer_CheckParamCount(uint16_t count) {
  if (XdrvMailbox.data_len == 0) {
    return count == 0;
  }
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

void PCA9685Dimmer_CommandRaw(boolean isGet) {
  if (XdrvMailbox.index >= PCA9685_DIMMER_CHANNELS) {
    // Index OOB
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR(D_JSON_ERROR));
    return;
  }

  if (!isGet) {
    if (!PCA9685Dimmer_CheckParamCount(1)) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR(D_JSON_ERROR));
      return;
    }
    char sub_string[XdrvMailbox.data_len + 1];
    char* val = subStr(sub_string, XdrvMailbox.data, ",", 1);

    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_shift = 0;
    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_target = atoi(val);
    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_velocity = 32;

    // Update PWM on the next 50ms cycle
    PCA9685Dimmer_updateActing(1 << XdrvMailbox.index);
  }
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"shift\":%d,\"value\":%d,\"target\":%d,\"velocity\":%d}"),
    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_shift,
    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_value,
    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_target,
    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_velocity
  );

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
      if (XdrvMailbox.data_len == 0) {
        PCA9685Dimmer_CommandSetupPrint();
      } else {
        PCA9685Dimmer_CommandSetup();
      }
      return true;
    case PCA9685_CMND_RAW:
      PCA9685Dimmer_CommandRaw(XdrvMailbox.data_len == 0);
      return true;
    default:
      return false;
  }
  return false;
}

void PCA9685Dimmer_OutputTelemetry() {
  if (0 == PCA9685Dimmer_detected) {
    return;
  }
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"" D_JSON_TIME "\":\"%s\",\"PCA9685Dimmer\": {"), GetDateAndTime(DT_LOCAL).c_str());
  snprintf_P(mqtt_data,sizeof(mqtt_data), PSTR("%s\"FREQ\":%d,\"INVRT\":%d,\"OUTDRV\":%s,\"BEVT\":%d,\"I2C_ERR\":%d}"),
    mqtt_data,
    Settings.pca685_dimmer.cfg.freq,
    Settings.pca685_dimmer.cfg.inv_out,
    (Settings.pca685_dimmer.cfg.totem_out ? "TOTEM" : "ODRAIN"),
    PCA9685Dimmer_boot_events,
    PCA9685Dimmer_twi_errors
  );
  MqttPublishPrefixTopic_P(TELE, PSTR(D_RSLT_SENSOR), Settings.flag.mqtt_sensor_retain);
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
      result = true;
      break;

    case FUNC_EVERY_SECOND:
      PCA9685Dimmer_LogBootEvents();
      if (!PCA9685Dimmer_detected) {
        PCA9685Dimmer_Detect();
        snprintf_P(log_data, sizeof(log_data), PSTR("PCA9685: Delayed init success"));
        AddLog(LOG_LEVEL_DEBUG);
      }
      if (tele_period == 0) {
        PCA9685Dimmer_OutputTelemetry();
      }

      break;
    case FUNC_COMMAND:
      result = PCA9685Dimmer_Command();
      break;
    case FUNC_EVERY_50_MSECOND: {
      PCA9685Dimmer_Advance();
      break;

    }
    default:
      break;
  }
  return result;
}

#endif // USE_PCA9685_DIMMER
#endif // USE_IC2
