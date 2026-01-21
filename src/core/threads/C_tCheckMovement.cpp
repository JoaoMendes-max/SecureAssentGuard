#include "C_tCheckMovement.h"
#include <iostream>
#include <cerrno>

C_tCheckMovement::C_tCheckMovement(C_Mqueue& m_mqToCheckMovement, C_Mqueue& m_mqToDatabase,C_Mqueue& m_mqToActuator,C_Monitor& m_monitor)
    : C_Thread(PRIO_MEDIUM), m_monitor(m_monitor),
      m_mqToActuator(m_mqToActuator),
      m_mqToDatabase(m_mqToDatabase),
      m_mqToCheckMovement(m_mqToCheckMovement)
{
}


void C_tCheckMovement::run() {

    while (!stopRequested()) {

        if (m_monitor.timedWait(1)) {
            continue;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_USER_IN_PIR;
        m_mqToDatabase.send(&msg, sizeof(msg));

        AuthResponse resp = {};
        
        while (!stopRequested()) {

            ssize_t bytes = m_mqToCheckMovement.timedReceive(&resp, sizeof(resp), 1);

            if (bytes > 0) {
                
                if (!resp.payload.auth.authorized) {
                    std::cout << "[ALERTA] Movimento NÃO autorizado! ATIVANDO ALARME." << std::endl;

                    ActuatorCmd alarm = {ID_ALARM_ACTUATOR, 1};
                    m_mqToActuator.send(&alarm, sizeof(alarm));

                    sendLog(false);
                } else {
                    std::cout << "[CheckMovement] Movimento autorizado: Utilizadores presentes." << std::endl;
                }

                
                break;
            }
            if (bytes < 0 && errno == ETIMEDOUT) {
                
                
                
                std::cout << "[CheckMovement] DB ainda não respondeu... a tentar ler a fila novamente." << std::endl;
                continue;
            }
            else {
                
                std::cerr << "[CheckMovement] Erro crítico na Message Queue. A sair da espera." << std::endl;
                break;
            }
        }

    }

    std::cout << "[CheckMovement] Thread terminada com sucesso." << std::endl;
}

void C_tCheckMovement::sendLog(bool authorized) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;
    msg.payload.log.logType = authorized ? LOG_TYPE_ACCESS : LOG_TYPE_ALERT;
    msg.payload.log.entityID = 0; 
    msg.payload.log.value = authorized ? 1.0 : 0.0;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(authorized, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg));
}

void C_tCheckMovement::generateDescription(bool authorized, char* buffer, size_t size) {
    if (!authorized) {
        snprintf(buffer, size, "ALERTA: Movimento detetado em sala VAZIA!");
    }
}
