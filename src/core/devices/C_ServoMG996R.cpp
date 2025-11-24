#include "C_ServoMG996R.h"
#include <iostream>

// Limites em % (Duty Cycle) para o período de 20ms
#define DUTY_MIN 5   // 1ms
#define DUTY_MAX 10  // 2ms

C_ServoMG996R::C_ServoMG996R(ActuatorID_enum id, C_PWM& pwm): C_Actuator(id), m_pwm(pwm), m_targetAngle(0)
{
    m_pwm.init();
    m_pwm.setPeriodns(20000000); // 20ms
    m_pwm.setEnable(true);
}

C_ServoMG996R::~C_ServoMG996R() {}

bool C_ServoMG996R::set_value(uint8_t angle) {
    if (angle > 180) {
        angle = 180;
    }
    m_targetAngle = angle;

    uint8_t duty = angleToDutyCycle(angle);

    std::cout << "[Servo] Angulo " << (int)angle << " -> Duty " << (int)duty << "%" << std::endl;

    return m_pwm.setDutyCycle(duty);
}

uint8_t C_ServoMG996R::angleToDutyCycle(uint8_t angle) {
    return DUTY_MIN + (angle * (DUTY_MAX - DUTY_MIN) / 180);
}