#ifndef MQX_ADS_1115
#define MQX_ADS_1115
#include <stdint.h>

#ifdef ARDUINO
    #if ARDUINO < 100
        #include "WProgram.h"
    #else
        #include "Arduino.h"
    #endif
#endif

#define MQX_ADS1115_REG_CONFIG_MODE_CONTIN  (0x0000)  // Continuous conversion mode
#define MQX_ADS1115_REG_POINTER_CONVERT     (0x00)
#define MQX_ADS1115_REG_POINTER_CONFIG      (0x01)

#define MQX_ADS1115_REG_CONFIG_CQUE_NONE    (0x0003)  // Disable the comparator and put ALERT/RDY in high state (default)
#define MQX_ADS1115_REG_CONFIG_CLAT_NONLAT  (0x0000)  // Non-latching comparator (default)
#define MQX_ADS1115_REG_CONFIG_PGA_4_096V   (0x0200)  // +/-4.096V range = Gain 1
#define MQX_ADS1115_REG_CONFIG_CPOL_ACTVLOW (0x0000)  // ALERT/RDY pin is low when active (default)
#define MQX_ADS1115_REG_CONFIG_CMODE_TRAD   (0x0000)  // Traditional comparator with hysteresis (default)
#define MQX_ADS1115_REG_CONFIG_DR_6000SPS   (0x00E0)  // 6000 samples per second
#define MQX_ADS1115_REG_CONFIG_MUX_SINGLE_0 (0x4000)  // Single-ended AIN0

#define MQX_ADS1115_REG_CONFIG_MODE_SINGLE  (0x0100)  // Power-down single-shot mode (default)
#define MQX_ADS1115_CONVERSIONDELAY         (8)       // CONVERSION DELAY (in mS)

#define MQX_ADS1115_MV_4P096                (0.125000)

#define RL_VALUE                            (10)   // Sensors load resistance kOhms

// Schematic defined constants. Describes a connection to the physical ADS1115 channels
#define MQ2_CHANNEL                         (0)
#define MQ7_CHANNEL                         (1)
#define VREF_CHANNEL                        (2)

class ADS1115Reader {
    public:
        ADS1115Reader();

        bool initialized() { return m_address != 0; }
        uint8_t address() { return m_address; }
        uint8_t detectAddress();

        // Get sensor resistance in kOhm based on load resistor value RL_VALUE
        float calculateResistance(int channel);
    private:
        int16_t getConversion(uint8_t channel);
        void startComparator(uint8_t channel, uint16_t mode);

    private:
        int16_t m_referenceVoltage;
        uint8_t m_address;
        int m_cont_channel;
};

float snsMqx_GetPercentage(float rs_ro_ratio, float *curve);


#endif //MQX_ADS_1115