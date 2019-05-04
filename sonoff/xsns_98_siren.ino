#ifndef XSNS_98
#define XSNS_98            98


const char HTTP_BTN_MENU_BUTTON_SIREN[] PROGMEM =
  "<br/><form action='sirencfg' method='get'><button name='mute'>Siren Config</button></form>";

const char HTTP_BTN_MENU_MAIN_SIREN[] PROGMEM =
  "<br/><form action='siren' method='get'><button name='mute'>Mute Siren</button></form>"
  "<br/><form action='siren' method='get'><button name='unmute'>Unmute Siren</button></form>"
  "<br/><form action='siren' method='get'><button name='warn'>Test Warning Siren</button></form>"
  "<br/><form action='siren' method='get'><button name='alm'>Test Alarm Siren</button></form>";

const char HTTP_SIREN_WEB[] PROGMEM = "%s"
  "{s}Siren Status{m}%s:%s{e}";

const char sns_siren_Statuses[] PROGMEM = "off|warning|alarm|mute|unmute|";
const char sns_siren_Gases[] PROGMEM = "co|co2|ch4|";

void sns_siren_Show(bool json)
{
  char siren_text[48];
  char gas_text[48];
  for (int gas = 0; gas < gasMax; gas++) {
    GetTextIndexed(gas_text, sizeof(gas_text), gas, sns_siren_Gases);
    GetTextIndexed(siren_text, sizeof(siren_text), Siren_GetStatus(gas), sns_siren_Statuses);

    if (json) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"gas_%s\":{\"status\":%s\"}"),
      mqtt_data, gas_text, Siren_GetStatus(gas), siren_text);
  #ifdef USE_WEBSERVER
    } else {
      snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_SIREN_WEB, mqtt_data, gas_text, siren_text);
  #endif  // USE_WEBSERVER
    }
  }
}


void sns_siren_HandleWebAction(void)
{
  if (!HttpCheckPriviledgedAccess()) { return; }

  if (WebServer->hasArg("mute")) {
    Siren_Mute();
    HandleRoot();
    return;
  }
  if (WebServer->hasArg("unmute")) {
    Siren_UnmuteReset(true);
    HandleRoot();
    return;
  }
  if (WebServer->hasArg("warn")) {
    Siren_SetStatusGas(sirenWarning, gasCO, true);
    HandleRoot();
    return;
  }
  if (WebServer->hasArg("alm")) {
    Siren_SetStatusGas(sirenAlarm, gasCO, true);
    HandleRoot();
    return;
  }
}

void sns_HandleSirenConfiguration(void)
{
  WSContentStart_P(PSTR("Siren Configuration"));
  WSContentSendStyle();
  WSContentSend_P(HTTP_BTN_MENU_MAIN_SIREN);
  WSContentSpaceButton(BUTTON_MAIN);
  WSContentStop();
}


/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns98(uint8_t function)
{
  bool result = false;
  if (!drv_siren_enabled) {
    return result;
  }

  switch (function) {
#ifdef USE_WEBSERVER
    case FUNC_WEB_APPEND:
      sns_siren_Show(0);
      break;
    case FUNC_WEB_ADD_MAIN_BUTTON:
      WSContentSend_P(HTTP_BTN_MENU_BUTTON_SIREN);
      break;
    case FUNC_WEB_ADD_HANDLER:
      WebServer->on("/siren", sns_siren_HandleWebAction);
      WebServer->on("/sirencfg", sns_HandleSirenConfiguration);
      break;
#endif  // USE_WEBSERVER
  }
  return result;
}

#endif //XSNS_98
