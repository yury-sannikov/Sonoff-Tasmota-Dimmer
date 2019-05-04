#ifndef MQ2_SENSOR_HDR
#define MQ2_SENSOR_HDR

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

enum MQ2_FLAGS {
  MQ2_NONE        = 0x0,
  MQ2_CALIBRATING = 0x1
};

#define MQ2_CALIBRATION_250MS_LOOPS 100
#define MQ2_CALCULATE_PPM_250MS_LOOPS 4

class MQ2Sensor {
    public:
        MQ2Sensor(SNSMQx_Settings* settings, ADS1115Reader& reader);
        // Start MQ2 sensor
        void start();

        // A step function to maintain calibration
        bool step();

        // Set default settings
        static void setDefaults(SNSMQx_Settings* settings);

        // Get CO PPM
        float ppmReading() { return m_ppm; }
        float ppmReadingSmoothed() { return m_ppm_smoothed; }

        bool isCalibrating() { return m_flags & MQ2_CALIBRATING; }

        float resistance() { return m_resistance; }
        unsigned int calibrationLoop() { return m_calibration_loop; }
    private:
        void calibrate();
        void updatePPM();
    private:
        SNSMQx_Settings* m_settings;
        ADS1115Reader& m_reader;
        uint8_t m_flags;
        float m_resistance;
        float m_ppm;
        float m_ppm_smoothed;
        unsigned int m_calibration_loop;
        unsigned int m_ppm_loop;
};







#endif // MQ2_SENSOR_HDR
