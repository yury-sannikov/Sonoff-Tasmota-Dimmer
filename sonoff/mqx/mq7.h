#ifndef MQ7_SENSOR_HDR
#define MQ7_SENSOR_HDR

#include <stdint.h>
#ifdef ARDUINO
    #if ARDUINO < 100
        #include "WProgram.h"
    #else
        #include "Arduino.h"
    #endif
#endif
#include "settings.h"
#include "ads1115.h"

enum MQ7_FLAGS {
  MQ7_NONE        = 0x0,
  MQ7_HEATING     = 0x1,
  MQ7_READING     = 0x2,
};

class MQ7Sensor {
    public:
        MQ7Sensor(SNSMQx_Settings* settings, ADS1115Reader& reader);
        // Start MQ7 sensor heat & cool cycles
        void start();
        // A step function to maintain heat/cool cycles. Return true if reading occur
        bool step();
        // Set default settings
        static void setDefaults(SNSMQx_Settings* settings);

        // Get CO PPM at reading start
        float ppmAtStart() { return m_ppm_start; }
        // Get CO PPM at reading end
        float ppmAtEnd() { return m_ppm_current; }

        bool isHeating() { return m_flags & MQ7_HEATING; }
        bool isReading() { return m_flags & MQ7_READING; }

        float resistance() { return m_resistance_current; }
        float resistanceDelta() { return m_resistance_current - m_resistance_start; }
        unsigned long heaterStarted() { return m_heat_start_mills; }
    private:
        void setHeat(bool isHeat);
        void calibrate();
        void updateResistance(void);
        void updatePPM();

    private:
        SNSMQx_Settings* m_settings;
        ADS1115Reader& m_reader;
        uint8_t m_flags;
        unsigned long m_heat_start_mills;
        float m_resistance_current;
        float m_resistance_start;
        float m_ppm_current;
        float m_ppm_start;
};






#endif // MQ7_SENSOR_HDR