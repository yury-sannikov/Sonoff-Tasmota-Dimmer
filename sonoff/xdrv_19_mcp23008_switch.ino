#ifdef USE_I2C

/*********************************************************************************************\
   MCP23008 - I2C GPIO Switch
   This module is intened to utilize MCP23008 with wall swithes to controll lights. The idea to make
   light swith to work as expected, but also be able to controll light/bright from MQTT channel

   Features:
   Configure GPIO as inputs and send POWERx messages based on configuration
   - Follow mode (+inverse): POWERx will follow GPIO pin status afte debouncing. Only one follow switch per pin
     - Use PowerOnStateX 0 (Keep relay(s) off after power on) since power state is controlled by swith
     - Use SetOption0 0
   - Toggle mode: Toggle POWER when GPIO pin state transition after debounce
     - You may use any PowerOnStateX value
   - Push button mode: not supported
   Feedback mode:
   A MCP pin can be configured as an output and reflect POWERx status (direct or inverted).
   This feature is handy to light up small led in a switch when light is off

  This module is reusing Mcp230xxCfg but use Mcp23008_switch_cfg layout
   pinmode:
   0 - follow               (FN)
   1 - follow inverted      (FI)
   2 - togglee              (TG)
   3 - (reserved)
   4 - status (feedback)    (SN)
   5 - feedback inverted    (SI)
  pullup: Enable internal weak pull-up resistor
  power_gpoup: (0 - 15) - associate GPIO with POWERx

  Commands:
  MCPSWITCH0  - report all pin configuration
  MCPSWITCHx  - report pin x status

  MCPSWITCH   - set configuration

  Set command format:
  [pin][pinmode opcode][power_group]
  MCPSWITCH   0FN0,1FN1,2TG2,3TG2,4TG2,5SN0,6SN1,7SN2
    pin 0, FN - follow normal mode, POWER GROUP 0
    pin 1, FN - follow normal mode, POWER GROUP 1
    pin 2, TG - toggle mode,        POWER GROUP 2
    pin 3, TG - toggle mode,        POWER GROUP 2
    pin 4, TG - toggle mode,        POWER GROUP 2
    pin 5, SN - status feedback     POWER GROUP 0
    pin 6, SN - status feedback     POWER GROUP 1
    pin 7, SN - status feedback     POWER GROUP 2

    pin0 and pin1 are simple follow mode (normal swith) which control Power0 and Power1 (relays)
    pins 2,3,4 connected to wall swith. Each wall swith toggle will toggle Power2 state.
    This may be helpfull for hallway/stair switches, where you can turn on light from downstairs
    and turn it off from your room

    NOTE on the Follow mode:
    TL-DR: Switch driver will never made an attempt to change power state on POWER-ON/RESET based on GPIO values.

    - A POWERx command have a priority over FOLLOW pin state. You can send MQTT `POWER OFF` command and it
    will turn power off and keep it in that state since switch module update state on change only.
    - In case of reset/power loss, a Settings.flag.save_state and Settings.power flag is used to determine
    which state your swtich should be in. You may require save state and the power state will be restored.
    Otherwice, it will maintain OFF state until you turn of and then on again.
    This is handy to avoid lights to be on if power failure occurs.

\*********************************************************************************************/

uint8_t MCP230family_IODIR          = 0x00;
uint8_t MCP230family_GPINTEN        = 0x02;
uint8_t MCP230family_IOCON          = 0x05;
uint8_t MCP230family_GPPU           = 0x06;
uint8_t MCP230family_INTF           = 0x07;
uint8_t MCP230family_INTCAP         = 0x08;
uint8_t MCP230family_GPIO           = 0x09;

// pinmode & mask == true --> set GPIO as output
#define PIN_MODE_OUTPUT_MASK (1 << 2)

enum MCP_TYPE {
  MCP_TYPE_NONE = 0,
  MCP_TYPE_MCP23008 = 1,
  MCP_TYPE_MCP23017 = 2
};

enum MCP_MODE {
  MCP_MODE_NONE = -1,
  MCP_MODE_FOLLOW = 0,
  MCP_MODE_FOLLOW_INV,
  MCP_MODE_TOGGLE,
  MCP_MODE_RESERVED,
  MCP_MODE_FEEDBACK,
  MCP_MODE_FEEDBACK_INV
};

uint8_t mcp230_switch_type = MCP_TYPE_NONE;

