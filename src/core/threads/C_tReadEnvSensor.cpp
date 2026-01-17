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
    std::cout << "[tReadEnv] Thread em execução. A aguardar primeiro ciclo..."
              << std::endl;


    while (!stopRequested()) {
        // ========================================
        // ESPERA PERIÓDICA (usa timedWait do monitor!)
        // Retorna true se timeout, false se signal
        // ========================================

        AuthResponse cmdMsg = {};
        ssize_t bytes = m_mqFromDb.timedReceive(&cmdMsg, sizeof(AuthResponse), 0);

        if (bytes > 0 && cmdMsg.command == DB_CMD_UPDATE_SETTINGS) {
            m_tempThreshold = cmdMsg.payload.settings.tempThreshold;
            m_intervalSeconds = cmdMsg.payload.settings.samplingInterval;
        }

        bool wasTimeout = m_monitor.timedWait(m_intervalSeconds);

        if (!wasTimeout) {
            // Acordou por signal() externo (ex: paragem)
            std::cout << "[tReadEnv] Signal recebido. A terminar..." << std::endl;
            break;
        }

        // Acordou por timeout → fazer leitura periódica
        std::cout << "[tReadEnv] Timeout! A ler sensor ambiental..." << std::endl;

        // ========================================
        // 1. LER SENSOR (igual ao padrão)
        // ========================================
        SensorData data = {};

        if (m_sensor.read(&data)) {
            int temp = data.data.tempHum.temp;
            int hum = data.data.tempHum.hum;

            std::cout << "[tReadEnv] Temp: " << temp
                     << "°C, Humidade: " << hum << "%" << std::endl;

            // ========================================
            // 2. VERIFICAR THRESHOLD E CONTROLAR VENTOINHA
            // ========================================
            uint8_t fanValue = (temp > m_tempThreshold) ? 1 : 0;

            if (fanValue != m_lastFanState) {
                ActuatorCmd cmd = {ID_FAN, fanValue};
                m_lastFanState = fanValue;

                if (m_mqToActuator.send(&cmd, sizeof(cmd))) {
                    std::cout << "[tReadEnv] Comando enviado: Ventoinha "
                             << (fanValue ? "LIGADA" : "DESLIGADA") << std::endl;
                } else {
                    std::cerr << "[tReadEnv] ERRO ao enviar comando para atuador!"
                             << std::endl;
                }
            }

            // ========================================
            // 3. ENVIAR LOG PARA BASE DE DADOS
            // (Igual ao padrão das outras threads)
            // ========================================
            sendLog(temp, hum);

        } else {
            std::cerr << "[tReadEnv] ERRO ao ler sensor SHT31!" << std::endl;
        }

        // Loop recomeça → timedWait() → dorme novamente
    }

    std::cout << "[tReadEnv] Thread terminada" << std::endl;
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
