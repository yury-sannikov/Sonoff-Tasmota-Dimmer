/*
  xdrv_97_pca9685.ino - Support for I2C PCA9685 12bit 16 pin hardware PWM driver

*/

#ifdef USE_I2C
#ifdef USE_PCA9685_DIMMER


#define PCA9685_DIMMER_REG_MODE1           0x00
#define PCA9685_DIMMER_REG_MODE2           0x01
#define PCA9685_DIMMER_REG_LED0_ON_L       0x06
#define PCA9685_DIMMER_REG_PRE_SCALE       0xFE
#define PCA9685_DIMMER_MAX_DIMMER_VALUE    0x1000

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

// power on or off velocity in 4096 / 50ms ticks
#define VELOCITY_ON_OFF 64


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
  uint16_t   m_velocity;
  // Current transition happening due to the power ON/OFF.
  // If set, change to the velocity can't be made until the end of the transition
  uint8_t    m_powerTransition;
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


// Previous state of power value
power_t PCA9685Dimmer_power;

/**
 * // CIE1931 correction table generation script

# min PWM value rendering non-dark LED
# depends on schematic/led/pwm freq
OUT_START = 35
# Correction array size
INPUT_SIZE = 256
# MAX PWM value
OUTPUT_SIZE = 4096
INT_TYPE = 'const uint16_t'
TABLE_NAME = 'PCA9685Dimmer_corr';

def cie1931(L):
    L = L*100.0
    if L <= 8:
        return (L/902.3)
    else:
        return ((L+16.0)/116.0)**3

x = range(0,int(INPUT_SIZE) - 1)
y = [round(cie1931(float(L)/(INPUT_SIZE - 2))*(OUTPUT_SIZE - OUT_START)) + OUT_START for L in x]

print('// CIE1931 correction table')
print('%s %s[%d] PROGMEM = {0, ' % (INT_TYPE, TABLE_NAME, len(x) + 1), end='')
for i,L in enumerate(y):
    print('%d, ' % int(L), end='')
    if i % 15 == 14:
        print('\n\t', end='')
print('\n};\n\n')
*/

// CIE1931 correction table

#define CORRECTION_TABLE_LENGTH 256

