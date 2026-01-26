/*
 * Flow: receive DB commands (add/delete), wait for fingerprint, control vault servo.
 */

#include "C_tVerifyVaultAccess.h"
#include <iostream>
#include <ctime>
#include <unistd.h>

#include "SharedTypes.h"


c_tVerifyVaultAccess::c_tVerifyVaultAccess(C_Monitor& m_monitorfgp,
                                         C_Monitor& m_monitorservovault,
                                         C_Fingerprint& m_fingerprint,
                                         C_Mqueue& m_mqToDatabase,
                                         C_Mqueue& m_mqToActuator,
                                         C_Mqueue& mqFromDatabase)
    : C_Thread(PRIO_MEDIUM),m_monitorfgp(m_monitorfgp),
      m_monitorservovault( m_monitorservovault),
      m_fingerprint(m_fingerprint),
      m_mqToDatabase(m_mqToDatabase),
      m_mqToActuator(m_mqToActuator),
      m_mqFromDatabase(mqFromDatabase)

{}

c_tVerifyVaultAccess::~c_tVerifyVaultAccess() = default;

void c_tVerifyVaultAccess::run() {
    std::cout << "[VaultAccess] Thread iniciada. Sensor Biométrico ativo." << std::endl;
    SensorData data={};

    AuthResponse cmdMsg = {};
    uint32_t pendingAddUserId = 0;

    while (!stopRequested()) {
        // Process pending commands from DB (add/delete biometrics).
        while (m_mqFromDatabase.timedReceive(&cmdMsg, sizeof(AuthResponse), 0) > 0) {
            if (cmdMsg.command == DB_CMD_ADD_USER) {
                pendingAddUserId = cmdMsg.payload.auth.userId;
            } else if (cmdMsg.command == DB_CMD_DELETE_USER) {
                m_fingerprint.wakeUp();
                m_fingerprint.deleteUser(static_cast<int>(cmdMsg.payload.auth.userId));
                m_fingerprint.sleep();
            }
        }

        // Wait for biometric sensor trigger.
        if (m_monitorfgp.timedWait(1)) {
            continue;
        }
        std::cout << "[finger ativou " <<  std::endl;
        m_fingerprint.wakeUp();

        if (pendingAddUserId > 0) {
            // Biometric user enrollment mode.
            m_fingerprint.addUser(static_cast<int>(pendingAddUserId));
            pendingAddUserId = 0;
            m_fingerprint.sleep();
            continue;
        }

        // Normal mode: authenticate and open vault.
        if (m_fingerprint.read(&data)) {
            if (data.data.fingerprint.authenticated) {
                ActuatorCmd cmd = {ID_SERVO_VAULT, 0};
                m_mqToActuator.send(&cmd, sizeof(cmd));
                
                sendLog(static_cast<uint32_t>(data.data.fingerprint.userID), true);

                // Wait for vault reed switch.
                while (!stopRequested()) {
                    if (!m_monitorservovault.timedWait(1)) {
                        break;
                    }
                }
                if (stopRequested()) {
                    break;
                }
                // Close the vault.
                cmd = {ID_SERVO_VAULT, 90};
                m_mqToActuator.send(&cmd, sizeof(cmd));
            } else {
                sendLog(0U, false);
            }
        }

        m_fingerprint.sleep();
    }

}




void c_tVerifyVaultAccess::generateDescription(uint32_t userId, bool authorized, char* buffer, size_t size) {
    if (authorized) {
        snprintf(buffer, size, "Cofre Aberto - Utilizador ID: %u", userId);
    } else {
        snprintf(buffer, size, "Tentativa falhada no Cofre - ID desconhecido");
    }
}

void c_tVerifyVaultAccess::sendLog(uint32_t userId, bool authorized) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;

    msg.payload.log.logType = authorized ? LOG_TYPE_ACCESS : LOG_TYPE_ALERT;

    // Log the event for UI and audit.
    msg.payload.log.entityID = userId;

    msg.payload.log.value = authorized ? 1.0 : 0.0;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(userId, authorized, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg));
}
