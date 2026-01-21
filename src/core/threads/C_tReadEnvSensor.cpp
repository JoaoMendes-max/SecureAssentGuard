#include "C_tReadEnvSensor.h"
#include <iostream>
#include <ctime>
#include <cstring>
#include "SharedTypes.h"
#include "C_TH_SHT30.h"
#include "C_Monitor.h"
#include "C_Mqueue.h"
// ============================================
// CONSTRUTOR
// ============================================
C_tReadEnvSensor::C_tReadEnvSensor(C_Monitor& monitor,
                                   C_TH_SHT30& sensor,
                                   C_Mqueue& mqAct,
                                   C_Mqueue& mqDB,
                                   C_Mqueue& mqFromDb,
                                   int intervalSec,
                                   int threshold)
    : C_Thread(PRIO_LOW),  // Prioridade baixa (como definido no design)
      m_monitor(monitor),
      m_sensor(sensor),
      m_mqToActuator(mqAct),
      m_mqToDatabase(mqDB),
      m_mqFromDb(mqFromDb),
      m_tempThreshold(threshold),
      m_intervalSeconds(intervalSec),
      m_lastFanState(0)
{}

// ============================================
// DESTRUTOR
// ============================================
C_tReadEnvSensor::~C_tReadEnvSensor() = default;

// ============================================
// RUN - Loop principal (IGUAL ao padrão das outras threads)
// ============================================
void C_tReadEnvSensor::run() {
    std::cout << "[tReadEnv] Thread em execução.\n";

    while (!stopRequested()) {

        // Espera por 2 coisas:
        // - mensagem DB_CMD_UPDATE_SETTINGS (acorda logo)
        // - timeout = m_intervalSeconds (faz leitura)
        AuthResponse cmdMsg{};
        ssize_t bytes = m_mqFromDb.timedReceive(&cmdMsg, sizeof(cmdMsg), m_intervalSeconds);

        if (bytes > 0) {
            // Recebeu algo -> aplica settings (e drena mais msgs pendentes)
            if (cmdMsg.command == DB_CMD_UPDATE_SETTINGS) {
                m_tempThreshold   = cmdMsg.payload.settings.tempThreshold;
                m_intervalSeconds = cmdMsg.payload.settings.samplingInterval;
                if (m_intervalSeconds < 1) m_intervalSeconds = 1;

                std::cout << "[tReadEnv] Settings atualizadas: interval="
                          << m_intervalSeconds << "s, threshold=" << m_tempThreshold << "\n";
            }

            // Drenar msgs extra que já estejam na fila (non-blocking)
            while (m_mqFromDb.timedReceive(&cmdMsg, sizeof(cmdMsg), 0) > 0) {
                if (cmdMsg.command == DB_CMD_UPDATE_SETTINGS) {
                    m_tempThreshold   = cmdMsg.payload.settings.tempThreshold;
                    m_intervalSeconds = cmdMsg.payload.settings.samplingInterval;
                    if (m_intervalSeconds < 1) m_intervalSeconds = 1;
                }
            }

            // Se quiseres “logo logo” uma leitura após mudar settings, descomenta:
            // goto doRead;

            continue; // volta a esperar com o novo intervalo já aplicado
        }

        // timeout -> fazer leitura periódica
        // doRead:
        SensorData data{};
        if (m_sensor.read(&data)) {
            int temp = data.data.tempHum.temp;
            int hum  = data.data.tempHum.hum;

            uint8_t fanValue = (temp > m_tempThreshold) ? 1 : 0;
            if (fanValue != m_lastFanState) {
                ActuatorCmd cmd{ID_FAN, fanValue};
                m_lastFanState = fanValue;
                m_mqToActuator.send(&cmd, sizeof(cmd));
            }

            sendLog(temp, hum);
        } else {
            std::cerr << "[tReadEnv] ERRO ao ler sensor!\n";
        }
    }

    std::cout << "[tReadEnv] Thread terminada\n";
}

// ============================================
// GENERATE DESCRIPTION (igual ao padrão)
// ============================================
void C_tReadEnvSensor::generateDescription(int temp, int hum,
                                          char* buffer, size_t size) {
    snprintf(buffer, size,
             "Leitura Ambiental: %d°C, %d HR",
             temp, hum);
}

// ============================================
// SEND LOG (EXATAMENTE igual às outras threads!)
// ============================================
void C_tReadEnvSensor::sendLog(int temp, int hum) const {
    DatabaseMsg msg = {};

    // Comando para gravar log
    msg.command = DB_CMD_WRITE_LOG;

    // Preencher estrutura do log
    msg.payload.log.logType = LOG_TYPE_SENSOR;
    msg.payload.log.entityID = ID_SHT31;  // ID do sensor no enum
    msg.payload.log.value = static_cast<uint16_t>(temp);
    msg.payload.log.value2 = static_cast<uint16_t>(hum);   // Humidade (novo campo)
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    // Gerar descrição
    generateDescription(temp, hum,
                       msg.payload.log.description,
                       sizeof(msg.payload.log.description));

    // Enviar para a base de dados
    if (m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0)) {
        std::cout << "[tReadEnv] Log enviado para BD" << std::endl;
    } else {
        std::cerr << "[tReadEnv] ERRO ao enviar log para BD!" << std::endl;
    }
}
