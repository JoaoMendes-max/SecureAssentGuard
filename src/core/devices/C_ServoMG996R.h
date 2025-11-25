#ifndef C_SERVOMG996R_H
#define C_SERVOMG996R_H

#include "C_Actuator.h"
#include "../hal/C_PWM.h"
#include <cstdint>

class C_ServoMG996R : public C_Actuator {



public:
    C_ServoMG996R(ActuatorID_enum id, C_PWM& pwm);

    virtual ~C_ServoMG996R();

    // Implementação da Interface
    bool init() override;
    bool set_value(uint8_t angle) override;
    void stop() override;

    // Funcionalidade Extra do Servo
    uint8_t getAngle() const { return m_targetAngle; }

private:
    C_PWM& m_pwm;
    uint8_t m_targetAngle;

    static uint8_t angleToDutyCycle(uint8_t angle);

};

#endif // C_SERVOMG996R_H