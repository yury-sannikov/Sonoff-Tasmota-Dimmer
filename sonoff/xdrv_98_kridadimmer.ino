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
#define KRIDA_OFF_VELOCITY 100
// Dimmer on velocity.
#define KRIDA_ON_VELOCITY 60
// Dimmer limit augmentation velocity (in FUNC_EVERY_250_MSECOND units)
#define KRIDA_AUGMENTATION_VELOCITY_SLOW 1
// Dimmer value equivalent to the full off
#define KRIDA_FULL_OFF_VALUE 100
// Dimmer value equivalent to the full on
#define KRIDA_FULL_ON_VALUE 1
// If user on -> off -> on within FORCE_LIMIT_RESET_TIMEOUT_USEC, limit will be temporarely lifted for this ON
#define FORCE_LIMIT_RESET_TIMEOUT_USEC 2000000
// Initial old power state value
#define EMPTY_POWER_STATE 0xFF
// Dimmer value equivalent to the full on when SetOption34 set to `leak` limit
#define KRIDA_LEAKED_FULL_ON_VALUE 25
// Dimmer increase each N seconds
#define LEAK_INCREASE_SECONDS 5

#define KRIDA_I2C_ADDR 0x3F

const char S_DIMMER_COMMAND_VALUE[] PROGMEM = "{\"DIMMER%d\":%d,\"LIMIT%d\":%d,\"LEAK%d\":%d,\"VALUE%d\":%d}";


struct Velocty {
  // Current velocity value
  uint32_t m_value : 31;
  // Use 1 - FUNC_EVERY_250_MSECOND units, 0 - use FUNC_EVERY_50_MSECOND
  uint32_t m_slow : 1;
};

typedef struct {
  // Current dimmer value
  int32_t   m_value;
  // Current dimmer value target
  int32_t   m_target;
  // Current dimmer limit. It might be engaged during night time not to blind people or in a `cinema` mode
  int32_t   m_limit;
  // Curent velocity of m_value change
  Velocty   m_velocity;
  // Last turn-on time. Used to temporarely override m_limit value and do full-on
  unsigned long m_last_on_time;
  // Set to true on transition end to report power/dimmer status
  boolean   m_should_report_status;
  // Leak seconds counter
  uint8_t   m_seconds_counter;
} Dimmable;

Dimmable g_items[KRIDA_DEVICES];

// True if active 50 ms velocity for at least one of the dimmables
boolean g_active_50msec = 0;
// True if active 250 ms velocity for at least one of the dimmables
boolean g_active_250msec = 0;
// Holds previous power state value
uint8_t g_power = EMPTY_POWER_STATE;


void reportPowerDimmer() {
  char scommand[33];
  char stopic[TOPSZ];

  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{"));

  for(size_t i = 0; i < KRIDA_DEVICES; ++i) {
    if (!g_items[i].m_should_report_status) {
      continue;
    }
    g_items[i].m_should_report_status = false;

    GetPowerDevice(scommand, i + 1, sizeof(scommand), Settings.flag.device_index_enable);
    byte light_power = g_items[i].m_value == KRIDA_FULL_OFF_VALUE ? 0 : 1;

    if (strlen(mqtt_data) > 1) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,"), mqtt_data);
    }

    snprintf_P(log_data, sizeof(log_data), PSTR("KRI: report D%d, %s: %d, dim: %d"), i, scommand, light_power, g_items[i].m_value);
    AddLog(LOG_LEVEL_DEBUG_MORE);

    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s\"%s\":\"%s\",\"" D_CMND_DIMMER "%d\":%d"),
      mqtt_data, scommand, GetStateText(light_power), i + 1, KRIDA_FULL_OFF_VALUE - g_items[i].m_value);
  }
  if (strlen(mqtt_data) > 1) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s}"), mqtt_data);
    GetTopic_P(stopic, STAT, mqtt_topic, S_RSLT_RESULT);
    MqttPublish(stopic);
  }
}

