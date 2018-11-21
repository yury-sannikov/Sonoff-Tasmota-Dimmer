/*
  Krida dimmer support for Sonoff-Tasmota
*/

// #ifdef USE_KRIDA_DIMMER

#ifndef KRIDA_DEFAULT_MODE
#define KRIDA_DEFAULT_MODE    0
#endif

// Amount of supportded devices
#define KRIDA_DEVICES 4
// Dimmer off velocity. Each FUNC_EVERY_50_MSECOND a KRIDA_OFF_VELOCITY will be incurred to the value until KRIDA_FULL_OFF_VALUE reached
#define KRIDA_OFF_VELOCITY 10
// Dimmer on velocity.
#define KRIDA_ON_VELOCITY 5
// Dimmer limit augmentation velocity (in FUNC_EVERY_250_MSECOND units)
#define KRIDA_AUGMENTATION_VELOCITY_SLOW 5
// Dimmer value equivalent to the full off
#define KRIDA_FULL_OFF_VALUE 100
// Dimmer value equivalent to the full on
#define KRIDA_FULL_ON_VALUE 0
//
#define FORCE_LIMIT_RESET_TIMEOUT_USEC 2000000

typedef union {
  uint32_t  m_raw;
    struct {
      // Current velocity value
      uint32_t m_value : 31;
      // Use 1 - FUNC_EVERY_250_MSECOND units, 0 - use FUNC_EVERY_50_MSECOND
      uint32_t m_slow : 1;
    };
} Velocty;

typedef struct {
  // Current dimmer value
  int32_t   m_value;
  // Current dimmer value target
  int32_t   m_taget;
  // Current dimmer limit. It might be engaged during night time not to blind people or in a `cinema` mode
  int32_t   m_limit;
  // Curent velocity of m_value change
  Velocty   m_velocity;
  // Last turn-on time. Used to temporarely override m_limit value and do full-on
  unsigned long m_last_on_time;
} Dimmable;

Dimmable g_items[KRIDA_DEVICES];

// True if active 50 ms velocity for at least one of the dimmables
volatile boolean g_active_50msec = 0;
// True if active 250 ms velocity for at least one of the dimmables
volatile boolean g_active_250msec = 0;

boolean KridaSetPower()
{
  boolean status = false;

  uint8_t rpower = XdrvMailbox.index;
  int16_t source = XdrvMailbox.payload;

  snprintf_P(log_data, sizeof(log_data), PSTR("KRI: SetDevicePower.rpower=%d, source=%d"), rpower, source);
  AddLog(LOG_LEVEL_DEBUG);

  // Go over devices and update their target values and velocities
  for(size_t i = 0; i < KRIDA_DEVICES; ++i) {
    if (rpower & 1) { // Turn power on
      unsigned long usec = micros();

      // Remove limit if user on->off->on within FORCE_LIMIT_RESET_TIMEOUT_USEC
      // This option allow to override max limit to get full bright of the lights
      int32_t limit = g_items[i].m_limit;
      if (usec - g_items[i].m_last_on_time < FORCE_LIMIT_RESET_TIMEOUT_USEC) {
        limit = KRIDA_FULL_ON_VALUE;
      }
      g_items[i].m_last_on_time = usec;

      g_items[i].m_taget = limit;
      g_items[i].m_velocity.m_value = KRIDA_ON_VELOCITY;
      g_items[i].m_velocity.m_slow = 0;
      g_active_50msec = true;
    } else { // Turn power off
      g_items[i].m_taget = KRIDA_FULL_OFF_VALUE;
      g_items[i].m_velocity.m_value = KRIDA_OFF_VELOCITY;
      g_items[i].m_velocity.m_slow = 0;
      g_active_50msec = true;
    }
    snprintf_P(log_data, sizeof(log_data), PSTR("KRI: D%d :: m_target=%d, m_value=%d"), i, g_items[i].m_taget, g_items[i].m_value);
    AddLog(LOG_LEVEL_DEBUG);

    rpower = rpower >> 1;
  }

  return true;
}

