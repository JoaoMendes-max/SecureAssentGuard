#ifndef C_SENSOR_H
#define C_SENSOR_H
#include "SharedTypes.h"

// Sensor-specific Data Structures
struct Data_SHT31 {
    float temp;
    float hum;
};

struct Data_RFID_Single {
    char tagID[11];
};

struct Data_RFID_Inventory {
    int tagCount;
    char tagList[4][14];
};

struct Data_Fingerprint {
    bool authenticated;
    int userID;
};

union SensorData_Union {
    Data_SHT31 tempHum;
    Data_RFID_Single rfid_single;
    Data_RFID_Inventory rfid_inventory;
    Data_Fingerprint fingerprint;
};

struct SensorData {
    SensorID_enum type;
    SensorData_Union data;
};


class C_Sensor {
protected:
    // id q identifica o tipo de sensor
    SensorID_enum m_sensorID;

public:
    C_Sensor(SensorID_enum id) : m_sensorID(id) {}

    virtual ~C_Sensor() = default;

    virtual bool init() = 0;
    //vamos usar isto porque assim podemos guardar todo o tipo de data seja ele qual for da mesma forma
    virtual bool read(SensorData* data) = 0;


    SensorID_enum get_ID() const { return m_sensorID; }
};

#endif // C_SENSOR_H