const uint16_t PCA9685Dimmer_corr[CORRECTION_TABLE_LENGTH] = {
  0, 38, 40, 42, 43, 45, 47, 49, 50, 52, 54, 56, 57, 59, 61, 63,
  65, 66, 68, 70, 72, 73, 75, 77, 79, 81, 83, 85, 87, 89, 92,
  94, 96, 99, 101, 104, 107, 109, 112, 115, 118, 121, 124, 128, 131, 134,
  138, 141, 145, 148, 152, 156, 160, 164, 168, 172, 177, 181, 186, 190, 195,
  200, 205, 210, 215, 220, 225, 230, 236, 241, 247, 253, 259, 265, 271, 277,
  283, 290, 296, 303, 310, 317, 324, 331, 338, 345, 353, 360, 368, 376, 384,
  392, 400, 408, 417, 425, 434, 443, 452, 461, 470, 479, 489, 498, 508, 518,
  528, 538, 549, 559, 570, 580, 591, 602, 613, 625, 636, 648, 659, 671, 683,
  696, 708, 721, 733, 746, 759, 772, 785, 799, 813, 826, 840, 854, 869, 883,
  898, 912, 927, 942, 958, 973, 989, 1005, 1021, 1037, 1053, 1069, 1086, 1103, 1120,
  1137, 1155, 1172, 1190, 1208, 1226, 1244, 1263, 1281, 1300, 1319, 1339, 1358, 1378, 1398,
  1418, 1438, 1458, 1479, 1500, 1521, 1542, 1563, 1585, 1607, 1629, 1651, 1674, 1696, 1719,
  1742, 1765, 1789, 1813, 1836, 1861, 1885, 1909, 1934, 1959, 1984, 2010, 2036, 2061, 2087,
  2114, 2140, 2167, 2194, 2221, 2249, 2276, 2304, 2332, 2361, 2389, 2418, 2447, 2477, 2506,
  2536, 2566, 2596, 2627, 2657, 2688, 2720, 2751, 2783, 2815, 2847, 2880, 2912, 2945, 2978,
  3012, 3046, 3080, 3114, 3148, 3183, 3218, 3253, 3289, 3324, 3360, 3397, 3433, 3470, 3507,
  3545, 3582, 3620, 3658, 3697, 3735, 3774, 3814, 3853, 3893, 3933, 3973, 4014, 4055, 4096
};

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
  devices_present = 0;

  // counting actual devices
  for (int i = 0; i < 16; ++i) {
    if (Settings.pca685_dimmer.lamps[i].type == PCA9685_LAMP_NONE) {
      break;
    }

    // Check values
    if (Settings.pca685_dimmer.lamps[i].type == PCA9685_LAMP_MULTIWHITE) {
      uint8_t warm = Settings.pca685_dimmer.lamps[i].warm_temp;
      uint8_t cold = Settings.pca685_dimmer.lamps[i].cold_temp;
      if (warm < 20 || warm > 80 ) {
        warm = 27;
      }
      if (cold < 20 || cold > 80 ) {
        cold = 70;
      }
      if (warm >= cold) {
        warm = 27;
        cold = 70;
      }
      Settings.pca685_dimmer.lamps[i].warm_temp = warm;
      Settings.pca685_dimmer.lamps[i].cold_temp = cold;

      if (Settings.pca685_dimmer.lamps[i].color_temperature < (warm * 100)) {
        Settings.pca685_dimmer.lamps[i].color_temperature = warm * 100;
      }
      if (Settings.pca685_dimmer.lamps[i].color_temperature > (cold * 100)) {
        Settings.pca685_dimmer.lamps[i].color_temperature = cold * 100;
      }
    } else {
      // Set black color to white. Do not allow black color, use power off instead
      if (Settings.pca685_dimmer.lamps[i].color.rgb == 0) {
        Settings.pca685_dimmer.lamps[i].color.rgb = 0xFFFFFF;
      }
    }

    // Do not zero out brightness. Use power off instead
    if (Settings.pca685_dimmer.lamps[i].brightness == 0) {
      Settings.pca685_dimmer.lamps[i].brightness = 1;
    }

    ++devices_present;
  }

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

#define SETTINGS_MAGIC 0xA0E2

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

  memset(Settings.pca685_dimmer.lamps, 0, sizeof(Settings.pca685_dimmer.lamps));
  for (int i = 0; i < 16; ++i) {
    Settings.pca685_dimmer.lamps[i].type = PCA9685_LAMP_NONE;
    // Immediate transition
    Settings.pca685_dimmer.lamps[i].velocity = 0;
    Settings.pca685_dimmer.lamps[i].warm_temp = 27;
    Settings.pca685_dimmer.lamps[i].cold_temp = 70;
    Settings.pca685_dimmer.lamps[i].brightness = 0xFF;
    Settings.pca685_dimmer.lamps[i].color.rgb = 0xFFFFFF;
    Settings.pca685_dimmer.lamps[i].color_temperature = 4800;
  }

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

//////////////////////////// RGB / Colors //////////////////////////////////////////////////////////////

void PCA9685Dimmer_convertRGBtoXYZ(int inR, int inG, int inB, float * outX, float * outY, float * outZ) {

	float var_R = (inR / 255.0f); //R from 0 to 255
	float var_G = (inG / 255.0f); //G from 0 to 255
	float var_B = (inB / 255.0f); //B from 0 to 255

	if (var_R > 0.04045f)
		var_R = powf(( (var_R + 0.055f) / 1.055f), 2.4f);
	else
		var_R = var_R / 12.92f;

	if (var_G > 0.04045)
		var_G = powf(( (var_G + 0.055f) / 1.055f), 2.4f);
	else
		var_G = var_G / 12.92f;

	if (var_B > 0.04045f)
		var_B = powf(( (var_B + 0.055f) / 1.055f), 2.4f);
	else
		var_B = var_B / 12.92f;

	var_R = var_R * 100;
	var_G = var_G * 100;
	var_B = var_B * 100;

	//Observer. = 2°, Illuminant = D65
	*outX = var_R * 0.4124f + var_G * 0.3576f + var_B * 0.1805f;
	*outY = var_R * 0.2126f + var_G * 0.7152f + var_B * 0.0722f;
	*outZ = var_R * 0.0193f + var_G * 0.1192f + var_B * 0.9505f;
}


