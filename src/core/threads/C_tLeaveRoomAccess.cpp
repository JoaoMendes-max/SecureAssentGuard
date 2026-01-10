#include "C_tLeaveRoomAccess.h"
#include <iostream>
#include <cstring>
#include <ctime>

C_tLeaveRoomAccess::C_tLeaveRoomAccess(C_Monitor& monitorrfid, C_Monitor& monitorservoroom,
                                       C_RDM6300& rfid,
                                       C_Mqueue& mqDB,
                                       C_Mqueue& mqFromDB,
                                       C_Mqueue& mqAct)
    : m_monitorrfid( monitorrfid),
      m_monitorservoroom( monitorservoroom),
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
        m_monitorrfid.wait();
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

                if (resp.payload.auth.authorized) {
                    std::cout << "[RFID-EXIT] Saída Autorizada! UserID: " << resp.payload.auth.userId << std::endl;
                    m_failedAttempts = 0;

                    // 4. Abrir porta para sair
                    ActuatorCmd cmd = {ID_SERVO_ROOM, 0};
                    m_mqToActuator.send(&cmd, sizeof(cmd));

                    // 5. Log de Saída
                    sendLog((uint8_t)resp.payload.auth.userId, (uint16_t)resp.payload.auth.accessLevel);


                    m_monitorservoroom.wait();
                    cmd = {ID_SERVO_ROOM, 90};//para ficar preso
                    m_mqToActuator.send(&cmd, sizeof(cmd));

                }
            }
        }
    }
}

void C_tLeaveRoomAccess::generateDescription(uint8_t userId, char* buffer, size_t size) {

        snprintf(buffer, size, "Utilizador %d SAIU da sala", userId);
}

void C_tLeaveRoomAccess::sendLog(uint8_t userId, uint16_t accessLevel) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;

    msg.payload.log.logType = LOG_TYPE_ACCESS ;
    msg.payload.log.entityID = userId;
    msg.payload.log.value = accessLevel;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(userId, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0);
}