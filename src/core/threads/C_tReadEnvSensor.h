#ifndef C_TREADENVENSOR_H
#define C_TREADENVENSOR_H
#include <cstdint>
#include "C_Thread.h"

class C_Monitor;
class C_TH_SHT30;
class C_Mqueue;

class C_tReadEnvSensor : public C_Thread {
private:
    C_TH_SHT30& m_sensor;
    C_Mqueue& m_mqToActuator;
    C_Mqueue& m_mqToDatabase;
    C_Mqueue& m_mqFromDb;

    int m_tempThreshold;
    int m_intervalSeconds;
    uint8_t m_lastFanState;
    
    void sendLog(double temp, double hum) const;
    static void generateDescription(double temp, double hum, char* buffer, size_t size);

public:
    C_tReadEnvSensor(C_TH_SHT30& sensor,
                     C_Mqueue& mqAct,
                     C_Mqueue& mqDB,
                     C_Mqueue& mqFromDb, 
                     int intervalSec = 600,
                     int threshold = 30);

    ~C_tReadEnvSensor() override;

    void run() override;
};

#endif