// Given RGB is set to the lamp, update color temperature based on RGB
void PCA9685Dimmer_applyRGBToColorTemperature(int index) {
  // Get RGB components and convert it to the color temperature
  int r = Settings.pca685_dimmer.lamps[index].color.red;
  int g = Settings.pca685_dimmer.lamps[index].color.green;
  int b = Settings.pca685_dimmer.lamps[index].color.blue;

  float X, Y, Z;
  PCA9685Dimmer_convertRGBtoXYZ(r, g, b, &X, &Y, &Z);
  float summ = X + Y + Z;
  if (summ == 0.0) {
    return;
  }
  float x = X / summ;
  float y = Y / summ;
  float n = (x - 0.3320) / (0.1858 - y);

  int color_temp = ((449 * n * n * n) + (3525 * n * n) + (6823.3 * n) + 5520.33);

  int warm_temp = (int)Settings.pca685_dimmer.lamps[index].warm_temp * 100;
  int cold_temp = (int)Settings.pca685_dimmer.lamps[index].cold_temp * 100;
  if (color_temp < warm_temp) {
    color_temp = warm_temp;
  }
  if (color_temp > cold_temp && cold_temp > 0) {
    color_temp = cold_temp;
  }
  Settings.pca685_dimmer.lamps[index].color_temperature = color_temp;
}

// Apply brightness/color/temp changes from settings to the PWM pins
// if isPower is set, set m_powerTransition. Else check if m_powerTransition set and ignore velocity
void PCA9685Dimmer_applyColorBrightness(int index, uint16_t velocity, boolean isPower) {

  // power is not obvoius global var...
  boolean power_on = power & (1 << index);
  // Get brightness from settings or zero, if power is off for this lamp
  uint16_t brightness = power_on ? ((uint16_t)Settings.pca685_dimmer.lamps[index].brightness << 4 ) : 0;

  switch(Settings.pca685_dimmer.lamps[index].type) {
    case PCA9685_LAMP_SINGLE: {
      int pin = Settings.pca685_dimmer.lamps[index].pins.ch0;
      PCA9685Dimmer_Channels.m_channel[pin].m_shift = 0;
      PCA9685Dimmer_Channels.m_channel[pin].m_target = brightness;
      if (isPower || !PCA9685Dimmer_Channels.m_channel[pin].m_powerTransition) {
        PCA9685Dimmer_Channels.m_channel[pin].m_velocity = velocity;
      }
      PCA9685Dimmer_updateActing(1 << pin);
      break;
    }
    // A 2-channel LED strip with warm white and cold white
    case PCA9685_LAMP_MULTIWHITE: {
      float color_temp = Settings.pca685_dimmer.lamps[index].color_temperature;
      float warm_temp = (float)Settings.pca685_dimmer.lamps[index].warm_temp * 100;
      float cold_temp = (float)Settings.pca685_dimmer.lamps[index].cold_temp * 100;

      float distance = cold_temp - warm_temp;
      if (distance == 0) { distance = 1; }
      float warm = (distance - (color_temp - warm_temp)) / distance;
      float cold = (distance - (cold_temp - color_temp)) / distance;

      warm *= brightness;
      cold *= brightness;

      snprintf_P(log_data, sizeof(log_data), PSTR("PCA9685: WARM: %d, COLD: %d"), (int)warm, (int)cold);
      AddLog(LOG_LEVEL_DEBUG);

      if (warm > PCA9685_DIMMER_MAX_DIMMER_VALUE) {
        warm = PCA9685_DIMMER_MAX_DIMMER_VALUE;
      }

      if (cold > PCA9685_DIMMER_MAX_DIMMER_VALUE) {
        cold = PCA9685_DIMMER_MAX_DIMMER_VALUE;
      }

      int pin_warm = Settings.pca685_dimmer.lamps[index].pins.ch0;
      int pin_cold = Settings.pca685_dimmer.lamps[index].pins.ch1;
      // Update shift to distribute power evenly
      PCA9685Dimmer_Channels.m_channel[pin_warm].m_shift = 0;
      PCA9685Dimmer_Channels.m_channel[pin_cold].m_shift = PCA9685_DIMMER_MAX_DIMMER_VALUE / 2;
      // Update brightness
      PCA9685Dimmer_Channels.m_channel[pin_warm].m_target = (uint16_t)warm;
      PCA9685Dimmer_Channels.m_channel[pin_cold].m_target = (uint16_t)cold;
      // Check if any ongoing power on/off transition happens
      boolean isPowerTransition = PCA9685Dimmer_Channels.m_channel[pin_warm].m_powerTransition ||
        PCA9685Dimmer_Channels.m_channel[pin_cold].m_powerTransition;

      // Update velocity if it's power transition or no power transition in progress
      if (isPower || !isPowerTransition) {
        PCA9685Dimmer_Channels.m_channel[pin_warm].m_velocity = velocity;
        PCA9685Dimmer_Channels.m_channel[pin_cold].m_velocity = velocity;
      }

      PCA9685Dimmer_updateActing((1 << pin_warm) | (1 << pin_cold));
      break;
    }

    // // A 3-channel RGB lamp
    // PCA9685_LAMP_RGB              = 3,
    // // A 4-channel RGBW strip
    // PCA9685_LAMP_RGBW             = 4
  }
}

