#ifndef C_SENSOR_H
#define C_SENSOR_H
#include "SharedTypes.h"

class C_Sensor {
protected:
    
    SensorID_enum m_sensorID;

public:
    C_Sensor(SensorID_enum id) : m_sensorID(id) {}
    virtual ~C_Sensor() = default;
    virtual bool init() = 0;
    virtual bool read(SensorData* data) = 0;
    SensorID_enum get_ID() const { return m_sensorID; }
};

#endif 