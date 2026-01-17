#include "C_tVerifyVaultAccess.h"
#include <iostream>
#include <ctime>
#include <unistd.h>

#include "SharedTypes.h"

// Construtor usando apenas o que pediste
c_tVerifyVaultAccess::c_tVerifyVaultAccess(C_Monitor& m_monitorfgp,
                                         C_Monitor& m_monitorservovault,
                                         C_Fingerprint& m_fingerprint,
                                         C_Mqueue& m_mqToDatabase,
                                         C_Mqueue& m_mqToActuator,
                                         C_Mqueue& mqFromDatabase)
    : m_monitorfgp(m_monitorfgp),
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

    AuthResponse cmdMsg = {}; // Buffer para receber comandos da DB

    while (!stopRequested()) {
        if (m_monitorfgp.timedWait(1)) {
            continue;
        }
        m_fingerprint.wakeUp();
        ssize_t bytes = m_mqFromDatabase.timedReceive(&cmdMsg, sizeof(AuthResponse), 1);

        if (bytes > 0) {
            if (cmdMsg.command == DB_CMD_ADD_USER) { // Precisas definir este comando no SharedTypes.h
                m_fingerprint.addUser(cmdMsg.payload.auth.userId);
            } else if (cmdMsg.command == DB_CMD_DELETE_USER) {
                m_fingerprint.deleteUser(cmdMsg.payload.auth.userId);
            }
        }

        if (m_fingerprint.read(&data)) {
            if (data.data.fingerprint.authenticated) {
                ActuatorCmd cmd = {ID_SERVO_VAULT, 0};//para ficar solto
                m_mqToActuator.send(&cmd, sizeof(cmd));
                //manda log de acesso bem sucedido
                sendLog(data.data.fingerprint.userID,true);

                while (!stopRequested()) {
                    if (!m_monitorservovault.timedWait(1)) {
                        break;
                    }
                }
                if (stopRequested()) {
                    break;
                }
                cmd = {ID_SERVO_VAULT, 90};//para ficar preso
                m_mqToActuator.send(&cmd, sizeof(cmd));
            }else {
                //se nao for reconhecida
                sendLog(0,false);
            }
        }

        m_fingerprint.sleep();
    }

}


void c_tVerifyVaultAccess::generateDescription(int userId, bool authorized, char* buffer, size_t size) {
    if (authorized) {
        // Usamos %d porque o userId é int
        snprintf(buffer, size, "Cofre Aberto - Utilizador ID: %d", userId);
    } else {
        snprintf(buffer, size, "Tentativa falhada no Cofre - ID desconhecido");
    }
}

void c_tVerifyVaultAccess::sendLog(int userId, bool authorized) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;

    msg.payload.log.logType = authorized ? LOG_TYPE_ACCESS : LOG_TYPE_ALERT;

    // O entityID recebe o teu int. O co,mpilador faz o cast automático.
    msg.payload.log.entityID = userId;

    msg.payload.log.value = authorized ? 1 : 0;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(userId, authorized, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg));
}