void PCA9685Dimmer_Power(void) {
  uint8_t rpower = XdrvMailbox.index;
  int16_t source = XdrvMailbox.payload;

  // Invert prev pover value to process all bits during restart
  if (source == SRC_RESTART) {
    PCA9685Dimmer_power = ~rpower;
  }

  uint8_t prev_power = PCA9685Dimmer_power;
  PCA9685Dimmer_power = rpower;

  for (int i = 0; i < devices_present; ++i) {
    if (Settings.pca685_dimmer.lamps[i].type == PCA9685_LAMP_NONE) {
      break;
    }
    if ((prev_power & 1) != (rpower & 1)) {
      PCA9685Dimmer_applyColorBrightness(i, VELOCITY_ON_OFF, true);
    }
    rpower = rpower >> 1;
    prev_power = prev_power >> 1;
  }
}
//////////////////////////////////////////////////////////////////////
////////////////////////// Low level dimming /////////////////////////
//////////////////////////////////////////////////////////////////////
uint16_t PCA9685Dimmer_PWMCorrection(uint16_t logical) {
  if (!Settings.pca685_dimmer.cfg.use_corr) {
    return logical;
  }
  // 4096 logical levels maps to 256 correction table by stripping 4 LS bits
  // 4 lsb of range 0..15 used to interpolate value between correction table points (linear)
  uint16_t index = logical >> 4;
  if (index >= (CORRECTION_TABLE_LENGTH - 1)) {
    return PCA9685Dimmer_corr[CORRECTION_TABLE_LENGTH - 1];
  }
  uint16_t corrected_value = PCA9685Dimmer_corr[index];
  uint16_t next_value = PCA9685Dimmer_corr[index + 1];
  // cv = cv + (diff * 0..15) / 16 where diff = next_point - current_point
  corrected_value += (((int)next_value - (int)corrected_value) * ((int)(logical & 0xF))) / 0x10;
  return corrected_value;
}

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
      ch->m_powerTransition = 0;
      continue;
    }

    // If velocity value is zero (and values does not match), set value to target
    if (ch->m_velocity == 0) {
      ch->m_value = ch->m_target;
      ch->m_powerTransition = 0;
    } else {
      // Get diff between current value and target value
      int32_t diff = (int32_t)ch->m_value - (int32_t)ch->m_target;
      int32_t delta = ch->m_velocity;
      // If diff between target and value is less-eq that delta, update value to the target
      if (abs(diff) <= delta) {
        ch->m_value = ch->m_target;
        ch->m_powerTransition = 0;
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

    int pos = PCA9685Dimmer_Channels.m_buckets[bucket].pos;
    int len = PCA9685Dimmer_Channels.m_buckets[bucket].len;
    // Start transmission
    Wire.beginTransmission((uint8_t)USE_PCA9685_DIMMER_ADDR);
    // Select start address based on a bucket start
    Wire.write(PCA9685_DIMMER_REG_LED0_ON_L + (pos * 4));

    // Mode1 AI flag should be enabled
    for (int i = pos; i < (pos + len); ++i) {
      uint16_t value = PCA9685Dimmer_PWMCorrection(PCA9685Dimmer_Channels.m_channel[i].m_value);
      uint16_t start = PCA9685Dimmer_Channels.m_channel[i].m_shift;

      if (value >= PCA9685_DIMMER_MAX_DIMMER_VALUE) {
        // Special case, turn on, no PWM
        value = 0;
        start = PCA9685_DIMMER_MAX_DIMMER_VALUE;
      } else if (value == 0) {
        // Special case, turn off, no PWM
        value = PCA9685_DIMMER_MAX_DIMMER_VALUE;
        start = 0;
      } else {
        value += start;
        if (value >= PCA9685_DIMMER_MAX_DIMMER_VALUE) {
          value &= (PCA9685_DIMMER_MAX_DIMMER_VALUE - 1);
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
  PCA9685_CMND_RAW,
  // get/set lamps configuration
  PCA9685_CMND_LAMP,
  // get/set lamp brightness
  PCA9685_CMND_BRIGHT,
  // get/set lamp color temperature
  PCA9685_CMND_TEMPERATURE,
  // get/set lamp RGB color
  PCA9685_CMND_COLOR
};

const char g_PCA9685Dimmer_Commands[] PROGMEM =
  "P9SETUP|P9RAW|P9LAMP|P9BRIGHT|P9TEMP|P9COLOR";

int PCA9685Dimmer_GetParamCount(char* q, char delim) {
  if (XdrvMailbox.data_len == 0) {
    return 0;
  }
  int count = 1;
  for (; *q; count += (*q++ == delim));
  return count;
}

boolean PCA9685Dimmer_CheckParamCount(uint16_t count) {
  if (XdrvMailbox.data_len == 0) {
    return 0;
  }
  return count == PCA9685Dimmer_GetParamCount(XdrvMailbox.data, ',');
}


void PCA9685Dimmer_CommandSetupPrint(void) {
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{\"FREQ\":%d,\"INVRT\":%d,\"OUTDRV\":%s,\"CIE1931\":%d}"),
    Settings.pca685_dimmer.cfg.freq,
    Settings.pca685_dimmer.cfg.inv_out,
    (Settings.pca685_dimmer.cfg.totem_out ? "TOTEM" : "ODRAIN"),
    Settings.pca685_dimmer.cfg.use_corr
  );
}

void PCA9685Dimmer_CommandSetup(void) {
  if (!PCA9685Dimmer_CheckParamCount(4)) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR(D_JSON_ERROR));
    return;
  }

  char sub_string[XdrvMailbox.data_len + 1];
  char* freqStr = subStr(sub_string, XdrvMailbox.data, ",", 1);
  char* invStr = subStr(sub_string, XdrvMailbox.data, ",", 2);
  char* outDrvStr = subStr(sub_string, XdrvMailbox.data, ",", 3);
  char* useCorrection = subStr(sub_string, XdrvMailbox.data, ",", 4);

  Settings.pca685_dimmer.cfg.freq = atoi(freqStr);
  Settings.pca685_dimmer.cfg.inv_out = atoi(invStr);
  Settings.pca685_dimmer.cfg.totem_out = atoi(outDrvStr);
  Settings.pca685_dimmer.cfg.use_corr = atoi(useCorrection);

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
    int param_count = PCA9685Dimmer_GetParamCount(XdrvMailbox.data, ',');
    if (param_count == 0 || param_count > 2) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR(D_JSON_ERROR));
      return;
    }
    char sub_string[XdrvMailbox.data_len + 1];
    char* val = subStr(sub_string, XdrvMailbox.data, ",", 1);

    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_shift = 0;
    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_target = atoi(val);
    PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_velocity = 32;
    if (param_count == 2) {
      val = subStr(sub_string, XdrvMailbox.data, ",", 2);
      PCA9685Dimmer_Channels.m_channel[XdrvMailbox.index].m_velocity = atoi(val);
    }

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

