#include "C_tAct.h"
#include "C_Mqueue.h"
#include "C_Actuator.h"
#include <iostream>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cerrno>

using namespace std;

static constexpr const char* MODULE_NAME = "[tAct]";

// ============================================
// CONSTRUTOR
// ============================================
C_tAct::C_tAct(C_Mqueue& mqIn,
               C_Mqueue& mqOut,
               const std::array<C_Actuator*, ID_ACTUATOR_COUNT>& listaAtuadores)
    : C_Thread(PRIO_HIGH), // Prioridade definida no Enum
      m_mqToActuator(mqIn),
      m_mqToDatabase(mqOut),
      m_actuators(listaAtuadores)
{
    size_t count = 0;
    for (size_t i = 0; i < m_actuators.size(); ++i) {
        if (m_actuators[i] != nullptr) {
            ++count;
        } else {
            cerr << MODULE_NAME << " AVISO: Atuador " << i
                 << " não configurado" << endl;
        }
    }

    cout << MODULE_NAME << " Thread criada (Prio " << PRIO_HIGH << ")"
         << ". Atuadores: " << count << "/" << m_actuators.size() << endl;
}

// ============================================
// RUN (Loop Bloqueante)
// ============================================
void C_tAct::run() {
    cout << MODULE_NAME << " Iniciada..." << endl;
    ActuatorCmd msg;

    while (true) {
        // Bloqueia até receber mensagem (0% CPU)
        ssize_t bytes = m_mqToActuator.receive(&msg, sizeof(msg));

        if (bytes == sizeof(ActuatorCmd)) {
            processMessage(msg);
        } else if (bytes < 0) {
            cerr << MODULE_NAME << " ERRO: Falha na fila " << strerror(errno) << endl;
            // Erro fatal (ex: fila destruída). Sai do loop.
            break;
        } else {
            cerr << MODULE_NAME << " AVISO: Mensagem corrompida (" << bytes << " bytes)" << endl;
        }
    }

    cout << MODULE_NAME << " Terminada" << endl;
}

// ============================================
// PROCESS MESSAGE
// ============================================
void C_tAct::processMessage(const ActuatorCmd& msg) {
    // 1. Validação
    if (!isValidActuatorID(msg.actuatorID)) {
        cerr << MODULE_NAME << " ERRO: ID inválido " << (int)msg.actuatorID << endl;
        return;
    }

    C_Actuator* actuator = m_actuators[msg.actuatorID];
    if (!actuator) {
        cerr << MODULE_NAME << " ERRO: Atuador não inicializado" << endl;
        return;
    }

    // 2. Execução (Polimorfismo)
    cout << MODULE_NAME << " Comando: " << ACTUATOR_NAMES[msg.actuatorID]
         << " -> " << (int)msg.value << endl;

    bool sucesso = actuator->set_value(msg.value);

    // 3. Log
    if (sucesso) {
        logToDatabase(msg.actuatorID, msg.value);
    } else {
        cerr << MODULE_NAME << " FALHA Hardware: " << ACTUATOR_NAMES[msg.actuatorID] << endl;
    }
}

// ============================================
// GENERATE DESCRIPTION
// ============================================
void C_tAct::generateDescription(ActuatorID_enum id,
                                uint8_t value,
                                char* buffer,
                                size_t size)
{
    if (!buffer || size == 0) return;

    // NOTA: Os servos usam 0 para desligar/livre (PWM OFF)

    switch (id) {
        case ID_SERVO_ROOM:
        case ID_SERVO_VAULT:
            if (value == 0) {
                snprintf(buffer, size, "Porta LIVRE (PWM OFF)");
            } else {
                snprintf(buffer, size, "Porta TRANCADA (%d°)", value);
            }
            break;

        case ID_FAN:
            snprintf(buffer, size, "Ventoinha %s",
                    (value > 0) ? "LIGADA" : "DESLIGADA");
            break;

        case ID_ALARM_ACTUATOR:
            snprintf(buffer, size, "Alarme %s",
                    (value > 0) ? "ATIVADO" : "DESATIVADO");
            break;

        default:
            snprintf(buffer, size, "Atuador %d Val %d", (int)id, value);
    }

    buffer[size - 1] = '\0';
}

// ============================================
// LOG TO DATABASE
// ============================================
void C_tAct::logToDatabase(ActuatorID_enum id, uint8_t value) {
    //mudar esta shit para mandar msg ahahah(ber verifyroomaccess
    DatabaseLog log;

    log.logType = LOG_TYPE_ACTUATOR;
    log.entityID = static_cast<uint8_t>(id);
    log.value = static_cast<uint16_t>(value); // Casting para uint16_t
    log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(id, value, log.description, sizeof(log.description));

    bool enviado = m_mqToDatabase.send(&log, sizeof(DatabaseLog), 0);

    if (!enviado) {
        cerr << MODULE_NAME << " ERRO ao enviar log" << endl;
    }
}