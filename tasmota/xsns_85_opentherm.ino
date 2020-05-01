/*

*/

#include <OpenTherm.h>

#ifdef USE_OPENTHERM

#define XSNS_85 85

// Hot water and boiler parameter ranges
#define OT_HOT_WATER_MIN 23
#define OT_HOT_WATER_MAX 55
#define OT_BOILER_MIN 40
#define OT_BOILER_MAX 85

#define OT_HOT_WATER_DEFAULT 36;
#define OT_BOILER_DEFAULT 85;

#define SNS_OT_DISCONNECT_COOLDOWN_SECONDS 10

enum OpenThermSettingsFlags
{
    // If set, central heating on/off state folllow diagnostic indication bit(6)
    // Otherwise controlled by the POWER state
    EnableCentralHeatingOnDiagnostics = 0x01,
    // If set, hot water heater is always on. It make sense for the on-demand water heaters
    // If clear, controlled by POWER + 1 state
    HotWaterAlwaysOn = 0x02,
};

enum OpenThermConnectionStatus
{
    OTC_NONE,         // OT not initialized
    OTC_DISCONNECTED, // OT communication timed out
    OTC_CONNECTING,   // Connecting after start or from DISCONNECTED state
    OTC_HANDSHAKE,    // Wait for the handshake response
    OTC_READY,        // Last Known Good response state is SUCCESS and no requests are in flight
    OTC_INFLIGHT      // Request sent, waiting from the response
};

OpenThermConnectionStatus sns_ot_connection_status = OpenThermConnectionStatus::OTC_NONE;
uint8_t sns_ot_disconnect_cooldown = 0;

OpenTherm *sns_ot_master = NULL;

// Has valid values if connection status is READY or INFLIGHT
typedef struct OT_BOILER_STATUS_T
{
    // Boiler fault code
    uint8_t m_fault_code;
    // Boiler OEM fault code
    uint8_t m_oem_fault_code;
    // Boilder OEM Diagnostics code
    uint16_t m_oem_diag_code;
    // OpenTherm ID(3) response.
    uint8_t m_slave_flags;
    // OpenTherm ID(1) codes. Should be used to display state
    unsigned long m_slave_raw_status;
    // Desired boiler state
    bool m_enableCentralHeating;
    bool m_enableHotWater;
    bool m_enableCooling;
    bool m_enableOutsideTemperatureCompensation;
    bool m_enableCentralHeating2;

    // Some boilers has an input for the heat request. When short, heat is requested
    // OT ID(0) bit 6 may indicate state of the Heat Request input
    // By enabling this bit we will set m_enableCentralHeating to true when OT ID(0) bit 6 is set.
    // This enables to use external mechanical thermostat to enable heating.
    // Some of the use cases might be setting an emergency temperature to prevent freezing
    // in case of the software thermostat failure.
    bool m_useDiagnosticIndicationAsHeatRequest;

    // If set, hot water does not have correspondent Power bit mapping and always on
    bool m_hotWaterAlwaysOn;

    // Hot Water temperature
    float m_hotWaterSetpoint_read;
    // Flame Modulation
    float m_flame_modulation_read;
    // Boiler Temperature
    float m_boiler_temperature_read;

    // Boiler desired values
    float m_boilerSetpoint;
    float m_hotWaterSetpoint;

} OT_BOILER_STATUS;

OT_BOILER_STATUS sns_ot_boiler_status;

const char *sns_opentherm_connection_stat_to_str(int status)
{
    switch (status)
    {
    case OpenThermConnectionStatus::OTC_NONE:
        return "NONE";
    case OpenThermConnectionStatus::OTC_DISCONNECTED:
        return "FAULT";
    case OpenThermConnectionStatus::OTC_CONNECTING:
        return "CONNECTING";
    case OpenThermConnectionStatus::OTC_HANDSHAKE:
        return "HANDSHAKE";
    case OpenThermConnectionStatus::OTC_READY:
        return "READY";
    case OpenThermConnectionStatus::OTC_INFLIGHT:
        return "BUSY";
    default:
        return "UNKNOWN";
    }
}

// power_t sns_opentherm_get_power_flags()
// {
//     //TODO:
//     // Driver should add 2 additional devices. One for central heating, another for hot water
//     if (devices_present < 2)
//     {
//         return 0;
//     }
//     power_t pwr = Settings.power << (devices_present - 2);
//     return pwr;
// }

