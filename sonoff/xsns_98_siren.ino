#ifndef XSNS_98
#define XSNS_98            98


const char HTTP_BTN_MENU_MAIN_SIREN[] PROGMEM =
  "<br/><form action='siren' method='get'><button name='mute'>Mute Siren</button></form>"
  "<br/><form action='siren' method='get'><button name='unmute'>Unmute Siren</button></form>"
  "<br/><form action='siren' method='get'><button name='test'>Test Siren</button></form>";

const char HTTP_SIREN_WEB[] PROGMEM = "%s"
  "{s}Siren Status{m}%s:%s{e}";

const char sns_siren_Statuses[] PROGMEM = "Off|Mute|Warning|Alarm|";
const char sns_siren_Gases[] PROGMEM = "None|CO|CO2|Flammable|";

void sns_siren_Show(bool json)
{
  char siren_text[48];
  char gas_text[48];
  GetTextIndexed(gas_text, sizeof(gas_text), Siren_GetGas(), sns_siren_Gases);
  GetTextIndexed(siren_text, sizeof(siren_text), Siren_GetStatus(), sns_siren_Statuses);

  if (json) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,\"siren\":{\"status\":%d,\"gas\":%d,\"trep\":\"%s:%s\"}"),
    mqtt_data, Siren_GetStatus(), Siren_GetGas(), gas_text, siren_text);
#ifdef USE_WEBSERVER
  } else {
    snprintf_P(mqtt_data, sizeof(mqtt_data), HTTP_SIREN_WEB, mqtt_data, gas_text, siren_text);
#endif  // USE_WEBSERVER
  }
}


int __stat = 0;
void sns_siren_HandleWebAction(void)
{
  if (!HttpCheckPriviledgedAccess()) { return; }

  if (WebServer->hasArg("mute")) {
    Siren_SetStatusGas(sirenMute, Siren_GetGas(), false);
    HandleRoot();
    return;
  }
  if (WebServer->hasArg("unmute")) {
    Siren_UnmuteReset();
    HandleRoot();
    return;
  }

  switch (__stat++)
  {
    case 0:
      Siren_SetStatusGas(sirenOff, gasNone, false);
      break;
    case 1:
      Siren_SetStatusGas(sirenWarning, gasCO, false);
      break;
    case 2:
      Siren_SetStatusGas(sirenWarning, gasCO2, false);
      break;
    case 3:
      Siren_SetStatusGas(sirenWarning, gasFlammable, false);
      break;
    case 4:
      Siren_SetStatusGas(sirenAlarm, gasCO, false);
      break;
    case 5:
      Siren_SetStatusGas(sirenAlarm, gasCO2, false);
      break;
    case 6:
      Siren_SetStatusGas(sirenAlarm, gasFlammable, false);
      break;
    case 7:
      Siren_SetStatusGas(sirenOff, gasNone, false);
      __stat = 0;
      break;
    default:
      break;
  }
  HandleRoot();
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
      WSContentSend_P(HTTP_BTN_MENU_MAIN_SIREN);
      break;
    case FUNC_WEB_ADD_HANDLER:
      WebServer->on("/siren", sns_siren_HandleWebAction);
      break;
#endif  // USE_WEBSERVER
  }
  return result;
}

#endif //XSNS_98
