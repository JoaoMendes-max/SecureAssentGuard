#include "C_ServoMG996R.h"
#include <iostream>

// Limites em % (Duty Cycle) para o período de 20ms
// 5% de 20ms = 1ms (0 graus)
// 10% de 20ms = 2ms (180 graus)
#define DUTY_MIN 5
#define DUTY_MAX 10

C_ServoMG996R::C_ServoMG996R(ActuatorID_enum id, C_PWM& pwm)
    : C_Actuator(id), m_pwm(pwm), m_targetAngle(0)
{
}

C_ServoMG996R::~C_ServoMG996R() {
    stop();
}

// --- INIT ---
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

// --- SET VALUE (Mover) ---
bool C_ServoMG996R::set_value(uint8_t angle) {
    // 1. Garante que o PWM está ativo (caso o stop() tenha sido chamado antes)
    if (!m_pwm.setEnable(true)) {
        std::cerr << "[Servo] Erro: Nao consegui reativar o motor" << std::endl;
        return false;
    }

    // max angles of 180
    if (angle > 180) {
        angle = 180;
    }
    m_targetAngle = angle;
    // vai converter angulo em duty cycle porque a função de pwm recebe um duty cycle que depois passa para nano segundos
    uint8_t duty = angleToDutyCycle(angle);

    std::cout << "[Servo] Mover para " << (int)angle << "º (Duty " << (int)duty << "%)" << std::endl;

    if (!m_pwm.setDutyCycle(duty)) {
        std::cerr << "[Servo] Erro critico: Falha ao escrever duty cycle!" << std::endl;
        return false;
    }

    return true;
}

void C_ServoMG996R::stop() {
    m_pwm.setEnable(false);
    std::cout << "[Servo] Stop (PWM Disabled)" << std::endl;
}

uint8_t C_ServoMG996R::angleToDutyCycle(uint8_t angle) {
    return DUTY_MIN + (angle * (DUTY_MAX - DUTY_MIN) / 180);
}