void sns_opentherm_init_boiler_status()
{
    memset(&sns_ot_boiler_status, 0, sizeof(OT_BOILER_STATUS));
    // power_t flags = sns_opentherm_get_power_flags();
    // // Enable boiler based on saved power state
    // // Use SetOption0 to control power status
    // sns_ot_boiler_status.m_enableCentralHeating = flags & 0x01;
    // sns_ot_boiler_status.m_enableHotWater = flags & 0x02;

    // Settings
    sns_ot_boiler_status.m_useDiagnosticIndicationAsHeatRequest = Settings.ot_flags & (uint8_t)OpenThermSettingsFlags::EnableCentralHeatingOnDiagnostics;
    sns_ot_boiler_status.m_hotWaterAlwaysOn = Settings.ot_flags & (uint8_t)OpenThermSettingsFlags::HotWaterAlwaysOn;

    sns_ot_boiler_status.m_boilerSetpoint = (float)Settings.ot_boiler_setpoint;
    sns_ot_boiler_status.m_hotWaterSetpoint = (float)Settings.ot_hot_water_setpoint;

    if (sns_ot_boiler_status.m_useDiagnosticIndicationAsHeatRequest) {
        // Central heating will be enabled by the Diagnostics event later
        sns_ot_boiler_status.m_enableCentralHeating = false;
    } else {
        // TODO: fetch boiler state from the power state
        // sns_ot_boiler_status.m_enableCentralHeating = flags & 0x01;

    }

    if (sns_ot_boiler_status.m_hotWaterAlwaysOn) {
        sns_ot_boiler_status.m_enableHotWater = true;
    } else {
        // TODO: fetch hot water from the power state
        sns_ot_boiler_status.m_enableHotWater = true;
    }


    // subject of the future improvements
    sns_ot_boiler_status.m_enableCooling = false;
    sns_ot_boiler_status.m_enableOutsideTemperatureCompensation = false;
    sns_ot_boiler_status.m_enableCentralHeating2 = false;


    sns_ot_boiler_status.m_fault_code = -1;
    sns_ot_boiler_status.m_oem_fault_code = -1;
    sns_ot_boiler_status.m_oem_diag_code = -1;
    sns_ot_boiler_status.m_hotWaterSetpoint_read = -1;
    sns_ot_boiler_status.m_flame_modulation_read = -1;
    sns_ot_boiler_status.m_boiler_temperature_read = -1;
}

void ICACHE_RAM_ATTR sns_opentherm_handleInterrupt()
{
    sns_ot_master->handleInterrupt();
}

void sns_opentherm_processResponseCallback(unsigned long response, int st)
{
    OpenThermResponseStatus status = (OpenThermResponseStatus)st;
    AddLog_P2(LOG_LEVEL_DEBUG_MORE,
              PSTR("[OTH]: Processing response. Status=%s, Response=0x%lX"),
              sns_ot_master->statusToString(status), response);

    if (sns_ot_connection_status == OpenThermConnectionStatus::OTC_HANDSHAKE) {
        return sns_ot_process_handshake(response, st);
    }

    switch (status)
    {
    case OpenThermResponseStatus::SUCCESS:
        if (sns_ot_master->isValidResponse(response))
        {
            sns_opentherm_process_success_response(&sns_ot_boiler_status, response);
        }
        sns_ot_connection_status = OpenThermConnectionStatus::OTC_READY;
        break;

    case OpenThermResponseStatus::INVALID:
        sns_opentherm_check_retry_request();
        sns_ot_connection_status = OpenThermConnectionStatus::OTC_READY;
        break;

    // Timeout may indicate not valid/supported command or connection error
    // In this case we do reconnect.
    // If this command will timeout multiple times, it will be excluded from the rotation later on
    // after couple of failed attempts. See sns_opentherm_check_retry_request logic
    case OpenThermResponseStatus::TIMEOUT:
        sns_opentherm_check_retry_request();
        sns_ot_connection_status = OpenThermConnectionStatus::OTC_DISCONNECTED;
        break;
    }
}

