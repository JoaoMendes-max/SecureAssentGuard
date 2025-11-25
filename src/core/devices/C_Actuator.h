#ifndef C_ACTUATOR_H
#define C_ACTUATOR_H

#include <cstdint>

enum ActuatorID_enum {
    ID_SERVO_ROOM,
    ID_SERVO_VAULT,
    ID_FAN,
    ID_ALARM_LED,
    ID_BUZZER,
    ID_GENERIC_ACTUATOR
};

class C_Actuator {
protected:
    ActuatorID_enum m_actuatorID;

public:
    C_Actuator(ActuatorID_enum id = ID_GENERIC_ACTUATOR) : m_actuatorID(id) {}
    virtual ~C_Actuator() = default;
    virtual bool init() = 0;
    virtual bool set_value(uint8_t value) = 0;

    // Desliga o atuador (Relaxa o servo, pára a ventoinha)
    virtual void stop() = 0;

    ActuatorID_enum get_ID() const { return m_actuatorID; }
};

#endif // C_ACTUATOR_H