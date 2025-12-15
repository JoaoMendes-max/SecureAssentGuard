
#include "C_ServoMG996R.h"
#include "C_PWM.h"
#include <iostream>

// Limits in % (Duty Cycle) for period of 20ms
// 5% of 20ms = 1ms (0 degrees)
// 10% of 20ms = 2ms (180 degrees)
#define DUTY_MIN 5
#define DUTY_MAX 10

C_ServoMG996R::C_ServoMG996R(ActuatorID_enum id, C_PWM& pwm)
    : C_Actuator(id), m_pwm(pwm), m_targetAngle(0) {}

C_ServoMG996R::~C_ServoMG996R() {
    C_ServoMG996R::stop();
}

void C_ServoMG996R::stop() {
    m_pwm.setEnable(false);
    std::cout << "[Servo] Stop (PWM Disabled)" << std::endl;
}

bool C_ServoMG996R::init() {
    if (!m_pwm.init()) {
        std::cerr << "[Servo] Erro: Falha no init do PWM" << std::endl;
        return false;
    }

    if (!m_pwm.setPeriodns(20000000)) {
        std::cerr << "[Servo] Erro: Falha ao definir periodo" << std::endl;
        return false;
    }

    if (!m_pwm.setEnable(true)) {
        std::cerr << "[Servo] Erro: Falha ao ativar PWM" << std::endl;
        return false;
    }

    return true;
}

bool C_ServoMG996R::set_value(uint8_t angle) {

    if (!m_pwm.setEnable(true)) {
        std::cerr << "[Servo] Erro: Nao consegui reativar o motor" << std::endl;
        return false;
    }
    if (angle == 0) {
        stop();  // para a thread dos atuadores assim ter se receber 0 é para desligar
        return true;
    }

    // max angles of 180
    if (angle > 180) {
        angle = 180;
    }
    m_targetAngle = angle;
    // convert angle to duty cycle ( the pwm class use receive a duty cycle in %)
    uint8_t duty = angleToDutyCycle(angle);

    std::cout << "[Servo] Mover para " << (int)angle << "º (Duty " << (int)duty << "%)" << std::endl;

    if (!m_pwm.setDutyCycle(duty)) {
        std::cerr << "[Servo] Erro critico: Falha ao escrever duty cycle!" << std::endl;
        return false;
    }

    return true;
}

uint8_t C_ServoMG996R::angleToDutyCycle(uint8_t angle) {
    return DUTY_MIN + (angle * (DUTY_MAX - DUTY_MIN) / 180);
}