bool sns_opentherm_Init()
{
    if (pin[GPIO_BOILER_OT_RX] < 99 && pin[GPIO_BOILER_OT_TX] < 99)
    {
        sns_ot_master = new OpenTherm(pin[GPIO_BOILER_OT_RX], pin[GPIO_BOILER_OT_TX]);
        sns_ot_master->begin(sns_opentherm_handleInterrupt, sns_opentherm_processResponseCallback);
        sns_ot_connection_status = OpenThermConnectionStatus::OTC_CONNECTING;

        sns_opentherm_init_boiler_status();
        return true;
    }
    return false;
    // !warning, sns_opentherm settings are not ready at this point
}

void sns_opentherm_stat(bool json)
{
    if (!sns_ot_master)
    {
        return;
    }
    const char *statusStr = sns_opentherm_connection_stat_to_str(sns_ot_connection_status);

    if (json)
    {
        ResponseAppend_P(PSTR(",\"OPENTHERM\":{"));
        ResponseAppend_P(PSTR("\"conn\":\"%s\","), statusStr);
        ResponseAppend_P(PSTR("\"flags\":{\"DIHR\":%d,\"HWAO\":%d},"),
            sns_ot_boiler_status.m_useDiagnosticIndicationAsHeatRequest,
            sns_ot_boiler_status.m_hotWaterAlwaysOn);
        sns_opentherm_dump_telemetry();
        ResponseJsonEnd();
#ifdef USE_WEBSERVER
    }
    else
    {
        WSContentSend_P(PSTR("{s}OpenTherm status{m}%s (0x%X){e}"), statusStr, (int)sns_ot_boiler_status.m_slave_flags);
        if (sns_ot_connection_status < OpenThermConnectionStatus::OTC_READY)
        {
            return;
        }
        WSContentSend_P(PSTR("{s}Std/OEM Fault Codes{m}%d/%d{e}"),
                        (int)sns_ot_boiler_status.m_fault_code,
                        (int)sns_ot_boiler_status.m_oem_fault_code);

        WSContentSend_P(PSTR("{s}OEM Diagnostic Code{m}%d{e}"),
                        (int)sns_ot_boiler_status.m_oem_diag_code);

        WSContentSend_P(PSTR("{s}Hot Water Setpoint{m}%d{e}"),
                        (int)sns_ot_boiler_status.m_hotWaterSetpoint_read);

        WSContentSend_P(PSTR("{s}Flame Modulation{m}%d{e}"),
                        (int)sns_ot_boiler_status.m_flame_modulation_read);

        WSContentSend_P(PSTR("{s}Boiler Temp/Setpnt{m}%d / %d{e}"),
                        (int)sns_ot_boiler_status.m_boiler_temperature_read,
                        (int)sns_ot_boiler_status.m_boilerSetpoint);

        if (sns_ot_boiler_status.m_enableCentralHeating) {
            WSContentSend_P(PSTR("{s}Central Heating is Enabled{m}{e}"));
        }
        if (sns_ot_boiler_status.m_enableHotWater) {
            WSContentSend_P(PSTR("{s}Hot Water is Enabled{m}{e}"));
        }
        if (sns_ot_master->isHotWaterActive(sns_ot_boiler_status.m_slave_raw_status)) {
            WSContentSend_P(PSTR("{s}Hot Water is ACTIVE{m}{e}"));
        }

        if (sns_ot_master->isFlameOn(sns_ot_boiler_status.m_slave_raw_status)) {
            WSContentSend_P(PSTR("{s}Flame is ACTIVE{m}{e}"));
        }
        if (sns_ot_boiler_status.m_enableCooling) {
            WSContentSend_P(PSTR("{s}Cooling is Enabled{m}{e}"));
        }
        if (sns_ot_master->isDiagnostic(sns_ot_boiler_status.m_slave_raw_status)) {
            WSContentSend_P(PSTR("{s}Diagnostic Indication{m}{e}"));
        }

#endif // USE_WEBSERVER
    }
}

void sns_ot_start_handshake()
{
    if (!sns_ot_master)
    {
        return;
    }

    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("[OTH]: perform handshake"));

    sns_ot_master->sendRequestAync(
        OpenTherm::buildRequest(OpenThermMessageType::READ_DATA, OpenThermMessageID::SConfigSMemberIDcode, 0)
    );


    sns_ot_connection_status = OpenThermConnectionStatus::OTC_HANDSHAKE;
}

