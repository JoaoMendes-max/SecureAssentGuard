/*
 * Flow: wait for exit RFID -> query DB -> open door -> log -> close door.
 */

#include "C_tLeaveRoomAccess.h"
#include <iostream>
#include <cstring>
#include <ctime>

C_tLeaveRoomAccess::C_tLeaveRoomAccess(C_Monitor& monitorrfid, C_Monitor& monitorservoroom,
                                       C_RDM6300& rfid,
                                       C_Mqueue& mqDB,
                                       C_Mqueue& mqFromDB,
                                       C_Mqueue& mqAct)
    :C_Thread(PRIO_MEDIUM), m_monitorrfid( monitorrfid),
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

    // Main loop: wait for RFID monitor signal.
    while (!stopRequested()) {

        // Timeout to allow graceful stop.
        if (m_monitorrfid.timedWait(1)) {
            continue;
        }

        SensorData data = {};
        // Read exit RFID.
        if (m_rfidExit.read(&data)) {
            const char* rfidRead = data.data.rfid_single.tagID;
            std::cout << "[RFID-EXIT] Cartão lido: " << rfidRead << std::endl;

            // Send exit request to DB.
            DatabaseMsg msg = {};
            msg.command = DB_CMD_LEAVE_ROOM_RFID; 
            strncpy(msg.payload.rfid, rfidRead, sizeof(msg.payload.rfid) - 1);
            msg.payload.rfid[sizeof(msg.payload.rfid) - 1] = '\0';
            m_mqToDatabase.send(&msg, sizeof(msg));

            AuthResponse resp = {};

            // Wait for DB response.
            while (!stopRequested()) {
                ssize_t bytes = m_mqToLeaveRoom.timedReceive(&resp, sizeof(resp), 1);

                if (bytes > 0) {
                    // Authorized: open door and log event.
                    if (resp.payload.auth.authorized) {
                        std::cout << "[RFID-EXIT] Saída Autorizada! UserID: " << static_cast<unsigned int>(resp.payload.auth.userId) << std::endl;
                        m_failedAttempts = 0;

                        ActuatorCmd cmd = {ID_SERVO_ROOM, 0};
                        m_mqToActuator.send(&cmd, sizeof(cmd));

                        // Exit log.
                        sendLog(static_cast<uint32_t>(resp.payload.auth.userId),
                                static_cast<uint32_t>(resp.payload.auth.accessLevel));

                        // Wait for reed switch indicating close.
                        while (!stopRequested()) {
                            
                            if (!m_monitorservoroom.timedWait(1)) {
                                break;
                            }
                        }
                        
                        if (stopRequested()) {
                            break; 
                        }

                        // Close door after passage.
                        cmd = {ID_SERVO_ROOM, 90};
                        m_mqToActuator.send(&cmd, sizeof(cmd));
                    }

                    // If not authorized, just exit the loop (no action).
                    break;
                }
            }
        }
    }

    std::cout << "[LeaveRoom] Thread terminada com sucesso." << std::endl;
}

void C_tLeaveRoomAccess::generateDescription(uint32_t userId, char* buffer, size_t size) {

        // Text message for logs.
        snprintf(buffer, size, "Utilizador %u SAIU da sala", userId);
}

void C_tLeaveRoomAccess::sendLog(uint32_t userId, uint32_t accessLevel) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;

    msg.payload.log.logType = LOG_TYPE_ACCESS ;
    msg.payload.log.entityID = userId;
    msg.payload.log.value = static_cast<double>(accessLevel);
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(userId, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0);
}
