#include "C_tLeaveRoomAccess.h"
#include <iostream>
#include <cstring>
#include <ctime>

C_tLeaveRoomAccess::C_tLeaveRoomAccess(C_Monitor& monitor,
                                       C_RDM6300& rfid,
                                       C_Mqueue& mqDB,
                                       C_Mqueue& mqFromDB,
                                       C_Mqueue& mqAct)
    : m_monitor(monitor),
      m_rfidExit(rfid),
      m_mqToDatabase(mqDB),
      m_mqToLeaveRoom(mqFromDB),
      m_mqToActuator(mqAct),
      m_maxAttempts(3),
      m_failedAttempts(0)
{
}

C_tLeaveRoomAccess::~C_tLeaveRoomAccess() {
}

void C_tLeaveRoomAccess::run() {
    std::cout << "[LeaveRoom] Thread em execução. À espera de tags para sair..." << std::endl;

    while (true) {
        m_monitor.wait();
        SensorData data = {};

        // 1. Leitura do sensor interno
        if (m_rfidExit.read(&data)) {
            char* rfidRead = data.data.rfid_single.tagID;
            std::cout << "[RFID-EXIT] Cartão lido: " << rfidRead << std::endl;

            // 2. Pedido de SAÍDA explícito à Base de Dados
            DatabaseMsg msg = {};
            msg.command = DB_CMD_LEAVE_ROOM_RFID; // Comando para forçar IsInside = 0
            strncpy(msg.payload.rfid, rfidRead, 11);
            m_mqToDatabase.send(&msg, sizeof(msg));

            // 3. Resposta da DB
            AuthResponse resp = {};
            if (m_mqToLeaveRoom.receive(&resp, sizeof(resp)) > 0) {

                if (resp.authorized) {
                    std::cout << "[RFID-EXIT] Saída Autorizada! UserID: " << resp.userId << std::endl;
                    m_failedAttempts = 0;

                    // 4. Abrir porta para sair
                    ActuatorCmd cmd = {ID_SERVO_ROOM, 0};
                    m_mqToActuator.send(&cmd, sizeof(cmd));

                    // 5. Log de Saída
                    sendLog((uint8_t)resp.userId, (uint16_t)resp.accessLevel, true);
                }
                else {
                    m_failedAttempts++;
                    std::cerr << "[RFID-EXIT] Saída Negada! Tentativa " << m_failedAttempts << std::endl;

                    if (m_failedAttempts >= m_maxAttempts) {
                        m_failedAttempts = 0;
                        ActuatorCmd alarm = {ID_ALARM_ACTUATOR, 1};
                        m_mqToActuator.send(&alarm, sizeof(alarm));
                    }
                    // Log de Alerta na saída
                    sendLog(0, 0, false);
                }
            }
        }
    }
}

void C_tLeaveRoomAccess::generateDescription(uint8_t userId, bool authorized, char* buffer, size_t size) {
    if (!authorized) {
        snprintf(buffer, size, "ACESSO NEGADO NA SAÍDA: Cartão não reconhecido");
    } else {
        // Semântica de SAÍDA
        snprintf(buffer, size, "Utilizador %d SAIU da sala", userId);
    }
}

void C_tLeaveRoomAccess::sendLog(uint8_t userId, uint16_t accessLevel, bool authorized) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;

    msg.payload.log.logType = authorized ? LOG_TYPE_ACCESS : LOG_TYPE_ALERT;
    msg.payload.log.entityID = userId;
    msg.payload.log.value = accessLevel;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(userId, authorized, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0);
}