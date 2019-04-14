#ifndef XDRV_95
#define XDRV_95            95

// Enable flag. Will be disabled if no siren GPIO has been assigned
uint8_t drv_siren_enabled = 1;

typedef enum SirenStatus {
  sirenOff = 0,
  sirenMute,
  sirenWarning,
  sirenAlarm,
  sirenMAX
} SirenStatus_t;

typedef enum SirenGas {
  gasNone = 0,
  gasCO,
  gasCO2,
  gasFlammable,
  gasMAX
} SirenGas_t;

// All patterns started with enabled alarm.
// Each pattern item eq to pause in 50ms. After alarm getting inverted
const uint16_t drv_siren_CO_WARNING_pattern[] = {
  1,                         // On for 50ms
  20 * 60 * 2                // Pause 2 minutes
};

const uint16_t drv_siren_CO2_WARNING_pattern[] = {
  1,                         // On for 50ms
  20 * 60 * 30               // Pause 30 minutes
};

const uint16_t drv_siren_CO_ALARM_pattern[] = {
  10, 4, 10, 20,              // 2 beeps
  10, 4, 4, 4, 10, 4, 4, 10,  // Morze C
  10, 4, 10, 4, 10,           // Morze O
  20 * 30                     // 30 sec pause
};

const uint16_t drv_siren_GAS_WARNING_pattern[] = {
  1, 20, 1, 20, 1,           // On for 50ms
  20 * 60 * 2                // Pause 2 minutes
};

const uint16_t drv_siren_GAS_ALARM_pattern[] = {
  10, 4, 10, 4, 10, 20,       // 3 beeps
  10, 4, 10, 4, 4, 10,        // Morze G
  4, 4, 10, 10,               // Morze A
  4, 4, 4, 4, 4, 10,          // Morze S
  20 * 30                     // 30 sec pause
};

const uint16_t drv_siren_TEST_pattern[] = {
  10, 4, 2, 4, 2, 2, 2, 2, 2, 4, 10, 20 * 10
};

const uint16_t* drv_siren_Selected_Pattern = NULL;
int drv_siren_Selected_Pattern_Index = 0;
int drv_siren_Selected_Pattern_Length = 0;


SirenStatus_t drv_siren_status = sirenOff;
SirenGas_t drv_siren_gas = gasNone;

#define D_LOG_PREFIX "SRN:"
char siren_topic[] = "siren";


const char drv_siren_commands[] PROGMEM = "off|mute|warning|alarm|";
const char drv_siren_gases[] PROGMEM = "none|co|co2|gas|";


void drv_siren_Init() {
  drv_siren_enabled = 0;

  if (pin[GPIO_SIREN] < 99) {
    drv_siren_enabled = 1;
    pinMode(pin[GPIO_SIREN], OUTPUT);
    digitalWrite(pin[GPIO_SIREN], 0);
  } else if (pin[GPIO_SIREN_WITH_CANCEL] < 99) {
    // GPIO_SIREN_WITH_CANCEL can be only on IO15 port to use INPUT_PULLDOWN_16
    // In this mode driver will output siren pulse then go to input mode INPUT_PULLDOWN_16 and check if
    // it's UP to cancel siren
    drv_siren_enabled = 1;
    pinMode(pin[GPIO_SIREN_WITH_CANCEL], OUTPUT);
    digitalWrite(pin[GPIO_SIREN_WITH_CANCEL], 0);
  }

  // Set cancel button input pin
  if (drv_siren_enabled && pin[GPIO_SIREN_CANCEL] < 99) {
    pinMode(pin[GPIO_SIREN_CANCEL], INPUT_PULLUP);
  }

  if (drv_siren_enabled) {
    AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "Siren enabled"));
  } else {
    AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "Siren disabled"));
  }
}

unsigned long drv_siren_50msecond = 0;

int Siren_GetStatus (void) {
  return drv_siren_status;
}
int Siren_GetGas (void) {
  return drv_siren_gas;
}

void Siren_UnmuteReset() {
  drv_siren_status = sirenOff;
  drv_siren_gas = gasNone;
}

