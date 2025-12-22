#include "C_tVerifyRoomAccess.h"
#include <iostream>
#include <cstring>
#include <ctime>

// O construtor inicializa todas as referências passadas pelo processo principal
C_tVerifyRoomAccess::C_tVerifyRoomAccess(C_Monitor& monitor,
                                       C_RDM6300& rfid,
                                       C_Mqueue& mqDB,
                                       C_Mqueue& mqFromDB,
                                       C_Mqueue& mqAct)
    : m_monitor(monitor),
      m_rfidEntry(rfid),
      m_mqToDatabase(mqDB),  // Fila de saída para a Base de Dados
      m_mqToRfid(mqFromDB),  // Fila de entrada com a resposta da BD
      m_mqToActuator(mqAct), // Fila de saída para os Actuadores
      m_maxAttempts(3),
      m_failedAttempts(0)
{
    // m_maxAttempts já está inicializado a 3 no header
}

C_tVerifyRoomAccess::~C_tVerifyRoomAccess() {
    // O fecho das queues é feito pelos objetos C_Mqueue no processo principal
}

void C_tVerifyRoomAccess::run() {
    std::cout << "[VerifyRoomAccess] Thread em execução. À espera de tags..." << std::endl;

    while (true) {
        m_monitor.wait();
        SensorData data={};// confitma esta merda de criar locais !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

        // 1. Tentar ler uma tag do sensor RDM6300
        if (m_rfidEntry.read(&data)) {
            // O ID da tag está aqui: sData.data.rfid_single.tagID
            char* rfidRead = data.data.rfid_single.tagID;
            std::cout << "[RFID] Cartão lido: " << rfidRead << std::endl;

            // 2. PEDIDO À BASE DE DADOS
            DatabaseMsg msg={};
            msg.command = DB_CMD_VERIFY_RFID;
            strncpy(msg.payload.rfid, rfidRead, 11);//so para copiar o q esta no rfid porque nao podemos usar = supostamente
            m_mqToDatabase.send(&msg, sizeof(msg));

            // 3. ESPERA RESPOSTA DA DB
            AuthResponse resp={};
            if (m_mqToRfid.receive(&resp, sizeof(resp)) > 0) {
                //se tiver autorizado so vai mandar o log de acesso
                if (resp.authorized) {
                    std::cout << "[RFID] Acesso Autorizado! UserID: " << resp.userId << std::endl;
                    m_failedAttempts = 0;
                    ActuatorCmd cmd = {ID_SERVO_ROOM, 0};//para ficar solto
                    m_mqToActuator.send(&cmd, sizeof(cmd));
                    sendLog((uint8_t)resp.userId, (uint16_t)resp.accessLevel, resp.isInside);
                }
                else {
                    m_failedAttempts++;
                    std::cerr << "[RFID] Negado! Tentativa " << m_failedAttempts << "/" << m_maxAttempts << std::endl;
                    if (m_failedAttempts >= m_maxAttempts) {
                        ActuatorCmd msg1;
                        m_failedAttempts = 0;
                        msg1.actuatorID=ID_ALARM_ACTUATOR;
                        msg1.value=1;
                        m_mqToActuator.send(&msg1, sizeof(msg1));
                        sendLog((uint8_t)resp.userId, (uint16_t)resp.accessLevel, resp.isInside);
                    }
                }
            }
        }
    }
}

void C_tVerifyRoomAccess::generateDescription(uint8_t userId, bool authorized, bool isInside, char* buffer, size_t size) {
    if (!authorized) {
        snprintf(buffer, size, "Tentativa de ACESSO NEGADO (User ID: %d)", userId);
    } else {
        snprintf(buffer, size, "Utilizador %d %s a sala",
                 userId, isInside ? "entrou n" : "saiu d");
    }
}






void C_tVerifyRoomAccess::sendLog(uint8_t userId, uint16_t accessLevel, bool authorized, bool isInside) {
    DatabaseMsg msg = {}; // Inicialização garantida
    msg.command = DB_CMD_WRITE_LOG;

    // A tua regra: se authorized é ACCESS, senão é ALERT
    msg.payload.log.logType = authorized ? LOG_TYPE_ACCESS : LOG_TYPE_ALERT;
    msg.payload.log.entityID = userId;
    msg.payload.log.value = accessLevel;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    // Descrição automática
    generateDescription(userId, authorized, isInside, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0);
}