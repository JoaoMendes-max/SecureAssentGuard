#include "C_tCheckMovement.h"
#include <iostream>

C_tCheckMovement::C_tCheckMovement(C_Mqueue& m_mqToCheckMovement, C_Mqueue& m_mqToDatabase,C_Mqueue& m_mqToActuator,C_Monitor& m_monitor)
    : m_monitor(m_monitor),
      m_mqToActuator(m_mqToActuator),
      m_mqToDatabase(m_mqToDatabase),
      m_mqToCheckMovement(m_mqToCheckMovement)
{
}
void C_tCheckMovement::run() {
    while (true) {
        m_monitor.wait();

        DatabaseMsg msg = {};
        msg.command = DB_CMD_USER_IN_PIR;
        m_mqToDatabase.send(&msg, sizeof(msg));

        AuthResponse resp = {};
        m_mqToCheckMovement.receive(&resp, sizeof(resp));
        if (m_mqToCheckMovement.receive(&resp, sizeof(resp)) > 0) {
            // Se authorized for FALSE, significa que não há ninguém autorizado lá dentro
            if (!resp.authorized) {
                std::cout << "[ALERTA] Movimento sem utilizadores na sala! ATIVANDO ALARME." << std::endl;

                // 4. MANDAR ATIVAR O ALARME
                ActuatorCmd alarm = {ID_ALARM_ACTUATOR, 1};
                m_mqToActuator.send(&alarm, sizeof(alarm));

                sendLog(false);
            } else {
                std::cout << "[CheckMovement] Movimento ignorado: Utilizadores presentes." << std::endl;
            }
        }
    }
}

void C_tCheckMovement::sendLog(bool authorized) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;
    msg.payload.log.logType = authorized ? LOG_TYPE_ACCESS : LOG_TYPE_ALERT;
    msg.payload.log.entityID = 0; // No PIR não há um UserID específico
    msg.payload.log.value = authorized ? 1 : 0;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(authorized, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg));
}

void C_tCheckMovement::generateDescription(bool authorized, char* buffer, size_t size) {
    if (!authorized) {
        snprintf(buffer, size, "ALERTA: Movimento detetado em sala VAZIA!");
    }
}
