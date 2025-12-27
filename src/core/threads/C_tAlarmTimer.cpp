#include "C_tAlarmTimer.h"
#include "C_Monitor.h"
#include "C_Mqueue.h"
#include <iostream>

// ============================================
// CONSTRUTOR
// ============================================
C_tAlarmTimer::C_tAlarmTimer(C_Monitor& monitor,
                             C_Mqueue& mqTrigger,
                             C_Mqueue& mqAct,
                             int durationSec)
    : C_Thread(PRIO_LOW),
      m_monitor(monitor),
      m_mqAlarmTrigger(mqTrigger),
      m_mqToActuator(mqAct),
      m_alarmDurationSec(durationSec)
{
    std::cout << "[tAlarmTimer] Thread criada (Timer: "
              << m_alarmDurationSec << "s)" << std::endl;
}

// ============================================
// DESTRUTOR
// ============================================
C_tAlarmTimer::~C_tAlarmTimer() {
    std::cout << "[tAlarmTimer] Thread destruída" << std::endl;
}

// ============================================
// RUN
// ============================================
void C_tAlarmTimer::run() {

    char active[1];

    while (true) {
        // ========================================
        // 1. ESPERAR GATILHO (Bloqueante)
        // ========================================
        // A thread dorme aqui até cair QUALQUER coisa na queue
        ssize_t bytes = m_mqAlarmTrigger.receive(active, sizeof(active));

        if (bytes > 0) {
            std::cout << "[tAlarmTimer] ⏰ Trigger recebido! A contar "
                      << m_alarmDurationSec << "s..." << std::endl;

            // ========================================
            // 2. CONTAGEM REGRESSIVA (Sem CPU)
            // ========================================
            // Usa o monitor para dormir X segundos
            m_monitor.timedWait(m_alarmDurationSec);

            // ========================================
            // 3. DESLIGAR ALARME
            // ========================================
            std::cout << "[tAlarmTimer]  Tempo esgotado! A desligar alarme..."
                      << std::endl;

            ActuatorCmd cmd = {ID_ALARM_ACTUATOR, 0}; // 0 = Desligar

            if (m_mqToActuator.send(&cmd, sizeof(cmd))) {
                std::cout << "[tAlarmTimer] Comando enviado: OFF" << std::endl;
            } else {
                std::cerr << "[tAlarmTimer] ERRO ao enviar comando!" << std::endl;
            }
        }
    }
}