void sns_ot_process_handshake(unsigned long response, int st) {
    OpenThermResponseStatus status = (OpenThermResponseStatus)st;

    if (status != OpenThermResponseStatus::SUCCESS || !sns_ot_master->isValidResponse(response)) {
        AddLog_P2(LOG_LEVEL_ERROR,
                  PSTR("[OTH]: getSlaveConfiguration failed. Status=%s"),
                  sns_ot_master->statusToString(status));
        sns_ot_connection_status = OpenThermConnectionStatus::OTC_DISCONNECTED;
        return;
    }

    AddLog_P2(LOG_LEVEL_DEBUG, PSTR("[OTH]: getLastResponseStatus SUCCESS. Slave Cfg: %lX"), response);

    sns_ot_boiler_status.m_slave_flags = (response & 0xFF00) >> 8;

    sns_ot_connection_status = OpenThermConnectionStatus::OTC_READY;
}


void sns_opentherm_CheckSettings(void)
{
    bool settingsValid = true;

    settingsValid &= Settings.ot_hot_water_setpoint >= OT_HOT_WATER_MIN;
    settingsValid &= Settings.ot_hot_water_setpoint <= OT_HOT_WATER_MAX;
    settingsValid &= Settings.ot_boiler_setpoint >= OT_BOILER_MIN;
    settingsValid &= Settings.ot_boiler_setpoint <= OT_BOILER_MAX;

    if (!settingsValid)
    {
        Settings.ot_hot_water_setpoint = OT_HOT_WATER_DEFAULT;
        Settings.ot_boiler_setpoint = OT_BOILER_DEFAULT;
        Settings.ot_flags =
            OpenThermSettingsFlags::EnableCentralHeatingOnDiagnostics |
            OpenThermSettingsFlags::HotWaterAlwaysOn;
    }
}
/*********************************************************************************************\
 * Command Processing
\*********************************************************************************************/
#define D_CMND_OTHERM "ot_"

#define D_CMND_OTHERM_SET_FLAGS "flags"
// set the boiler temperature into the boiler status. Sutable for the PID app.
// After restart will use default
#define D_CMND_OTHERM_SET_BOILER_SETPOINT "tboiler"
// set the boiler temperature in the boiler status and settings. Incur flash write.
// After power on this value will be used
// Do not overuse tboiler_store, sine it may wear out flash memory
#define D_CMND_OTHERM_WRITE_BOILER_SETPOINT "tboiler_st"

// set DHW temperature into the boiler status. Do not write it in the flash memory.
// suitable for the temporary changes
#define D_CMND_OTHERM_SET_DHW_SETPOINT "twater"
// write DHW temperature into the settings. Incur flash write
#define D_CMND_OTHERM_WRITE_DHW_SETPOINT "twater_st"


enum OpenThermCommands { CMND_OTHERM_SET_FLAGS, CMND_OTHERM_SET_BOILER_SETPOINT, CMND_OTHERM_WRITE_BOILER_SETPOINT,
    CMND_OTHERM_SET_DHW_SETPOINT, CMND_OTHERM_WRITE_DHW_SETPOINT };
const char kOpenThermCommands[] PROGMEM = D_CMND_OTHERM_SET_FLAGS "|" D_CMND_OTHERM_SET_BOILER_SETPOINT "|" D_CMND_OTHERM_WRITE_BOILER_SETPOINT
    "|" D_CMND_OTHERM_SET_DHW_SETPOINT "|" D_CMND_OTHERM_WRITE_DHW_SETPOINT;