void PCA9685Dimmer_CommandLamp_Print(void) {
  if (Settings.pca685_dimmer.lamps[0].type == PCA9685_LAMP_NONE) {
    return;
  }
  mqtt_data[0] = 0;

  for (int i = 0; i < 16; ++i) {
    if (Settings.pca685_dimmer.lamps[i].type == PCA9685_LAMP_NONE) {
      break;
    }
    if (strlen(mqtt_data) > 0) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,"), mqtt_data);
    }
    uint16_t velocityMs = Settings.pca685_dimmer.lamps[i].velocity * 50;

    switch (Settings.pca685_dimmer.lamps[i].type)
    {
      case PCA9685_LAMP_SINGLE:
        snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%sS=%d&V=%d"), mqtt_data,
          Settings.pca685_dimmer.lamps[i].pins.ch0,
          velocityMs);
        break;
      case PCA9685_LAMP_MULTIWHITE:
        snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%sMW=%d:%d&KW=%d&KC=%d&V=%d"), mqtt_data,
          Settings.pca685_dimmer.lamps[i].pins.ch0,
          Settings.pca685_dimmer.lamps[i].pins.ch1,
          Settings.pca685_dimmer.lamps[i].warm_temp * 100,
          Settings.pca685_dimmer.lamps[i].cold_temp * 100,
          velocityMs);
        break;
      case PCA9685_LAMP_RGB:
        snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%sRGB=%d:%d:%d&V=%d"), mqtt_data,
          Settings.pca685_dimmer.lamps[i].pins.ch0,
          Settings.pca685_dimmer.lamps[i].pins.ch1,
          Settings.pca685_dimmer.lamps[i].pins.ch2,
          velocityMs);
        break;
      case PCA9685_LAMP_RGBW:
        snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%sRGBW=%d:%d:%d:%d&V=%d"), mqtt_data,
          Settings.pca685_dimmer.lamps[i].pins.ch0,
          Settings.pca685_dimmer.lamps[i].pins.ch1,
          Settings.pca685_dimmer.lamps[i].pins.ch2,
          Settings.pca685_dimmer.lamps[i].pins.ch3,
          velocityMs);
        break;
      default:
        break;
    }
  }
  if (strlen(mqtt_data) == 0) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("Empty"));
  }
}


