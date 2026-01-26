/*
 * Flow: wait for RFID event -> query DB -> trigger servo/alarms -> write log.
 */

#include "C_tVerifyRoomAccess.h"
#include <iostream>
#include <cstring>
#include <ctime>

C_tVerifyRoomAccess::C_tVerifyRoomAccess(C_Monitor& monitorrfid, C_Monitor& m_monitorservoroom, C_RDM6300& rfid, C_Mqueue& mqDB, C_Mqueue& mqFromDB,C_Mqueue& mqAct)
    : C_Thread(PRIO_MEDIUM),m_monitorrfid(monitorrfid),
      m_monitorservoroom(m_monitorservoroom),
      m_rfidEntry(rfid),
      m_mqToDatabase(mqDB), 
      m_mqToVerifyRoom(mqFromDB), 
      m_mqToActuator(mqAct), 
      m_maxAttempts(3),
      m_failedAttempts(0) {
    
}

C_tVerifyRoomAccess::~C_tVerifyRoomAccess() {
    
}


void C_tVerifyRoomAccess::run() {
    std::cout << "[VerifyRoomAccess] Thread iniciada. À espera de tags..." << std::endl;

    // Main loop: wait for RFID monitor signal.
    while (!stopRequested()) {

        // timedWait returns true on timeout; loop continues.
        if (m_monitorrfid.timedWait(1)) {
            continue;
        }

        SensorData data = {}; 

        // Read entry RFID.
        if (m_rfidEntry.read(&data)) {
            const char* rfidRead = data.data.rfid_single.tagID;
            std::cout << "[RFID entry] Cartão lido: " << rfidRead << std::endl;

            // Send authorization request to DB.
            DatabaseMsg msg = {};
            msg.command = DB_CMD_ENTER_ROOM_RFID;
            strncpy(msg.payload.rfid, rfidRead, sizeof(msg.payload.rfid) - 1);
            msg.payload.rfid[sizeof(msg.payload.rfid) - 1] = '\0';
            m_mqToDatabase.send(&msg, sizeof(msg));
            std::cout << "m enviafa: " << std::endl;

            // Wait for DB response with timeout to allow stop.
            while (!stopRequested()) {
                
                AuthResponse resp = {};
                ssize_t bytes = m_mqToVerifyRoom.timedReceive(&resp, sizeof(resp), 1);

                if (bytes > 0) {
                    // DB response: authorized vs. denied.
                    if (resp.payload.auth.authorized) {
                        std::cout << "[RFID] Acesso Autorizado! UserID: " << static_cast<unsigned int>(resp.payload.auth.userId) << std::endl;
                        m_failedAttempts = 0;

                        // Open room door and log access.
                        ActuatorCmd cmd = {ID_SERVO_ROOM, 0};
                        m_mqToActuator.send(&cmd, sizeof(cmd));
                        sendLog(static_cast<uint32_t>(resp.payload.auth.userId),
                                static_cast<uint32_t>(resp.payload.auth.accessLevel),
                                true);

                        // Wait for door reed switch to close.
                        while (!stopRequested()) {
                            if (!m_monitorservoroom.timedWait(1)) {
                                break; 
                            }
                        }

                        if (stopRequested()) break;
                        // Close room door.
                        cmd = {ID_SERVO_ROOM, 90};
                        m_mqToActuator.send(&cmd, sizeof(cmd));
                    }
                    else {
                        // Track failed attempts and trigger alarm if needed.
                        m_failedAttempts++;
                        std::cerr << "[RFID] Negado! Tentativa " << m_failedAttempts << "/" << m_maxAttempts << std::endl;

                        if (m_failedAttempts >= m_maxAttempts) {
                            m_failedAttempts = 0;
                            ActuatorCmd alarm = {ID_ALARM_ACTUATOR, 1};
                            m_mqToActuator.send(&alarm, sizeof(alarm));
                            sendLog(0U, 0U, false);
                        }
                    }
                    break; 
                }
            }
        }
    }

    std::cout << "[VerifyRoomAccess] Thread terminada com sucesso." << std::endl;
}


void C_tVerifyRoomAccess::generateDescription(uint32_t userId, bool authorized, char* buffer, size_t size) {
    if (!authorized) {
        // Access denied log message.
        snprintf(buffer, size, "ACESSO NEGADO: Cartão ou Utilizador não reconhecido");
    } else {
        // Access granted log message.
        snprintf(buffer, size, "Utilizador %u ENTROU na sala", userId);
    }
}

void C_tVerifyRoomAccess::sendLog(uint32_t userId, uint32_t accessLevel, bool authorized) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;

    // LOG_TYPE_ACCESS vs LOG_TYPE_ALERT.
    msg.payload.log.logType = authorized ? LOG_TYPE_ACCESS : LOG_TYPE_ALERT;
    msg.payload.log.entityID = userId;
    msg.payload.log.value = static_cast<double>(accessLevel);
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    // Human-readable description for UI.
    generateDescription(userId, authorized, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0);
}
