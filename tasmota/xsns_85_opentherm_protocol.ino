#include "OpenTherm.h"

#ifdef USE_OPENTHERM

typedef union {
    uint8_t m_flags;
    struct
    {
        uint8_t notSupported : 1; // If set, boiler does not support this command
        uint8_t supported : 1;    // Set if at least one response were successfull
        uint8_t retryCount : 2;   // Retry counter before notSupported flag being set
    };
} OpenThermParamFlags;

typedef union {
    float m_float;
    uint8_t m_u8;
    uint16_t m_u16;
    unsigned long m_ul;
} ResponseStorage;

typedef struct OpenThermCommandT
{
    const char*         m_command_name;
    uint8_t             m_command_code;
    OpenThermParamFlags m_flags;
    ResponseStorage     m_results[2];
    unsigned long (*m_ot_make_request)(OpenThermCommandT *self);
    void (*m_ot_parse_response)(OpenThermCommandT *self, OT_BOILER_STATUS_T *boilerStatus, unsigned long response);
    void (*m_ot_appent_telemetry)(OpenThermCommandT *self);
} OpenThermCommand;

OpenThermCommand sns_opentherm_commands[] = {
    {// Read Application-specific fault flags and OEM fault code
     .m_command_name = "ASFF",
     .m_command_code = 0,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_flags,
     .m_ot_parse_response = sns_opentherm_parse_flags,
     .m_ot_appent_telemetry = sns_opentherm_tele_flags},
    {// Read An OEM-specific diagnostic/service code
     .m_command_name = "OEMD",
     .m_command_code = 0,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_oem_diag,
     .m_ot_parse_response = sns_opentherm_parse_oem_diag,
     .m_ot_appent_telemetry = sns_opentherm_tele_oem_diag},
    {// Read Flame modulation
     .m_command_name = "FLM",
     .m_command_code = (uint8_t)OpenThermMessageID::RelModLevel,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_generic_float,
     .m_ot_parse_response = sns_opentherm_parse_flame_modulation,
     .m_ot_appent_telemetry = sns_opentherm_tele_generic_float},
    {// Read Boiler Temperature
     .m_command_name = "TB",
     .m_command_code = (uint8_t)OpenThermMessageID::Tboiler,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_generic_float,
     .m_ot_parse_response = sns_opentherm_parse_boiler_temperature,
     .m_ot_appent_telemetry = sns_opentherm_tele_generic_float},
    {// Read DHW temperature
     .m_command_name = "TDHW",
     .m_command_code = (uint8_t)OpenThermMessageID::Tdhw,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_generic_float,
     .m_ot_parse_response = sns_opentherm_parse_generic_float,
     .m_ot_appent_telemetry = sns_opentherm_tele_generic_float},
    {// Read Outside temperature
     .m_command_name = "TOUT",
     .m_command_code = (uint8_t)OpenThermMessageID::Toutside,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_generic_float,
     .m_ot_parse_response = sns_opentherm_parse_generic_float,
     .m_ot_appent_telemetry = sns_opentherm_tele_generic_float},
    {// Read Return water temperature
     .m_command_name = "TRET",
     .m_command_code = (uint8_t)OpenThermMessageID::Tret,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_generic_float,
     .m_ot_parse_response = sns_opentherm_parse_generic_float,
     .m_ot_appent_telemetry = sns_opentherm_tele_generic_float},
    {// Read DHW setpoint
     .m_command_name = "DHWS",
     .m_command_code = (uint8_t)OpenThermMessageID::TdhwSet,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_generic_float,
     .m_ot_parse_response = sns_opentherm_parse_dhw_setpoint,
     .m_ot_appent_telemetry = sns_opentherm_tele_generic_float},
    {// Read max CH water setpoint
     .m_command_name = "TMAX",
     .m_command_code = (uint8_t)OpenThermMessageID::MaxTSet,
     .m_flags = 0,
     .m_results = {{.m_u8 = 0}, {.m_u8 = 0}},
     .m_ot_make_request = sns_opentherm_get_generic_float,
     .m_ot_parse_response = sns_opentherm_parse_generic_float,
     .m_ot_appent_telemetry = sns_opentherm_tele_generic_float},

};

/////////////////////////////////// App Specific Fault Flags //////////////////////////////////////////////////
unsigned long sns_opentherm_get_flags(struct OpenThermCommandT *self)
{
    return OpenTherm::buildRequest(OpenThermRequestType::READ, OpenThermMessageID::ASFflags, 0);
}

void sns_opentherm_parse_flags(struct OpenThermCommandT *self, struct OT_BOILER_STATUS_T *boilerStatus, unsigned long response)
{
    uint8_t fault_code = (response >> 8) & 0xFF;
    uint8_t oem_fault_code = response & 0xFF;
    boilerStatus->m_fault_code = fault_code;
    boilerStatus->m_oem_fault_code = fault_code;
    self->m_results[0].m_u8 = fault_code;
    self->m_results[1].m_u8 = oem_fault_code;
}

void sns_opentherm_tele_flags(struct OpenThermCommandT *self)
{
    ResponseAppend_P(PSTR("{\"FC\":%d,\"OFC\":%d}"),
                     (int)self->m_results[0].m_u8,
                     (int)self->m_results[1].m_u8);
}