void PCA9685Dimmer_CommandLamp_SetPins(PCA9685_dimmer_lamp* lamp, char* value) {
  char buff[strlen(value) + 1];
  int params = PCA9685Dimmer_GetParamCount(value, ':');
  uint32_t channels = 0;
  for(int i = 0; i < params; ++i) {
    char* pinStr = subStr(buff, value, ":", i + 1);
    uint8_t val = atoi(pinStr) & 0xF;
    channels |= (val << (i * 4));
  }
  lamp->pins.raw = channels;
}

void PCA9685Dimmer_CommandLamp_Set(void) {
  memset(Settings.pca685_dimmer.lamps, 0, sizeof(PCA9685_dimmer_lamp) * 16);
  int lamps = PCA9685Dimmer_GetParamCount(XdrvMailbox.data, ',');
  if (lamps > 16) {
    return;
  }

  for(int lamp = 0; lamp < lamps; ++lamp) {
    char sub_string[XdrvMailbox.data_len + 1];
    char* lampStr = subStr(sub_string, XdrvMailbox.data, ",", lamp + 1);
    int params = PCA9685Dimmer_GetParamCount(lampStr, '&');

    // parse lamp
    for(int lampParam = 0; lampParam < params; ++lampParam) {
      char paramStr_buff[strlen(lampStr) + 1];
      char* paramStr = subStr(paramStr_buff, lampStr, "&", lampParam + 1);
      // Parse key-value
      char kv_buff[strlen(paramStr) + 1];
      char* keyStr = subStr(kv_buff, paramStr, "=", 1);
      char* valueStr = subStr(kv_buff, paramStr, "=", 2);

      if (!strncmp(keyStr, "S", 1)) {
        Settings.pca685_dimmer.lamps[lamp].type = PCA9685_LAMP_SINGLE;
        PCA9685Dimmer_CommandLamp_SetPins(&Settings.pca685_dimmer.lamps[lamp], valueStr);
      } else if (!strncmp(keyStr, "MW", 2)) {
        Settings.pca685_dimmer.lamps[lamp].type = PCA9685_LAMP_MULTIWHITE;
        PCA9685Dimmer_CommandLamp_SetPins(&Settings.pca685_dimmer.lamps[lamp], valueStr);
      } else if (!strncmp(keyStr, "RGBW", 4)) {
        Settings.pca685_dimmer.lamps[lamp].type = PCA9685_LAMP_RGBW;
        PCA9685Dimmer_CommandLamp_SetPins(&Settings.pca685_dimmer.lamps[lamp], valueStr);
      } else if (!strncmp(keyStr, "RGB", 3)) {
        Settings.pca685_dimmer.lamps[lamp].type = PCA9685_LAMP_RGB;
        PCA9685Dimmer_CommandLamp_SetPins(&Settings.pca685_dimmer.lamps[lamp], valueStr);
      } else if (!strncmp(keyStr, "KW", 2)) {
        Settings.pca685_dimmer.lamps[lamp].warm_temp = atoi(valueStr) / 100;
      } else if (!strncmp(keyStr, "KC", 2)) {
        Settings.pca685_dimmer.lamps[lamp].cold_temp = atoi(valueStr) / 100;
      } else if (!strncmp(keyStr, "V", 1)) {
        Settings.pca685_dimmer.lamps[lamp].velocity = atoi(valueStr) / 50;
      }
    }
  }
  // Try to set power devices w/o software restart
  PCA9685Dimmer_InitDevices();
  // Print current setup
  PCA9685Dimmer_CommandLamp_Print();
}

