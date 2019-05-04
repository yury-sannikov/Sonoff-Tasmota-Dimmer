#ifndef XDRV_95
#define XDRV_95            95

// Enable flag. Will be disabled if no siren GPIO has been assigned
uint8_t drv_siren_enabled = 1;

typedef enum SirenStatus {
  sirenOff = 0,
  sirenWarning,
  sirenAlarm,
  sirenMute,
  sirenUnmute,
} SirenStatus_t;

typedef enum SirenGas {
  gasCO = 0,
  gasCO2,
  gasFlammable,
  gasMax
} SirenGas_t;


uint8_t drv_siren_statuses[gasMax] = {sirenOff, sirenOff, sirenOff};
bool drv_siren_locals[gasMax] = {true, true, true};


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


// An identifier of the siren module (mac address)
char drv_siren_self[32] = { 0 };
// Time when siren was muted
unsigned long drv_siren_mute_time = 0;
// Time when status update has been sent
unsigned long drv_siren_status_time = 0;

#define D_LOG_PREFIX "SRN:"
char siren_topic[] = "siren";

const char drv_siren_commands[] PROGMEM = "off|warning|alarm|mute|unmute|";
const char drv_siren_gases[] PROGMEM = "co|co2|ch4|";


void drv_siren_Init() {
  drv_siren_enabled = 0;
  drv_siren_mute_time = 0;
  drv_siren_Selected_Pattern = NULL;
  drv_siren_Selected_Pattern_Index = 0;
  drv_siren_Selected_Pattern_Length = 0;

  memset(drv_siren_statuses, 0, sizeof(drv_siren_statuses));
  for (int i = 0; i < gasMax; ++i) {
    drv_siren_statuses[i] = sirenOff;
    drv_siren_locals[i] = true;

  }

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

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  strncpy(drv_siren_self, mac.c_str(), sizeof(drv_siren_self));
}

unsigned long drv_siren_50msecond = 0;

int Siren_GetStatus (int gas) {
  return drv_siren_statuses[gas];
}

void Siren_UnmuteReset(bool isUserAction) {
  for (int i = 0; i < gasMax; i++) {
    drv_siren_statuses[i] = sirenOff;
    drv_siren_locals[i] = true;
    if (isUserAction) {
      drv_siren_MqttSend(sirenUnmute, i);
    }
  }
  drv_siren_mute_time = 0;
  drv_siren_status_time = 0;
  Siren_UpdatePattern();
}

void Siren_Mute() {
  for (int i = 0; i < gasMax; i++) {
    Siren_SetStatusGas(sirenMute, i, true);
  }
}

// Decide if MQTT status update should be sent.
// Send on status change and each minute
bool Siren_ShouldSendUpdate(int status, int gas) {
  if (status != drv_siren_statuses[gas] || (millis() - drv_siren_status_time) > 120000) {
    drv_siren_status_time = millis();
    return true;
  }
  return false;
}

void Siren_SetStatusGas(int status, int gas, bool isLocal) {
  // Store muted time if muted
  if (status == sirenMute && drv_siren_statuses[gas] != sirenMute) {
    drv_siren_mute_time = millis();
    drv_siren_statuses[gas] = sirenMute;

    // Broadcast local update
    if (isLocal) {
      drv_siren_MqttSend(sirenMute, gas);
    }
  }

  // Do nothing if muted
  if (drv_siren_statuses[gas] == sirenMute) {
    if (isLocal || status != sirenUnmute) {
      Siren_UpdatePattern();
      return;
    }
  }

  // Convert unmute to off. Should come from remote only
  if (status == sirenUnmute) {
    if (isLocal) {
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "Siren_UnmuteReset should be called locally instead of passing sirenUnmute status"));
      return;
    }
    drv_siren_mute_time = 0;
    status = sirenOff;
    drv_siren_statuses[gas] = (SirenStatus_t)status;
  }

  if (drv_siren_locals[gas] == isLocal) {
    bool shouldSend = Siren_ShouldSendUpdate(status, gas);
    // Do not negotiate if siren source status matches
    // For instance, if it was local alarm and become local warning, update to warning
    // If it was remote warning and going a remote off, turn it off
    if (drv_siren_statuses[gas] != status) {
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "apply %s status %d, gas %d"), isLocal ? "local" : "remote", status, gas);
    }
    drv_siren_statuses[gas] = (SirenStatus_t)status;
    Siren_UpdatePattern();

    // Broadcast local-only update
    if (isLocal && shouldSend) {
      drv_siren_MqttSend(status, gas);
    }
  } else {
    // Case when local and remote status does not match
    if (status > drv_siren_statuses[gas]) {
      AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "status %d beats %d. Came from %s"), status, drv_siren_statuses[gas], isLocal ? "local" : "remote");
      // Incoming status has higher priority (mute is highest)
      drv_siren_statuses[gas] = (SirenStatus_t)status;
      drv_siren_locals[gas] = isLocal;
      Siren_UpdatePattern();

      // Broadcast if local status update win
      if (isLocal) {
        drv_siren_MqttSend(status, gas);
      }
    }
  }
}

