#include "C_tReadEnvSensor.h"
#include <iostream>
#include <ctime>
#include <cstring>
#include "SharedTypes.h"
#include "C_TH_SHT30.h"
#include "C_Monitor.h"
#include "C_Mqueue.h"



C_tReadEnvSensor::C_tReadEnvSensor(C_TH_SHT30& sensor,
                                   C_Mqueue& mqAct,
                                   C_Mqueue& mqDB,
                                   C_Mqueue& mqFromDb,
                                   int intervalSec,
                                   int threshold)
    : C_Thread(PRIO_LOW),  
      m_sensor(sensor),
      m_mqToActuator(mqAct),
      m_mqToDatabase(mqDB),
      m_mqFromDb(mqFromDb),
      m_tempThreshold(threshold),
      m_intervalSeconds(intervalSec),
      m_lastFanState(0)
{}


C_tReadEnvSensor::~C_tReadEnvSensor() = default;

void C_tReadEnvSensor::run() {
    std::cout << "[tReadEnv] Thread em execução.\n";

    {
        DatabaseMsg reqSettings = {};
        reqSettings.command = DB_CMD_GET_SETTINGS_THREAD;
        m_mqToDatabase.send(&reqSettings, sizeof(reqSettings));

        std::cout << "[tReadEnv] A pedir settings à BD..." << std::endl;

        // Espera resposta (máx 5s)
        AuthResponse settingsResp{};
        ssize_t bytes = m_mqFromDb.timedReceive(&settingsResp, sizeof(settingsResp), 5);

        if (bytes > 0 && settingsResp.command == DB_CMD_GET_SETTINGS_THREAD) {
            m_tempThreshold = settingsResp.payload.settings.tempThreshold;
            m_intervalSeconds = settingsResp.payload.settings.samplingInterval;
            std::cout << "[tReadEnv] Settings carregadas: threshold="
                      << m_tempThreshold << "°C, interval="
                      << m_intervalSeconds << "s" << std::endl;
        } else {
            std::cout << "[tReadEnv] AVISO: A usar valores default (BD não respondeu)" << std::endl;
        }
    }

    while (!stopRequested()) {
        AuthResponse cmdMsg{};
        ssize_t bytes =
            m_mqFromDb.timedReceive(&cmdMsg, sizeof(cmdMsg), m_intervalSeconds);

        if (bytes > 0) {
            if (cmdMsg.command == DB_CMD_STOP_ENV_SENSOR) {
                break;
            }
            if (cmdMsg.command == DB_CMD_UPDATE_SETTINGS) {
                m_tempThreshold   = cmdMsg.payload.settings.tempThreshold;
                m_intervalSeconds = cmdMsg.payload.settings.samplingInterval;
                if (m_intervalSeconds < 1) m_intervalSeconds = 1;

                std::cout << "[tReadEnv] Settings atualizadas: interval="
                          << m_intervalSeconds
                          << "s, threshold=" << m_tempThreshold << "\n";
            }

            continue;
        }

        SensorData data{};
        if (m_sensor.read(&data)) {
            float temp = data.data.tempHum.temp;
            float hum  = data.data.tempHum.hum;

            uint8_t fanValue = (temp > static_cast<float>(m_tempThreshold)) ? 1 : 0;

            if (fanValue != m_lastFanState) {
                ActuatorCmd cmd{ID_FAN, fanValue};
                m_lastFanState = fanValue;
                m_mqToActuator.send(&cmd, sizeof(cmd));
            }

            sendLog(static_cast<float>(temp),
                    static_cast<float>(hum));
        } else {
            std::cerr << "[tReadEnv] ERRO ao ler sensor!\n";
        }
    }

    std::cout << "[tReadEnv] Thread terminada\n";
}




void C_tReadEnvSensor::generateDescription(double temp, double hum,
                                          char* buffer, size_t size) {
    snprintf(buffer, size,
             "Leitura Ambiental: %.1f°C, %.1f HR",
             temp, hum);
}




// void C_tReadEnvSensor::sendLog(int temp, int hum) const {
void C_tReadEnvSensor::sendLog(double temp, double hum) const {
    DatabaseMsg msg = {};

    
    msg.command = DB_CMD_WRITE_LOG;

    
    msg.payload.log.logType = LOG_TYPE_SENSOR;
    msg.payload.log.entityID = ID_SHT31;  
    msg.payload.log.value = temp;
    msg.payload.log.value2 = hum;
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    
    generateDescription(temp, hum,
                       msg.payload.log.description,
                       sizeof(msg.payload.log.description));

    
    if (m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0)) {
        std::cout << "[tReadEnv] Log enviado para BD" << std::endl;
    } else {
        std::cerr << "[tReadEnv] ERRO ao enviar log para BD!" << std::endl;
    }
}