void PCA9685Dimmer_CommandLight(int command_code) {
  if (devices_present <= XdrvMailbox.index) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("INDEX ERROR"));
    return;
  }

  int param_count = PCA9685Dimmer_GetParamCount(XdrvMailbox.data, ',');

  if (param_count > 0) {
    char sub_string[XdrvMailbox.data_len + 1];
    char* paramValue = subStr(sub_string, XdrvMailbox.data, ",", 1);

    switch (command_code)
    {
      case PCA9685_CMND_BRIGHT:
        Settings.pca685_dimmer.lamps[XdrvMailbox.index].brightness = atoi(paramValue);
        break;

      case PCA9685_CMND_TEMPERATURE:
      {
        if (Settings.pca685_dimmer.lamps[XdrvMailbox.index].type != PCA9685_LAMP_MULTIWHITE) {
          snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("NOT MULTIWHITE"));
          return;
        }
        int color_temperature = atoi(paramValue);
        int warm = (int)Settings.pca685_dimmer.lamps[XdrvMailbox.index].warm_temp * 100;
        int cold = (int)Settings.pca685_dimmer.lamps[XdrvMailbox.index].cold_temp * 100;
        if (warm > color_temperature) { color_temperature = warm; }
        if (cold < color_temperature) { color_temperature = cold; }
        Settings.pca685_dimmer.lamps[XdrvMailbox.index].color_temperature = color_temperature;
        break;
      }

      case PCA9685_CMND_COLOR: {
        if (3 != PCA9685Dimmer_GetParamCount(paramValue, ':')) {
          snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("ERROR: need R:G:B value"));
          return;
        }
        char rgb_string[strlen(paramValue) + 1];
        uint32_t red = atoi(subStr(rgb_string, paramValue, ":", 1)) & 0xFF;
        uint32_t green = atoi(subStr(rgb_string, paramValue, ":", 2)) & 0xFF;
        uint32_t blue = atoi(subStr(rgb_string, paramValue, ":", 3)) & 0xFF;
        Settings.pca685_dimmer.lamps[XdrvMailbox.index].color.rgb = (red << 16) | (green << 8) | blue;

        if (Settings.pca685_dimmer.lamps[XdrvMailbox.index].type == PCA9685_LAMP_MULTIWHITE) {
          PCA9685Dimmer_applyRGBToColorTemperature(XdrvMailbox.index);
        }
        break;
      }
    }

    uint16_t velocity = Settings.pca685_dimmer.lamps[XdrvMailbox.index].velocity;
    if (param_count > 1) {
      char* paramVelocity = subStr(sub_string, XdrvMailbox.data, ",", 2);
      velocity = atoi(paramVelocity);
    }

    PCA9685Dimmer_applyColorBrightness(XdrvMailbox.index, velocity, false);
  }

  switch (command_code)
  {
    case PCA9685_CMND_BRIGHT:
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%d"), Settings.pca685_dimmer.lamps[XdrvMailbox.index].brightness);
      break;

    case PCA9685_CMND_TEMPERATURE:
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%d"), Settings.pca685_dimmer.lamps[XdrvMailbox.index].color_temperature);
      break;

    case PCA9685_CMND_COLOR:
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%d:%d:%d"),
        Settings.pca685_dimmer.lamps[XdrvMailbox.index].color.red,
        Settings.pca685_dimmer.lamps[XdrvMailbox.index].color.green,
        Settings.pca685_dimmer.lamps[XdrvMailbox.index].color.blue
      );
      break;
  }
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
    case PCA9685_CMND_LAMP:
      if (XdrvMailbox.data_len == 0) {
        PCA9685Dimmer_CommandLamp_Print();
      } else {
        PCA9685Dimmer_CommandLamp_Set();
      }
      return true;
    case PCA9685_CMND_BRIGHT:
    case PCA9685_CMND_TEMPERATURE:
    case PCA9685_CMND_COLOR:
      PCA9685Dimmer_CommandLight(command_code);
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
    case FUNC_SET_DEVICE_POWER:
      PCA9685Dimmer_Power();
      result = true;
      break;
    }
    default:
      break;
  }
  return result;
}