// An input mask. Calculated based on the settings. GPIO read values outside of the mask will be ignored
// 0-7 bits - bank0, 8-15 bits - bank1
uint16_t mcp230_switch_input_mask = 0;
// Hold last power state to compare with. If changed, update feedback pins
power_t mcp230_switch_last_power_state = 0;

uint8_t MCPSwitch_readGPIO(uint8_t bank) {
  return I2cRead8(USE_MCP230xx_ADDR, MCP230family_GPIO + bank);
}

void MCPSwitch_ApplySettings(void) {
  Mcp23008_switch_cfg* cfg = (Mcp23008_switch_cfg*)Settings.mcp230xx_config;
  mcp230_switch_input_mask = 0;

  for (uint8_t mcp_bank = 0; mcp_bank < mcp230_switch_type; ++mcp_bank) {
    // Set all as inputs by default
    uint8_t reg_iodir = 0xFF;
    // Output (switch feedback) pin values
    uint8_t reg_portpins = 0x00;
    // Do not use any interrupts, only pooling
    uint8_t reg_gpinten = 0;
    // Pullup. You probably need at least an RC filter with strong pullup
    // to avoid interference with mains
    uint8_t reg_gppu = 0;

    for (uint8_t idx = 0; idx < 8; idx++) {
      uint8_t cidx = idx + (mcp_bank * 8);
      uint8_t pinmask = (1 << idx);
      // 1 - input,  0 - output
      if ((cfg[cidx].pinmode & PIN_MODE_OUTPUT_MASK) == 0) {
        reg_iodir |= pinmask;
        reg_gppu |= cfg[cidx].pullup ? pinmask : 0;
      } else {
        // Set register as an output
        reg_iodir &= ~pinmask;
        // Sync up power state.
        // Get mask from power_gpoup.
        power_t mask = 1 << cfg[cidx].power_gpoup;
        // If power match mask, set value to pinmask to set proper bit in reg_portpins
        power_t value = (Settings.power & mask) ? pinmask : 0;
        // Invert a bit if current pin mode is in inverted feedback
        if (cfg[cidx].pinmode == MCP_MODE_FEEDBACK_INV) {
          value ^= pinmask;
        }
        // Update port pin
        reg_portpins |= value;
      }
    }
    snprintf_P(log_data, sizeof(log_data), PSTR("SWI: Bank%d GPPU(0x%X) IODIR(0x%X) GPIO(0x%X)"), mcp_bank, reg_gppu, reg_iodir, reg_portpins);
    AddLog(LOG_LEVEL_DEBUG_MORE);

    // Update mcp230_switch_input_mask based on reg_iodir value
    if (mcp_bank > 0) {
      // Shift for MCP23017
      mcp230_switch_input_mask |= ((uint16_t)reg_iodir) << 8;
    } else {
      mcp230_switch_input_mask |= reg_iodir;
    }


    // Init bank
    I2cWrite8(USE_MCP230xx_ADDR, MCP230family_GPPU + mcp_bank, reg_gppu);
    I2cWrite8(USE_MCP230xx_ADDR, MCP230family_GPINTEN + mcp_bank, reg_gpinten);
    I2cWrite8(USE_MCP230xx_ADDR, MCP230family_IODIR + mcp_bank, reg_iodir);
    I2cWrite8(USE_MCP230xx_ADDR, MCP230family_GPIO + mcp_bank, reg_portpins);
  }
}


void MCPSwitch_Detect(void)
{
  if (mcp230_switch_type) {
    return;
  }

  uint8_t buffer;

  I2cWrite8(USE_MCP230xx_ADDR, MCP230family_IOCON, 0x80); // attempt to set bank mode - this will only work on MCP23017, so its the best way to detect the different chips 23008 vs 23017
  if (I2cValidRead8(&buffer, USE_MCP230xx_ADDR, MCP230family_IOCON)) {
    if (0x00 == buffer) {
      mcp230_switch_type = MCP_TYPE_MCP23008;
      snprintf_P(log_data, sizeof(log_data), S_LOG_I2C_FOUND_AT, "MCP23008", USE_MCP230xx_ADDR);
      AddLog(LOG_LEVEL_DEBUG);

      MCPSwitch_ApplySettings();
    } else {
      if (0x80 == buffer) {
        mcp230_switch_type = MCP_TYPE_MCP23017;
        snprintf_P(log_data, sizeof(log_data), S_LOG_I2C_FOUND_AT, "MCP23017", USE_MCP230xx_ADDR);
        AddLog(LOG_LEVEL_DEBUG);

        // Reset bank mode to 0
        I2cWrite8(USE_MCP230xx_ADDR, MCP230family_IOCON, 0x00);

        // Update register locations for MCP23017
        MCP230family_GPINTEN        = 0x04;
        MCP230family_GPPU           = 0x0C;
        MCP230family_INTF           = 0x0E;
        MCP230family_INTCAP         = 0x10;
        MCP230family_GPIO           = 0x12;

        MCPSwitch_ApplySettings();
      }
    }
  }
  mcp230_switch_last_power_state = power;
}


