#ifndef C_SENSOR_H
#define C_SENSOR_H

#include <cstdint>

// --- Data Definitions ---

// Sensor Identifiers
enum SensorID_enum {
    ID_SHT31,
    ID_RDM6300,
    ID_YRM1001,
    ID_FINGERPRINT
};

// Sensor-specific Data Structures
struct Data_SHT31 {
    float temp;
    float hum;
};

struct Data_RFID_Single {
    char tagID[20];
};

struct Data_RFID_Inventory {
    int tagCount;
    char tagList[50][20];
};

struct Data_Fingerprint {
    bool authenticated;
    int userID;
};

// Union to save memory (stores only one type at a time)
union SensorData_Union {
    Data_SHT31 tempHum;
    Data_RFID_Single rfid_single;
    Data_RFID_Inventory rfid_inventory;
    Data_Fingerprint fingerprint;
};

// Main Data Packet
struct SensorData {
    SensorID_enum type;
    SensorData_Union data;
};


class C_Sensor {
protected:
    SensorID_enum m_sensorID;

public:
    //
    C_Sensor(SensorID_enum id) : m_sensorID(id) {}

    virtual ~C_Sensor();

    virtual bool init() = 0;
    virtual bool read(SensorData* data) = 0;

    // Getter
    SensorID_enum get_ID() const { return m_sensorID; }
};

#endif // C_SENSOR_H