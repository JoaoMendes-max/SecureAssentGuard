#include "C_tVerifyVaultAccess.h"
#include <iostream>
#include <ctime>
#include <unistd.h>

#include "SharedTypes.h"

// Construtor usando apenas o que pediste
c_tVerifyVaultAccess::c_tVerifyVaultAccess(C_Monitor& m_monitor,
                                         C_Fingerprint& m_fingerprint,
                                         C_Mqueue& m_mqToDatabase,
                                         C_Mqueue& m_mqToActuator)
    : m_monitor(m_monitor),
      m_fingerprint(m_fingerprint),
      m_mqToDatabase(m_mqToDatabase),
      m_mqToActuator(m_mqToActuator)

{
}

c_tVerifyVaultAccess::~c_tVerifyVaultAccess() {
}

void c_tVerifyVaultAccess::run() {
    std::cout << "[VaultAccess] Thread iniciada. Sensor Biométrico ativo." << std::endl;
    SensorData data={};

    while (true) {
        m_monitor.wait();
        if (m_fingerprint.read(&data)) {
            if (data.data.fingerprint.authenticated) {
                ActuatorCmd cmd = {ID_SERVO_VAULT, 0};//para ficar solto
                m_mqToActuator.send(&cmd, sizeof(cmd));
                //manda log de acesso bem sucedido
                sendLog(data.data.fingerprint.userID,true);
            }else {
                //se nao for reconhecida
                sendLog(0,false);
            }
        }
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
}