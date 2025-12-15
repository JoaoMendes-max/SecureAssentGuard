
#ifndef C_ACTUATOR_H
#define C_ACTUATOR_H

#include <cstdint>
#include "SharedTypes.h"

class C_Actuator {
protected:
    ActuatorID_enum m_actuatorID;

public:
    C_Actuator(ActuatorID_enum id): m_actuatorID(id) {}
    virtual ~C_Actuator() = default;
    virtual bool init() = 0;
    virtual bool set_value(uint8_t value) = 0;
    virtual void stop() = 0;


    ActuatorID_enum get_ID() const { return m_actuatorID; }
};

#endif // C_ACTUATOR_H




