#ifndef C_TREADENVENSOR_H
#define C_TREADENVENSOR_H
#include <cstdint>
#include "C_Thread.h"

class C_Monitor;
class C_TH_SHT30;
class C_Mqueue;

class C_tReadEnvSensor : public C_Thread {
private:
    C_Monitor& m_monitor;        // Mantém consistência com outras threads!
    C_TH_SHT30& m_sensor;
    C_Mqueue& m_mqToActuator;
    C_Mqueue& m_mqToDatabase;

    int m_tempThreshold;
    int m_intervalSeconds;
    uint8_t m_lastFanState;
    // Métodos auxiliares (igual às outras threads)
    void sendLog(int temp, int hum) const;
    static void generateDescription(int temp, int hum, char* buffer, size_t size);

public:
    C_tReadEnvSensor(C_Monitor& monitor,
                     C_TH_SHT30& sensor,
                     C_Mqueue& mqAct,
                     C_Mqueue& mqDB,
                     int intervalSec = 600,
                     int threshold = 30);

    ~C_tReadEnvSensor() override;

    void run() override;
};

#endif