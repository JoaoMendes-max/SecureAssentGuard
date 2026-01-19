#include "C_tVerifyRoomAccess.h"
#include <iostream>
#include <cstring>
#include <ctime>
// O construtor inicializa todas as referências passadas pelo processo principal
C_tVerifyRoomAccess::C_tVerifyRoomAccess(C_Monitor& monitorrfid, C_Monitor& m_monitorservoroom, C_RDM6300& rfid, C_Mqueue& mqDB, C_Mqueue& mqFromDB,C_Mqueue& mqAct)
    : m_monitorrfid(monitorrfid),
      m_monitorservoroom(m_monitorservoroom),
      m_rfidEntry(rfid),
      m_mqToDatabase(mqDB), // Fila de saída para a Base de Dados
      m_mqToVerifyRoom(mqFromDB), // Fila de entrada com a resposta da BD
      m_mqToActuator(mqAct), // Fila de saída para os Actuadores
      m_maxAttempts(3),
      m_failedAttempts(0) {
    // m_maxAttempts já está inicializado a 3 no header
}

C_tVerifyRoomAccess::~C_tVerifyRoomAccess() {
    // O fecho das queues é feito pelos objetos C_Mqueue no processo principal
}


void C_tVerifyRoomAccess::run() {
    std::cout << "[VerifyRoomAccess] Thread iniciada. À espera de tags..." << std::endl;

    // 1. LOOP PRINCIPAL: Vigilância do RFID de entrada
    while (!stopRequested()) {

        // Espera pelo sinal do sensor RFID (1 segundo de cada vez)
        if (m_monitorrfid.timedWait(1)) {
            continue;
        }

        SensorData data = {}; // Limpo a cada nova deteção

        // Tentar ler a tag do sensor
        if (m_rfidEntry.read(&data)) {
            char* rfidRead = data.data.rfid_single.tagID;
            std::cout << "[RFID] Cartão lido: " << rfidRead << std::endl;

            // 2. PEDIDO À BASE DE DADOS
            DatabaseMsg msg = {};
            msg.command = DB_CMD_ENTER_ROOM_RFID;
            strncpy(msg.payload.rfid, rfidRead, 11);
            m_mqToDatabase.send(&msg, sizeof(msg));

            // 3. LOOP INTERNO: Espera pela Resposta da DB
            while (!stopRequested()) {
                AuthResponse resp = {};
                ssize_t bytes = m_mqToVerifyRoom.timedReceive(&resp, sizeof(resp), 1);

                if (bytes > 0) {
                    // RESPOSTA RECEBIDA
                    if (resp.payload.auth.authorized) {
                        std::cout << "[RFID] Acesso Autorizado! UserID: " << (int)resp.payload.auth.userId << std::endl;
                        m_failedAttempts = 0;

                        // Abrir porta (0 graus)
                        ActuatorCmd cmd = {ID_SERVO_ROOM, 0};
                        m_mqToActuator.send(&cmd, sizeof(cmd));
                        sendLog((uint8_t)resp.payload.auth.userId, (uint16_t)resp.payload.auth.accessLevel, true);

                        // 4. ESPERA PELO FECHO DA PORTA (Servo)
                        while (!stopRequested()) {
                            if (!m_monitorservoroom.timedWait(1)) {
                                break; // Porta fechou (recebeu sinal)
                            }
                        }

                        if (stopRequested()) break;

                        // Trancar porta (90 graus)
                        cmd = {ID_SERVO_ROOM, 90};
                        m_mqToActuator.send(&cmd, sizeof(cmd));
                    }
                    else {
                        // ACESSO NEGADO
                        m_failedAttempts++;
                        std::cerr << "[RFID] Negado! Tentativa " << m_failedAttempts << "/" << m_maxAttempts << std::endl;

                        if (m_failedAttempts >= m_maxAttempts) {
                            m_failedAttempts = 0;
                            ActuatorCmd alarm = {ID_ALARM_ACTUATOR, 1};
                            m_mqToActuator.send(&alarm, sizeof(alarm));
                            sendLog(0, 0, false);
                        }
                    }
                    break; // Sai do loop da DB e volta a esperar novo cartão
                }
                if (bytes < 0 && errno == ETIMEDOUT) {
                    continue; // DB lenta, insiste na leitura
                }
                else {
                    break; // Erro na fila
                }
            }
        }
    }

    std::cout << "[VerifyRoomAccess] Thread terminada com sucesso." << std::endl;
}

void C_tVerifyRoomAccess::generateDescription(uint8_t userId, bool authorized, char* buffer, size_t size) {
    if (!authorized) {
        // Se não foi reconhecido, não tentamos mostrar o ID
        snprintf(buffer, size, "ACESSO NEGADO: Cartão ou Utilizador não reconhecido");
    } else {
        // Se é esta thread, é porque ENTROU
        snprintf(buffer, size, "Utilizador %d ENTROU na sala", userId);
    }
}

void C_tVerifyRoomAccess::sendLog(uint8_t userId, uint16_t accessLevel, bool authorized) {
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG;

    // A tua regra: autorizado é ACCESS, não autorizado é ALERT
    msg.payload.log.logType = authorized ? LOG_TYPE_ACCESS : LOG_TYPE_ALERT;
    msg.payload.log.entityID = userId;
    msg.payload.log.value = accessLevel;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    // Já não passamos o isInside, a descrição é fixa pela função acima
    generateDescription(userId, authorized, msg.payload.log.description, sizeof(msg.payload.log.description));

    m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0);
}