#endif // USE_PCA9685_DIMMER
#endif // USE_IC2


/*

////////////////////////////////////////////////////////////////////////////////////



//Get the maximum between R, G, and B
float tM = Math.Max(Ri, Math.Max(Gi, Bi));

//If the maximum value is 0, immediately return pure black.
if(tM == 0)
   { return new rgbwcolor() { r = 0, g = 0, b = 0, w = 0 }; }

//This section serves to figure out what the color with 100% hue is
float multiplier = 255.0f / tM;
float hR = Ri * multiplier;
float hG = Gi * multiplier;
float hB = Bi * multiplier;

//This calculates the Whiteness (not strictly speaking Luminance) of the color
float M = Math.Max(hR, Math.Max(hG, hB));
float m = Math.Min(hR, Math.Min(hG, hB));
float Luminance = ((M + m) / 2.0f - 127.5f) * (255.0f/127.5f) / multiplier;

//Calculate the output values
int Wo = Convert.ToInt32(Luminance);
int Bo = Convert.ToInt32(Bi - Luminance);
int Ro = Convert.ToInt32(Ri - Luminance);
int Go = Convert.ToInt32(Gi - Luminance);

//Trim them so that they are all between 0 and 255
if (Wo < 0) Wo = 0;
if (Bo < 0) Bo = 0;
if (Ro < 0) Ro = 0;
if (Go < 0) Go = 0;
if (Wo > 255) Wo = 255;
if (Bo > 255) Bo = 255;
if (Ro > 255) Ro = 255;
if (Go > 255) Go = 255;
return new rgbwcolor() { r = Ro, g = Go, b = Bo, w = Wo };

*/