enum MCPSwitchCommands {
  SWI_CMND_MCPSWITCH,
  SWI_CMND_PULLUP
};

const char g_MCPSwitchCommands[] PROGMEM =
  "MCPSWITCH|MCPPULLUP";

#define MCPSWITCH_CMD_LEN 9

const char* MCPSwitch_GetPinModeText(uint8_t mode) {
  switch(mode) {
    case MCP_MODE_FOLLOW: return "FN";
    case MCP_MODE_FOLLOW_INV: return "FI";
    case MCP_MODE_TOGGLE: return "TG";
    case MCP_MODE_FEEDBACK: return "SN";
    case MCP_MODE_FEEDBACK_INV: return "SI";
    default: return "??";
  }
}

void MCPSwitch_Pullup() {
  int16_t index = XdrvMailbox.index - 1;
  if (index < 0 || index > 15) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("index err"));
    return;
  }
  Mcp23008_switch_cfg* cfg = (Mcp23008_switch_cfg*)Settings.mcp230xx_config;
  if (strlen(XdrvMailbox.data) != 0) {
    cfg[index].pullup = atoi(XdrvMailbox.data) > 0 ? 1 : 0;
  }
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s"),
    cfg[index].pullup ? "ON" : "OFF");
}

// Print current status
void MCPSwitch_Status() {
  Mcp23008_switch_cfg* cfg = (Mcp23008_switch_cfg*)Settings.mcp230xx_config;

  // Use index 0 to display all values
  int16_t index = XdrvMailbox.index - 1;
  snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("{"));

  for (uint8_t mcp_bank = 0; mcp_bank < mcp230_switch_type; ++mcp_bank) {
    uint8_t gpio_value = MCPSwitch_readGPIO(mcp_bank);

    // snprintf_P(log_data, sizeof(log_data), PSTR("SWI: Read BANK%d, GPIO=0x%X"),
    //   mcp_bank, gpio_value);
    // AddLog(LOG_LEVEL_DEBUG);

    for (uint8_t idx = 0; idx < 8; idx++) {
      uint8_t cidx = idx + (mcp_bank * 8);
      // If index is set, skip everything except that index
      if (index != -1 && index != cidx) {
        continue;
      }
      if (strlen(mqtt_data) > 1) {
        snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s,"), mqtt_data);
      }
      uint8_t value = gpio_value & (1 << idx);
      value = value >> idx;
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s\"p%d\":{\"m\":\"%s\",\"pg\":\"%d\",\"v\":\"%d\"}"),
        mqtt_data, cidx, MCPSwitch_GetPinModeText(cfg[cidx].pinmode), cfg[cidx].power_gpoup, value);
    }
  }

  if (strlen(mqtt_data) > 0) {
    snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("%s}"), mqtt_data);
  }
}

uint8_t MCPSwitch_ParsePinMode(char *mode) {
  if (!strncmp(mode, "FN", 2)) {
    return MCP_MODE_FOLLOW;
  } else if (!strncmp(mode, "FI", 2)) {
    return MCP_MODE_FOLLOW_INV;
  } else if (!strncmp(mode, "TG", 2)) {
    return MCP_MODE_TOGGLE;
  } else if (!strncmp(mode, "SN", 2)) {
    return MCP_MODE_FEEDBACK;
  } else if (!strncmp(mode, "SI", 2)) {
    return MCP_MODE_FEEDBACK_INV;
  }
  return MCP_MODE_NONE;
}

