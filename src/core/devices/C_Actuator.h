#ifndef C_ACTUATOR_H
#define C_ACTUATOR_H

#include <cstdint>

// Enum para identificar os atuadores
enum ActuatorID_enum {
    ID_FAN,
    ID_SERVO_ROOM,
    ID_SERVO_VAULT,
    ID_ALARM_LED,
    ID_BUZZER
};

class C_Actuator {
protected:
    ActuatorID_enum m_actuatorID;

public:

    C_Actuator(ActuatorID_enum id) : m_actuatorID(id) {}
    virtual ~C_Actuator() = default;
    virtual bool set_value(uint8_t value) = 0;
    ActuatorID_enum get_ID() const { return m_actuatorID; }
};

#endif // C_ACTUATOR_H