int last_status = sirenOff;
int last_gas = -1;
void Siren_UpdatePattern() {
  // Calculate most important status and it's gas
  int status = sirenOff;
  int gas = gasMax;
  for (int i = 0; i < gasMax; i++) {
    if (drv_siren_statuses[i] > status) {
      status = drv_siren_statuses[i];
      gas = i;
    }
  }
  // If status and gas was the same, do nothing not to break sound patterns
  if (status == last_status && gas == last_gas) {
    return;
  }

  last_status = status;
  last_gas = gas;

  drv_siren_Selected_Pattern_Index = 0;
  drv_siren_Selected_Pattern_Length = 0;
  drv_siren_Selected_Pattern = NULL;
  drv_siren_50msecond = 0;

  switch (status)
  {
    case sirenMute:
    case sirenUnmute:
    case sirenOff:
      drv_siren_OnOff(false);
      drv_siren_Selected_Pattern = NULL;
      drv_siren_Selected_Pattern_Length = 0;
    break;
    case sirenWarning:
    {
      switch(gas) {
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
      switch(gas) {
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
  char stopic[TOPSZ];
  snprintf_P(stopic, sizeof(stopic), PSTR("%s/#"), siren_topic);
  MqttSubscribe(stopic);
}

void drv_siren_MqttSend(int status, int gas) {
  // Get command
  char cmd_to_send[25];
  GetTextIndexed(cmd_to_send, sizeof(cmd_to_send), status, drv_siren_commands);

  // Get topic - siren/{command}
  char topic_to_send[25];
  snprintf_P(topic_to_send, sizeof(topic_to_send), PSTR("%s/%s"), siren_topic, cmd_to_send);

  // Get Gas as payload
  char gas_to_send[25];
  GetTextIndexed(gas_to_send, sizeof(gas_to_send), gas, drv_siren_gases);

  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,%s"), gas_to_send, drv_siren_self);

  AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " send: %s %s"), topic_to_send, mqtt_data);

  // Send gas value to the command topic
  MqttPublish(topic_to_send);
}

bool drv_siren_MqttData(void)
{
  if (strncmp(XdrvMailbox.topic, siren_topic, strlen(siren_topic))) {
    return false;
  }
  // AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " MQTT DATA:%s TOPIC:%s, LEN:%d"), XdrvMailbox.data, XdrvMailbox.topic, XdrvMailbox.data_len);

  char command[CMDSZ];
  int status_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic + strlen(siren_topic) + 1, drv_siren_commands);
  if (-1 == status_code) {
    AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "Unknown command"));
    return false;
  }

  char sub_string[XdrvMailbox.data_len + 1];
  char* gas_str = subStr(sub_string, XdrvMailbox.data, ",", 1);
  char* source_str = subStr(sub_string, XdrvMailbox.data, ",", 2);

  // Skip message sent by itself
  if (strncmp(source_str, drv_siren_self, sizeof(drv_siren_self)) == 0) {
    return false;
  }

  char gas[CMDSZ];
  int gas_code = GetCommandCode(gas, sizeof(gas),gas_str, drv_siren_gases);
  if (-1 == gas_code) {
    AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX "Unknown gas"));
    return false;
  }

  if (drv_siren_statuses[gas_code] != status_code) {
    AddLog_P2(LOG_LEVEL_DEBUG, PSTR(D_LOG_PREFIX " Got %s %s from %s"), XdrvMailbox.topic, gas_str, source_str);
    Siren_SetStatusGas(status_code, gas_code, false);
  }
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("OK"));
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
    case FUNC_EVERY_SECOND:
      // Unmute siren after 8 hours, if mute is set
      if (drv_siren_mute_time != 0 && millis() - drv_siren_mute_time > 1000 * 60 * 60 * 8) {
        Siren_UnmuteReset(false);
      }
      break;
  }
  return result;
}

#endif // XDRV_95