boolean sns_opentherm_command() {
    char command [CMDSZ];
    uint8_t ua_prefix_len = strlen(D_CMND_OTHERM);

    boolean serviced = false;

    if (0 != strncasecmp_P(XdrvMailbox.topic, PSTR(D_CMND_OTHERM), ua_prefix_len)) {
        return serviced;
    }

    snprintf_P(log_data, sizeof(log_data), "OpenTherm Command called: "
        "index: %d data_len: %d payload: %d topic: %s data: %s",
        XdrvMailbox.index,
        XdrvMailbox.data_len,
        XdrvMailbox.payload,
        (XdrvMailbox.payload >= 0 ? XdrvMailbox.topic : ""),
        (XdrvMailbox.data_len >= 0 ? XdrvMailbox.data : ""));
    AddLog(LOG_LEVEL_DEBUG);

    int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic + ua_prefix_len, kOpenThermCommands);
    serviced = true;

    bool query = strlen(XdrvMailbox.data) == 0;
    char actual[FLOATSZ];
    switch (command_code) {
        case CMND_OTHERM_SET_FLAGS:
            if (!query) {
                // Set flags value
                Settings.ot_flags = atoi(XdrvMailbox.data);
                // Reset boiler status to apply settings
                sns_opentherm_init_boiler_status();
            }
            snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%d"), Settings.ot_flags);
        break;
        case CMND_OTHERM_SET_BOILER_SETPOINT:
            if (!query) {
                sns_ot_boiler_status.m_boilerSetpoint = atof(XdrvMailbox.data);
            }
            dtostrfd(sns_ot_boiler_status.m_boilerSetpoint, Settings.flag2.temperature_resolution, actual);
            snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s"), actual);
        break;
        case CMND_OTHERM_WRITE_BOILER_SETPOINT:
            if (!query) {
                sns_ot_boiler_status.m_boilerSetpoint = atof(XdrvMailbox.data);
                Settings.ot_boiler_setpoint = (uint8_t)sns_ot_boiler_status.m_boilerSetpoint;
            }
            snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%d"), (int)Settings.ot_boiler_setpoint);
        break;
        case CMND_OTHERM_SET_DHW_SETPOINT:
            if (!query) {
                sns_ot_boiler_status.m_hotWaterSetpoint = atof(XdrvMailbox.data);
            }
            dtostrfd(sns_ot_boiler_status.m_hotWaterSetpoint, Settings.flag2.temperature_resolution, actual);
            snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s"), actual);
        break;
        case CMND_OTHERM_WRITE_DHW_SETPOINT:
            if (!query) {
                sns_ot_boiler_status.m_hotWaterSetpoint = atof(XdrvMailbox.data);
                Settings.ot_hot_water_setpoint = (uint8_t)sns_ot_boiler_status.m_hotWaterSetpoint;
            }
            snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%d"), (int)Settings.ot_hot_water_setpoint);
        break;
        default:
            serviced = false;
    }
    return serviced;
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns85(uint8_t function)
{
    bool result = false;
    if (FUNC_INIT == function)
    {
        if (sns_opentherm_Init())
        {
            sns_opentherm_CheckSettings();
        }
    }

    if (!sns_ot_master)
    {
        return result;
    }

    switch (function)
    {
    case FUNC_LOOP:
        sns_ot_master->process();
        break;
    case FUNC_EVERY_100_MSECOND:
        if (sns_ot_connection_status == OpenThermConnectionStatus::OTC_READY && sns_ot_master->isReady())
        {
            unsigned long request = sns_opentherm_get_next_request(&sns_ot_boiler_status);
            if (-1 != request)
            {
                sns_ot_master->sendRequestAync(request);
                sns_ot_connection_status = OpenThermConnectionStatus::OTC_INFLIGHT;
            }
        }
        break;
    case FUNC_EVERY_SECOND:
        if (sns_ot_connection_status == OpenThermConnectionStatus::OTC_DISCONNECTED)
        {
            // If disconnected, wait for the SNS_OT_DISCONNECT_COOLDOWN_SECONDS before handshake
            // handshake does sync calls, this cooldown make system more responsive
            if (sns_ot_disconnect_cooldown == 0)
            {
                sns_ot_disconnect_cooldown = SNS_OT_DISCONNECT_COOLDOWN_SECONDS;
            }
            else if (--sns_ot_disconnect_cooldown == 0)
            {
                sns_ot_connection_status = OpenThermConnectionStatus::OTC_CONNECTING;
            }
        }
        else if (sns_ot_connection_status == OpenThermConnectionStatus::OTC_CONNECTING)
        {
            sns_ot_start_handshake();
        }
        break;
    case FUNC_COMMAND:
        result = sns_opentherm_command();
        break;
    case FUNC_JSON_APPEND:
        sns_opentherm_stat(1);
        break;
#ifdef USE_WEBSERVER
    case FUNC_WEB_SENSOR:
        sns_opentherm_stat(0);
        break;
#endif // USE_WEBSERVER
    }

    return result;
}

#endif // USE_OPENTHERM
