

#ifndef C_SERVOMG996R_H
#define C_SERVOMG996R_H

#include "C_Actuator.h"
#include <cstdint>


class C_PWM;

class C_ServoMG996R final : public C_Actuator {
public:
    C_ServoMG996R(ActuatorID_enum id, C_PWM& pwm);
    ~C_ServoMG996R() override;
    bool init() override;
    bool set_value(uint8_t angle) override;
    void stop() override;
    uint8_t getAngle() const { return m_targetAngle; }

private:
    C_PWM& m_pwm;
    uint8_t m_targetAngle;
    static uint8_t angleToDutyCycle(uint8_t angle);

};
#endif 



