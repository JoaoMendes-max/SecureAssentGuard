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

C_tAct::C_tAct(C_Mqueue& mqIn,
               C_Mqueue& mqOut,
               const std::array<C_Actuator*, ID_ACTUATOR_COUNT>& listaAtuadores)
    : C_Thread(PRIO_HIGH), 
      m_mqToActuator(mqIn),
      m_mqToDatabase(mqOut),
      m_actuators(listaAtuadores),
      m_alarmTimerId(0) 
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

    initTimer();

    cout << MODULE_NAME << " Thread criada (Prio " << PRIO_HIGH << ")"
         << ". Atuadores: " << count << "/" << m_actuators.size() << endl;
}

C_tAct::~C_tAct() {
    
    if (m_alarmTimerId != 0) {
        timer_delete(m_alarmTimerId);
    }
}

void C_tAct::initTimer() {

    struct sigevent sev = {};
    
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = alarmTimerCallback; 
    sev.sigev_value.sival_ptr = this; 
    sev.sigev_notify_attributes = nullptr;

    if (timer_create(CLOCK_MONOTONIC, &sev, &m_alarmTimerId) == -1) {
        cerr << MODULE_NAME << " ERRO CRÍTICO: Falha ao criar timer POSIX!" << endl;
    }
}

void C_tAct::alarmTimerCallback(union sigval sv) {
    
    C_tAct* self = static_cast<C_tAct*>(sv.sival_ptr);

    if (self) {
        cout << "[tAct-Timer] Tempo esgotado! A enviar comando OFF..." << endl;

        
        ActuatorCmd cmd;
        cmd.actuatorID = ID_ALARM_ACTUATOR;
        cmd.value = 0;
        self->m_mqToActuator.send(&cmd, sizeof(cmd));
    }
}

void C_tAct::startAlarmTimer(int seconds) {
    struct itimerspec its;

    
    its.it_value.tv_sec = seconds;
    its.it_value.tv_nsec = 0;

    
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    if (timer_settime(m_alarmTimerId, 0, &its, nullptr) == -1) {
        cerr << MODULE_NAME << " ERRO ao armar timer" << endl;
    } else {
        cout << MODULE_NAME << " Timer armado para " << seconds << "s" << endl;
    }
}

void C_tAct::stopAlarmTimer() {
    struct itimerspec its;
    
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    timer_settime(m_alarmTimerId, 0, &its, nullptr);
}

void C_tAct::run() {
    cout << MODULE_NAME << " Iniciada..." << endl;
    ActuatorCmd msg;

    while (!stopRequested()) {
        
        ssize_t bytes = m_mqToActuator.timedReceive(&msg, sizeof(msg), 1);

        if (bytes == sizeof(ActuatorCmd)) {
            processMessage(msg);
        } else if (bytes < 0) {
            if (errno == ETIMEDOUT) {
                continue;
            }
            if (stopRequested()) {
                break;
            }
        } else {
            cerr << MODULE_NAME << " AVISO: Mensagem corrompida (" << bytes << " bytes)" << endl;
        }
    }
    stopAlarmTimer();

    cout << MODULE_NAME << " Terminada" << endl;
}

void C_tAct::processMessage(const ActuatorCmd& msg) {
    
    if (!isValidActuatorID(msg.actuatorID)) {
        cerr << MODULE_NAME << " ERRO: ID inválido " << static_cast<int>(msg.actuatorID) << endl;
        return;
    }

    C_Actuator* actuator = m_actuators[msg.actuatorID];
    if (!actuator) {
        cerr << MODULE_NAME << " ERRO: Atuador não inicializado" << endl;
        return;
    }

    cout << MODULE_NAME << " Comando: " << ACTUATOR_NAMES[msg.actuatorID]
         << " -> " << static_cast<int>(msg.value) << endl;

    bool sucesso = actuator->set_value(msg.value);

    if (sucesso && msg.actuatorID == ID_ALARM_ACTUATOR) {
        if (msg.value == 1) {
            
            startAlarmTimer(30);
        } else {
            
            
            stopAlarmTimer();
        }
    }

    
    if (sucesso) {
        sendLog(msg.actuatorID, msg.value);
    } else {
        cerr << MODULE_NAME << " FALHA Hardware: " << ACTUATOR_NAMES[msg.actuatorID] << endl;
    }
}




void C_tAct::generateDescription(ActuatorID_enum id,
                                uint8_t value,
                                char* buffer,
                                size_t size)
{
    if (!buffer || size == 0) return;

    

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
            snprintf(buffer, size, "Atuador %d Val %d", static_cast<int>(id), value);
    }

    buffer[size - 1] = '\0';
}




void C_tAct::sendLog(ActuatorID_enum id, uint8_t value) {
    
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG; 

    
    msg.payload.log.logType = LOG_TYPE_ACTUATOR;
    msg.payload.log.entityID = static_cast<uint8_t>(id);
    msg.payload.log.value = static_cast<double>(value);
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(id, value, msg.payload.log.description, sizeof(msg.payload.log.description));

    
    bool enviado = m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0);

    if (!enviado) {
        cerr << MODULE_NAME << " ERRO ao enviar log (DatabaseMsg)" << endl;
    }
}