/////////////////////////////////// OEM Diag Code //////////////////////////////////////////////////
unsigned long sns_opentherm_get_oem_diag(struct OpenThermCommandT *self)
{
    return OpenTherm::buildRequest(OpenThermRequestType::READ, OpenThermMessageID::OEMDiagnosticCode, 0);
}

void sns_opentherm_parse_oem_diag(struct OpenThermCommandT *self, struct OT_BOILER_STATUS_T *boilerStatus, unsigned long response)
{
    uint16_t diag_code = (uint16_t)response & 0xFFFF;
    boilerStatus->m_oem_diag_code = diag_code;
    self->m_results[0].m_u16 = diag_code;
}

void sns_opentherm_tele_oem_diag(struct OpenThermCommandT *self)
{
    ResponseAppend_P(PSTR("%d"), (int)self->m_results[0].m_u16);
}

/////////////////////////////////// Generic Single Float /////////////////////////////////////////////////
unsigned long sns_opentherm_get_generic_float(struct OpenThermCommandT *self)
{
    return OpenTherm::buildRequest(OpenThermRequestType::READ, (OpenThermMessageID)self->m_command_code, 0);
}

void sns_opentherm_parse_generic_float(struct OpenThermCommandT *self, struct OT_BOILER_STATUS_T *boilerStatus, unsigned long response)
{
    self->m_results[0].m_float = OpenTherm::getFloat(response);
}

void sns_opentherm_tele_generic_float(struct OpenThermCommandT *self)
{
    char str[FLOATSZ];
    dtostrfd(self->m_results[0].m_float, Settings.flag2.temperature_resolution, str);
    ResponseAppend_P(PSTR("%s"), str);
}

/////////////////////////////////// Specific Floats Rerports to the  /////////////////////////////////////////////////
void sns_opentherm_parse_dhw_setpoint(struct OpenThermCommandT *self, struct OT_BOILER_STATUS_T *boilerStatus, unsigned long response)
{
    self->m_results[0].m_float = OpenTherm::getFloat(response);
    boilerStatus->m_hotWaterSetpoint_read = self->m_results[0].m_float;
}

void sns_opentherm_parse_flame_modulation(struct OpenThermCommandT *self, struct OT_BOILER_STATUS_T *boilerStatus, unsigned long response)
{
    self->m_results[0].m_float = OpenTherm::getFloat(response);
    boilerStatus->m_flame_modulation_read = self->m_results[0].m_float;
}

void sns_opentherm_parse_boiler_temperature(struct OpenThermCommandT *self, struct OT_BOILER_STATUS_T *boilerStatus, unsigned long response)
{
    self->m_results[0].m_float = OpenTherm::getFloat(response);
    boilerStatus->m_boiler_temperature_read = self->m_results[0].m_float;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SNS_OT_COMMANDS_COUNT (sizeof(sns_opentherm_commands) / sizeof(OpenThermCommand))
int sns_opentherm_current_command = SNS_OT_COMMANDS_COUNT;

unsigned long sns_opentherm_get_next_request()
{
    // get next and loop the command
    if (++sns_opentherm_current_command >= SNS_OT_COMMANDS_COUNT)
    {
        sns_opentherm_current_command = 0;
    }

    struct OpenThermCommandT *cmd = &sns_opentherm_commands[sns_opentherm_current_command];
    // Return error if command known as not supported
    if (cmd->m_flags.notSupported)
    {
        return -1;
    }
    // Retrurn OT compatible request
    return cmd->m_ot_make_request(cmd);
}

void sns_opentherm_check_retry_request()
{
    if (sns_opentherm_current_command >= SNS_OT_COMMANDS_COUNT)
    {
        return;
    }
    struct OpenThermCommandT *cmd = &sns_opentherm_commands[sns_opentherm_current_command];

    bool canRetry = ++cmd->m_flags.retryCount < 3;
    // In case of last retry and if this command never respond successfully, set notSupported flag
    if (!canRetry && !cmd->m_flags.supported)
    {
        cmd->m_flags.notSupported = true;
        AddLog_P2(LOG_LEVEL_ERROR,
                  PSTR("[OTH]: command %s not supported by the boiler. Last status: %s"),
                  cmd->m_command_name,
                  sns_ot_master->statusToString(sns_ot_master->getLastResponseStatus()));
    }
}

void sns_opentherm_process_success_response(OT_BOILER_STATUS_T *boilerStatus, unsigned long response)
{
    if (sns_opentherm_current_command >= SNS_OT_COMMANDS_COUNT)
    {
        return;
    }
    struct OpenThermCommandT *cmd = &sns_opentherm_commands[sns_opentherm_current_command];
    // mark command as supported
    cmd->m_flags.supported = true;

    cmd->m_ot_parse_response(cmd, boilerStatus, response);
}

void sns_opentherm_dump_telemetry()
{
    bool add_coma = false;
    for (int i = 0; i < SNS_OT_COMMANDS_COUNT; ++i)
    {
        struct OpenThermCommandT *cmd = &sns_opentherm_commands[i];
        if (!cmd->m_flags.supported)
        {
            continue;
        }

        ResponseAppend_P(PSTR("%s\"%s\":"), add_coma ? "," : "", cmd->m_command_name);

        cmd->m_ot_appent_telemetry(cmd);

        add_coma = true;
    }
}
#endif