boolean KridaSetPower()
{
  boolean status = false;

  uint8_t rpower = XdrvMailbox.index;
  int16_t source = XdrvMailbox.payload;

  uint8_t prev_power = g_power;
  g_power = rpower;

  // Tell to process all bits if prev_power has EMPTY_POWER_STATE value
  if (prev_power == EMPTY_POWER_STATE) {
    prev_power = ~rpower;
  }

  snprintf_P(log_data, sizeof(log_data), PSTR("KRI: SetDevicePower.rpower=%d, source=%d, prev_power=%d"), rpower, source, prev_power);
  AddLog(LOG_LEVEL_DEBUG);

  // Check if power state has been triggered due to dimmer animation completion
  if (source == SRC_LIGHT) {
    reportPowerDimmer();
    // Do not need to update target & velocity since this call was made by the transition end event
    return true;
  }

  // Go over devices and update their target values and velocities
  for(size_t i = 0; i < KRIDA_DEVICES; ++i) {

    // Process only updated bits
    if ((prev_power & 1) != (rpower & 1)) {
      if (rpower & 1) { // Turn power on
        unsigned long usec = micros();

        // Remove limit if user on->off->on within FORCE_LIMIT_RESET_TIMEOUT_USEC
        // This option allow to override max limit to get full bright of the lights
        int32_t limit = g_items[i].m_limit;
        if (usec - g_items[i].m_last_on_time < FORCE_LIMIT_RESET_TIMEOUT_USEC) {
          limit = KRIDA_FULL_ON_VALUE;
        }
        g_items[i].m_last_on_time = usec;

        g_items[i].m_target = limit;
        g_items[i].m_velocity.m_value = KRIDA_ON_VELOCITY;
        g_items[i].m_velocity.m_slow = 0;
        g_active_50msec = true;
      } else { // Turn power off
        g_items[i].m_target = KRIDA_FULL_OFF_VALUE;
        g_items[i].m_velocity.m_value = KRIDA_OFF_VELOCITY;
        g_items[i].m_velocity.m_slow = 0;
        g_active_50msec = true;
      }
      g_items[i].m_seconds_counter = 0;

      snprintf_P(log_data, sizeof(log_data), PSTR("KRI: D%d :: m_target=%d, m_value=%d"), i, g_items[i].m_target, g_items[i].m_value);
      AddLog(LOG_LEVEL_DEBUG_MORE);
    }

    rpower = rpower >> 1;
    prev_power = prev_power >> 1;
  }

  return true;
}

boolean KridaModuleSelected()
{
  if (!(pin[GPIO_I2C_SCL] < 99) || !(pin[GPIO_I2C_SDA] < 99)) {
    snprintf_P(log_data, sizeof(log_data), PSTR("KRI: I2C pins not set"));
    AddLog(LOG_LEVEL_DEBUG);
  }
  light_type = 0;
  devices_present = KRIDA_DEVICES;

  // Zero out all states
  memset(g_items, 0, sizeof(g_items));

  if (!Settings.param[P_TUYA_DIMMER_ID]) {
    Settings.param[P_TUYA_DIMMER_ID] = KRIDA_DEFAULT_MODE;
  }

  power_t pwr = Settings.power;
  // Set up values
  for(size_t i = 0; i < KRIDA_DEVICES; ++i) {
    g_items[i].m_target = (pwr & 1) ? KRIDA_FULL_ON_VALUE : KRIDA_FULL_OFF_VALUE;
    g_items[i].m_value = g_items[i].m_target;
    g_items[i].m_limit = KRIDA_FULL_ON_VALUE;
    g_items[i].m_seconds_counter = 0;
    pwr >>= 1;
  }

  snprintf_P(log_data, sizeof(log_data), PSTR("KRI: KridaModuleSelected called, pwr: %d"), Settings.power);
  AddLog(LOG_LEVEL_DEBUG);
  return true;
}