void MCPSwitch_Configure() {
  char sub_string[XdrvMailbox.data_len + 1];

  uint8_t tokens = 1;
  for (int i = 0; i < XdrvMailbox.data_len; ++i) {
    if (XdrvMailbox.data[i] == ',') {
      ++tokens;
    }
  }

  Mcp23008_switch_cfg* cfg = (Mcp23008_switch_cfg*)Settings.mcp230xx_config;

  for (int i = 1; i <= tokens; ++i) {
    char* pinConfig = subStr(sub_string, XdrvMailbox.data, ",", i);
    if (!pinConfig) {
      break;
    }

    uint8_t pin = 0;
    uint8_t mode = MCP_MODE_NONE;
    uint8_t power_group = 0;
    // Parse `3TG2` of type [pin/d{1,2}][MCP_MODE][power_group]
    size_t tokenlen = strlen(pinConfig);
    if (tokenlen == 4 || tokenlen == 5) {
      pin = *pinConfig - '0';
      ++pinConfig;
      if (tokenlen == 5) {
        pin = (pin * 10) + *pinConfig - '0';
        ++pinConfig;
      }
      mode = MCPSwitch_ParsePinMode(pinConfig);
      power_group = pinConfig[2] - '0';
    }
    if (mode == MCP_MODE_NONE || pin >= 16 || power_group > devices_present) {
      snprintf_P(mqtt_data, sizeof(mqtt_data), PSTR("bad pin%d cfg:[%s]"), i, pinConfig);
      return;
    }
    cfg[pin].power_gpoup = power_group;
    cfg[pin].pinmode = mode;
  }
  XdrvMailbox.index = 0;
  MCPSwitch_Status();
}

boolean MCPSwitch_Command()
{
  char command[CMDSZ];
  int command_code = GetCommandCode(command, sizeof(command), XdrvMailbox.topic, g_MCPSwitchCommands);
  if (-1 == command_code) {
    return false;
  }
  snprintf_P(log_data, sizeof(log_data), PSTR("SWI: cmd %s"), XdrvMailbox.topic);
  AddLog(LOG_LEVEL_DEBUG);

  switch(command_code) {
    case SWI_CMND_MCPSWITCH:
      if (strlen(XdrvMailbox.data) == 0) {
        MCPSwitch_Status();
      } else {
        MCPSwitch_Configure();
      }
    break;
    case SWI_CMND_PULLUP:
      MCPSwitch_Pullup();
    break;
    default:
      return false;
  }
  return true;
}

// SyncPower check current power status and update feedback GPIO pins if it was changed
void MCPSwitch_SyncPower() {
  if (mcp230_switch_type == MCP_TYPE_NONE) {
    return;
  }
  if (mcp230_switch_last_power_state == power) {
    return;
  }
  snprintf_P(log_data, sizeof(log_data), PSTR("SWI: sync power 0x%X => 0x%X"),
    mcp230_switch_last_power_state, power);
  AddLog(LOG_LEVEL_DEBUG);

  mcp230_switch_last_power_state = power;

  Mcp23008_switch_cfg* cfg = (Mcp23008_switch_cfg*)Settings.mcp230xx_config;
  for (uint8_t mcp_bank = 0; mcp_bank < mcp230_switch_type; ++mcp_bank) {
    uint8_t reg_portpins = 0x00;
    for (uint8_t idx = 0; idx < 8; idx++) {
      uint8_t cidx = idx + (mcp_bank * 8);
      uint8_t pinmask = (1 << idx);
      if ((cfg[cidx].pinmode & PIN_MODE_OUTPUT_MASK) == 0) {
        continue;
      }
      // Get mask from power_gpoup.
      power_t mask = 1 << cfg[cidx].power_gpoup;
      // If power match mask, set value to pinmask to set proper bit in reg_portpins
      power_t value = (power & mask) ? pinmask : 0;
      // Invert a bit if current pin mode is in inverted feedback
      if (cfg[cidx].pinmode == MCP_MODE_FEEDBACK_INV) {
        value ^= pinmask;
      }
      // Update port pin
      reg_portpins |= value;
    }
    I2cWrite8(USE_MCP230xx_ADDR, MCP230family_GPIO + mcp_bank, reg_portpins);
  }

}

/*********************************************************************************************\
   Interface
\*********************************************************************************************/

#define XDRV_19
boolean Xdrv19(byte function)
{
  boolean result = false;

  if (!i2c_flg) {
    return result;
  }

  switch (function) {
    case FUNC_EVERY_SECOND:
      MCPSwitch_Detect();
      break;
    case FUNC_COMMAND:
      result = MCPSwitch_Command();
      break;
    case FUNC_EVERY_100_MSECOND:
      MCPSwitch_SyncPower();
      break;
  }
  return result;
}

#endif