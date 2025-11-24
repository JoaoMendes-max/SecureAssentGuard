#ifndef C_SERVOMG996R_H
#define C_SERVOMG996R_H

#include "C_Actuator.h"
#include "C_PWM.h" // A tua classe de PWM fornecida

class C_ServoMG996R : public C_Actuator {

private:
    C_PWM& m_pwm;
    uint8_t m_targetAngle;

    static uint8_t angleToDutyCycle(uint8_t angle);

public:
    C_ServoMG996R(ActuatorID_enum id, C_PWM& pwm);
    ~C_ServoMG996R() override;

    bool set_value(uint8_t angle) override;
};

#endif