boolean KridaModuleSelected()
{
  if (!(pin[GPIO_I2C_SCL] < 99) || !(pin[GPIO_I2C_SDA] < 99)) {
    snprintf_P(log_data, sizeof(log_data), "KRI: I2C pins not set");
    AddLog(LOG_LEVEL_DEBUG);
  }
  light_type = LT_BASIC;
  devices_present = KRIDA_DEVICES;

  snprintf_P(log_data, sizeof(log_data), "KRI: KridaModuleSelected called");
  AddLog(LOG_LEVEL_DEBUG);
  return true;
}


void KridaInit()
{
  // Reuse SetOption34 for Krida
  if (!Settings.param[P_TUYA_DIMMER_ID]) {
    Settings.param[P_TUYA_DIMMER_ID] = KRIDA_DEFAULT_MODE;
  }

  // Zero out all states
  memset(g_items, 0, sizeof(g_items));

  // Set up values
  for(size_t i = 0; i < KRIDA_DEVICES; ++i) {
    g_items[i].m_value = KRIDA_FULL_OFF_VALUE;
    g_items[i].m_taget = KRIDA_FULL_OFF_VALUE;
    g_items[i].m_limit = KRIDA_FULL_ON_VALUE;
  }

  snprintf_P(log_data, sizeof(log_data), "KRI: Init");
  AddLog(LOG_LEVEL_DEBUG);
}

/*
  setDimmerValue communicate through I2C interface and set actuall dimmer value to the KRIDA device
*/
void setDimmerValue(size_t dimmerIndex, int32_t value, boolean final) {
  // TODO:
  if (value % 10 == 0 || final) {
    snprintf_P(log_data, sizeof(log_data), PSTR("KRI: D%d <-- %d"), dimmerIndex, value);
    AddLog(LOG_LEVEL_DEBUG);
  }
}

/*
  advanceDimmers - go over dimmers and update m_value toward m_taget following m_velocity rule
  boolean isSlow - true: should advance 250ms timers, false - 50ms timers
  return:
    true - keep updating correspontent timer set
    false - all transitions for the current timer stopped
*/
boolean advanceDimmers(boolean isSlow) {
  boolean hasPending = false;
  for(size_t i = 0; i < KRIDA_DEVICES; ++i) {
    // process only slow or fast timers
    if (g_items[i].m_velocity.m_slow != isSlow) {
      continue;
    }

    // If value match target, do nothing
    if (g_items[i].m_value == g_items[i].m_taget) {
      continue;
    }

    // If velocity value is zero (and values does not match), set value to target
    if (g_items[i].m_velocity.m_value == 0) {
      g_items[i].m_value = g_items[i].m_taget;
    } else {
      // Get diff between current value and target value
      int32_t diff = g_items[i].m_value - g_items[i].m_taget;
      int32_t delta = g_items[i].m_velocity.m_value;
      // If diff between target and value is less-eq that delta, update value to the target
      if (abs(diff) <= delta) {
        g_items[i].m_value = g_items[i].m_taget;
      } else {
        // A case, where value far from target. Move value toward the target
        g_items[i].m_value = diff > 0 ? g_items[i].m_value - delta : g_items[i].m_value + delta;
        hasPending = true;
      }
    }
    // Update actual dimmer value
    setDimmerValue(i, g_items[i].m_value, g_items[i].m_value == g_items[i].m_taget);
  }
  return hasPending;
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

#define XDRV_18

boolean Xdrv18(byte function)
{
  boolean result = false;

  if (KRIDA_DIMMER == Settings.module) {
    switch (function) {
      case FUNC_MODULE_INIT:
        result = KridaModuleSelected();
        break;
      case FUNC_INIT:
        KridaInit();
        break;
      case FUNC_SET_DEVICE_POWER:
        result = KridaSetPower();
        break;
      case FUNC_EVERY_50_MSECOND:
        if (g_active_50msec) {
          g_active_50msec = advanceDimmers(false);
        }
        break;
      case FUNC_EVERY_250_MSECOND:
        if (g_active_250msec) {
          g_active_250msec = advanceDimmers(true);
        }
        break;
    }
  }
  return result;
}

//#endif  // USE_KRIDA_DIMMER