void Siren_SetStatusGas(int status, int gas, bool fromOther) {
  if (status == sirenMute) {
      // Log mute time to automatically disable it in the morning
      // TODO: g_mute_time = now()
  }

  if (fromOther) {
    // Update status if foreign status have higher priority than ours
    // or is's off/mute.
    // off/mute never send by itself, but as a result of user interaction or condition has been improved
    if (status > drv_siren_status || status == sirenOff || status == sirenMute) {
      drv_siren_status = (SirenStatus_t)status;
      drv_siren_gas = (SirenGas_t)gas;
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " applying foreign status: %d, gas: %d"), status, gas);
    } else {
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " ignoring foreign status: %d, gas: %d"), status, gas);
    }
  } else {
    // Do not update status for muted siren
    if (drv_siren_status != sirenMute) {
      drv_siren_status = (SirenStatus_t)status;
    } else {
      AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " ignore muted"));
    }
    // Update gas for muted
    drv_siren_gas = (SirenGas_t)gas;
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " applying local status: %d, gas: %d"), status, gas);
  }

  drv_siren_Selected_Pattern_Index = 0;
  drv_siren_Selected_Pattern_Length = 0;
  drv_siren_Selected_Pattern = NULL;
  drv_siren_50msecond = 9;
  switch (status)
  {
    case sirenMute:
    case sirenOff:
      drv_siren_OnOff(false);
    break;
    case sirenWarning:
    {
      switch(drv_siren_gas) {
        case gasCO:
          drv_siren_Selected_Pattern = drv_siren_CO_WARNING_pattern;
          drv_siren_Selected_Pattern_Length = sizeof(drv_siren_CO_WARNING_pattern) / sizeof(uint16_t);
        break;
        case gasCO2:
          drv_siren_Selected_Pattern = drv_siren_CO2_WARNING_pattern;
          drv_siren_Selected_Pattern_Length = sizeof(drv_siren_CO2_WARNING_pattern) / sizeof(uint16_t);
        break;
        case gasFlammable:
          drv_siren_Selected_Pattern = drv_siren_GAS_WARNING_pattern;
          drv_siren_Selected_Pattern_Length = sizeof(drv_siren_GAS_WARNING_pattern) / sizeof(uint16_t);
        break;
      }
    }
    break;
    case sirenAlarm: {
      switch(drv_siren_gas) {
        case gasCO:
          drv_siren_Selected_Pattern = drv_siren_CO_ALARM_pattern;
          drv_siren_Selected_Pattern_Length = sizeof(drv_siren_CO_ALARM_pattern) / sizeof(uint16_t);
        break;
        case gasCO2:
          drv_siren_Selected_Pattern = drv_siren_TEST_pattern;
          drv_siren_Selected_Pattern_Length = sizeof(drv_siren_TEST_pattern) / sizeof(uint16_t);
        break;
        case gasFlammable:
          drv_siren_Selected_Pattern = drv_siren_GAS_ALARM_pattern;
          drv_siren_Selected_Pattern_Length = sizeof(drv_siren_GAS_ALARM_pattern) / sizeof(uint16_t);
        break;
      }
    }
    break;
    default:
      break;
  }

  // Turn siren on and set first timer
  if (drv_siren_Selected_Pattern != NULL) {
    drv_siren_OnOff(true);
    drv_siren_50msecond = millis() + (50ul * (unsigned long)drv_siren_Selected_Pattern[drv_siren_Selected_Pattern_Index++]);
  }
  if (!fromOther) {
    // Send status update
    drv_siren_MqttSend();
  }
}

void drv_siren_OnOff(bool isOn) {
  if (pin[GPIO_SIREN] < 99) {
    digitalWrite(pin[GPIO_SIREN], isOn ? 1 : 0);
  } else if (pin[GPIO_SIREN_WITH_CANCEL] < 99) {
    pinMode(pin[GPIO_SIREN_WITH_CANCEL], OUTPUT);
    digitalWrite(pin[GPIO_SIREN_WITH_CANCEL], isOn ? 1 : 0);
  }
}


void drv_siren_Loop() {
  if (drv_siren_Selected_Pattern == NULL) {
    return;
  }
  if (TimeReached(drv_siren_50msecond)) {
    // Expired odd index always turn siren off
    drv_siren_OnOff((drv_siren_Selected_Pattern_Index % 2) == 0);
    // Extend timer
    drv_siren_50msecond = millis() + (50ul * (unsigned long)drv_siren_Selected_Pattern[drv_siren_Selected_Pattern_Index++]);
    // Loop siren pattern and turn it off
    if (drv_siren_Selected_Pattern_Index >= drv_siren_Selected_Pattern_Length) {
      drv_siren_Selected_Pattern_Index = 0;
      drv_siren_OnOff(false);
    }
  }
}

void drv_siren_MqttSubscribe(void) {
  // Report status on reconnect
  if (drv_siren_status == sirenWarning || drv_siren_status == sirenAlarm) {
    drv_siren_MqttSend();
  }

  char stopic[TOPSZ];
  snprintf_P(stopic, sizeof(stopic), PSTR("%s/#"), siren_topic);
  MqttSubscribe(stopic);
}

void drv_siren_MqttSend(void) {
  // Get command
  char cmd_to_send[25];
  GetTextIndexed(cmd_to_send, sizeof(cmd_to_send), Siren_GetStatus(), drv_siren_commands);

  // Get topic - siren/{command}
  char topic_to_send[25];
  snprintf_P(topic_to_send, sizeof(topic_to_send), PSTR("%s/%s"), siren_topic, cmd_to_send);

  // Get Gas as payload
  GetTextIndexed(mqtt_data, sizeof(mqtt_data), Siren_GetGas(), drv_siren_gases);

  AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " send: %s %s"), topic_to_send, mqtt_data);

  // Send gas value to the command topic
  MqttPublish(topic_to_send);
}

bool drv_siren_MqttData(void)
{
  if (strncmp(XdrvMailbox.topic, siren_topic, strlen(siren_topic))) {
    return false;
  }
  AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " MQTT DATA:%s TOPIC:%s, LEN:%d"), XdrvMailbox.data, XdrvMailbox.topic, XdrvMailbox.data_len);

  char command[CMDSZ];
  int status_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic + strlen(siren_topic) + 1, drv_siren_commands);
  if (-1 == status_code) {
    AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "Unknown command"));
    return false;
  }

  char gas[CMDSZ];
  int gas_code = GetCommandCode(gas, sizeof(gas), XdrvMailbox.data, drv_siren_gases);
  if (-1 == gas_code) {
    AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "Unknown gas"));
    return false;
  }

  if (status_code != drv_siren_status || gas_code != drv_siren_gas) {
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " passing foreign status: %d, gas: %d"), status_code, gas_code);
    Siren_SetStatusGas(status_code, gas_code, true);
  }
  return true;
}


/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv95(uint8_t function)
{
  bool result = false;
  if (!drv_siren_enabled) {
    return result;
  }

  switch (function) {
    case FUNC_INIT:
      drv_siren_Init();
    break;
    case FUNC_LOOP:
      drv_siren_Loop();
    break;
    case FUNC_MQTT_SUBSCRIBE:
      drv_siren_MqttSubscribe();
      break;
    case FUNC_MQTT_DATA:
      result = drv_siren_MqttData();
      break;
  }
  return result;
}

#endif // XDRV_95