void KridaInit()
{
  snprintf_P(log_data, sizeof(log_data), "KRI: Init");
  AddLog(LOG_LEVEL_DEBUG);
  setDimmerValues();
}

/*
  setDimmerValue communicate through I2C interface and set actuall dimmer value to the KRIDA device
*/
void setDimmerValues() {
  uint8_t buffer[] = {
    (uint8_t)g_items[0].m_value,
    0x81,
    (uint8_t)g_items[1].m_value,
    0x82,
    (uint8_t)g_items[2].m_value,
    0x83,
    (uint8_t)g_items[3].m_value
  };
  I2cWriteBuffer(KRIDA_I2C_ADDR, 0x80, buffer, sizeof(buffer) / sizeof(uint8_t));
  // if (value % 5 == 0 || final) {
  //   snprintf_P(log_data, sizeof(log_data), PSTR("KRI: D%d <-- %d"), dimmerIndex, value);
  //   AddLog(LOG_LEVEL_DEBUG);
  // }
}

/*
  advanceDimmers - go over dimmers and update m_value toward m_target following m_velocity rule
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
    if (g_items[i].m_value == g_items[i].m_target) {
      continue;
    }
    uint8_t power_state = EMPTY_POWER_STATE;

    // If velocity value is zero (and values does not match), set value to target
    if (g_items[i].m_velocity.m_value == 0) {
      g_items[i].m_value = g_items[i].m_target;
      g_items[i].m_should_report_status = true;
      power_state = g_items[i].m_value == KRIDA_FULL_OFF_VALUE ? POWER_OFF_NO_STATE : POWER_ON_NO_STATE;
    } else {
      // Get diff between current value and target value
      int32_t diff = g_items[i].m_value - g_items[i].m_target;
      int32_t delta = g_items[i].m_velocity.m_value;
      // If diff between target and value is less-eq that delta, update value to the target
      if (abs(diff) <= delta) {
        g_items[i].m_value = g_items[i].m_target;
        g_items[i].m_should_report_status = true;
        power_state = g_items[i].m_value == KRIDA_FULL_OFF_VALUE ? POWER_OFF_NO_STATE : POWER_ON_NO_STATE;
      } else {
        // A case, where value far from target. Move value toward the target
        g_items[i].m_value = diff > 0 ? g_items[i].m_value - delta : g_items[i].m_value + delta;
        hasPending = true;
      }
    }

    // Turn off power if dimmer below or at 15% and moving toward 0%
    if (g_items[i].m_value >= 85 &&
      g_items[i].m_value < g_items[i].m_target &&
      g_items[i].m_value != KRIDA_FULL_OFF_VALUE) {

      g_items[i].m_value = KRIDA_FULL_OFF_VALUE;
      g_items[i].m_target = KRIDA_FULL_OFF_VALUE;
      power_state = POWER_OFF_NO_STATE;
    }

    // Update internal power state w/o reporting
    if (power_state != EMPTY_POWER_STATE) {
      ExecuteCommandPower(i + 1, power_state, SRC_LIGHT);
    }

  }
  // Update actual dimmer value
  setDimmerValues();
  return hasPending;
}


boolean updateDimmerTargetAndLimit(uint16_t index, uint16_t value, uint16_t limit, struct Velocty velocty) {
  if (index >= KRIDA_DEVICES) {
    return false;
  }

  if (value <= KRIDA_FULL_OFF_VALUE) {
    // Krida bug
    if (value == 0) {
      value = KRIDA_FULL_ON_VALUE;
    }

    snprintf_P(log_data, sizeof(log_data), PSTR("KRI: update target D%d << %d"),
      index, value);
    AddLog(LOG_LEVEL_DEBUG);
    // Obey limit
    if (value < g_items[index].m_limit) {
      value = g_items[index].m_limit;
    }
    g_items[index].m_target = value;
  }
  if (limit <= KRIDA_FULL_OFF_VALUE) {
    // Krida bug
    if (limit == 0) {
      limit = KRIDA_FULL_ON_VALUE;
    }

    snprintf_P(log_data, sizeof(log_data), PSTR("KRI: update limit D%d <| %d"),
      index, limit);
    AddLog(LOG_LEVEL_DEBUG);

    g_items[index].m_limit = limit;
    if (g_items[index].m_target != KRIDA_FULL_OFF_VALUE) {
      g_items[index].m_target = limit;
    }
  }

  g_items[index].m_seconds_counter = 0;
  g_items[index].m_velocity = velocty;
  if (velocty.m_slow) {
    g_active_250msec = true;
  } else {
    g_active_50msec = true;
  }
  return true;
}

enum KridaCommands {
  KRI_CMND_DIMMER, KRI_CMND_DIMLIM };
const char g_kridaCommands[] PROGMEM =
  D_CMND_DIMMER "|DIMLIM";

boolean KridaCommand()
{
  char command [CMDSZ];
  int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic, g_kridaCommands);
  if (-1 == command_code) {
    return false;
  }

  snprintf_P(log_data, sizeof(log_data), PSTR("KRI: cmd %s"), XdrvMailbox.topic);
  AddLog(LOG_LEVEL_DEBUG);
  uint16_t index = XdrvMailbox.index - 1;
  if (strlen(XdrvMailbox.data) > 0) {
    switch (command_code) {
      case KRI_CMND_DIMMER:
        updateDimmerTargetAndLimit(index,
          100 - XdrvMailbox.payload16,
          -1,
          { .m_value = KRIDA_ON_VELOCITY, .m_slow = 0 }
        );
        break;
      case KRI_CMND_DIMLIM:
        updateDimmerTargetAndLimit(index,
          -1,
          100 - XdrvMailbox.payload16,
          { .m_value = KRIDA_AUGMENTATION_VELOCITY_SLOW, .m_slow = 1 }
        );
        break;
      default:
        return false;
    }
  }
  snprintf_P(mqtt_data, sizeof(mqtt_data), S_DIMMER_COMMAND_VALUE,
    XdrvMailbox.index, 100 - g_items[index].m_target,
    XdrvMailbox.index, 100 - g_items[index].m_limit,
    XdrvMailbox.index, ((Settings.param[P_TUYA_DIMMER_ID] >> index) & 1),
    XdrvMailbox.index, 100 - g_items[index].m_value
  );
  return true;
}

// If SetOption34 bit set for the dimmer and it's `on`, it means that every second value should get increased toward
// max value discarding limit.
// Used to work as a night mode bathrooms/stairs
void leakToFullOn() {
  uint8_t param = Settings.param[P_TUYA_DIMMER_ID];
  uint8_t mask = 1;
  for(size_t i = 0; i < KRIDA_DEVICES; ++i) {
    if (g_items[i].m_target != KRIDA_FULL_OFF_VALUE
      && (param & mask)
      && (g_items[i].m_target == g_items[i].m_limit)) {
      if (g_items[i].m_value > KRIDA_LEAKED_FULL_ON_VALUE) {

        g_items[i].m_seconds_counter += 1;
        if (g_items[i].m_seconds_counter >= LEAK_INCREASE_SECONDS) {
          g_items[i].m_seconds_counter = 0;
          g_items[i].m_value -= 1;
        }
      }
    }
    mask <<= 1;
  }
  setDimmerValues();
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

#define XDRV_98
boolean skip_50 = true;

boolean Xdrv98(byte function)
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
        if (skip_50) {
          skip_50 = false;
          break;
        }
        skip_50 = true;
        if (g_active_50msec) {
          g_active_50msec = advanceDimmers(false);
        }
        break;
      case FUNC_EVERY_250_MSECOND:
        if (g_active_250msec) {
          g_active_250msec = advanceDimmers(true);
        }
        break;
      case FUNC_EVERY_SECOND:
        leakToFullOn();
        break;
      case FUNC_COMMAND:
        result = KridaCommand();
        break;
    }
  }
  return result;
}

//#endif  // USE_KRIDA_DIMMER
