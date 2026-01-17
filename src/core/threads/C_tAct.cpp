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
      m_actuators(listaAtuadores),
      m_alarmTimerId(0) // Inicializa a zero
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
    // É boa prática apagar o timer ao destruir a classe
    if (m_alarmTimerId != 0) {
        timer_delete(m_alarmTimerId);
    }
}

// ============================================
// INIT TIMER (Cria a estrutura, mas não arranca)
// ============================================
void C_tAct::initTimer() {

    struct sigevent sev = {};
    // Configura para criar uma thread temporária ou sinal quando o timer estourar
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = alarmTimerCallback; // A nossa função estática
    sev.sigev_value.sival_ptr = this; // O TRUQUE: Passamos o ponteiro desta classe
    sev.sigev_notify_attributes = nullptr;

    if (timer_create(CLOCK_MONOTONIC, &sev, &m_alarmTimerId) == -1) {
        cerr << MODULE_NAME << " ERRO CRÍTICO: Falha ao criar timer POSIX!" << endl;
    }
}

// ============================================
// STATIC CALLBACK (Executa quando o tempo acaba)
// ============================================
void C_tAct::alarmTimerCallback(union sigval sv) {
    // Recuperar o ponteiro "this" que passamos no initTimer
    C_tAct* self = static_cast<C_tAct*>(sv.sival_ptr);

    if (self) {
        cout << "[tAct-Timer] Tempo esgotado! A enviar comando OFF..." << endl;

        // Criar comando para desligar o alarme
        ActuatorCmd cmd;
        cmd.actuatorID = ID_ALARM_ACTUATOR;
        cmd.value = 0; // DESLIGAR

        // Enviar para a PRÓPRIA fila da thread tAct
        // Nota: As Mqueues são thread-safe, por isso isto é seguro
        self->m_mqToActuator.send(&cmd, sizeof(cmd));
    }
}

// ============================================
// START ALARM TIMER
// ============================================
void C_tAct::startAlarmTimer(int seconds) {
    struct itimerspec its;

    // Tempo para disparar
    its.it_value.tv_sec = seconds;
    its.it_value.tv_nsec = 0;

    // Intervalo (0 = one-shot, dispara uma vez e para)
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    if (timer_settime(m_alarmTimerId, 0, &its, nullptr) == -1) {
        cerr << MODULE_NAME << " ERRO ao armar timer" << endl;
    } else {
        cout << MODULE_NAME << " Timer armado para " << seconds << "s" << endl;
    }
}

// ============================================
// STOP ALARM TIMER (Opcional, para cancelar)
// ============================================
void C_tAct::stopAlarmTimer() {
    struct itimerspec its;
    // Zeros desativam o timer
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    timer_settime(m_alarmTimerId, 0, &its, nullptr);
}

// ============================================
// RUN
// ============================================
void C_tAct::run() {
    cout << MODULE_NAME << " Iniciada..." << endl;
    ActuatorCmd msg;

    while (!stopRequested()) {
        // Bloqueia até receber mensagem (0% CPU)
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
            cerr << MODULE_NAME << " ERRO: Falha na fila " << strerror(errno) << endl;
            // Erro fatal (ex: fila destruída). Sai do loop.
            break;
        } else {
            cerr << MODULE_NAME << " AVISO: Mensagem corrompida (" << bytes << " bytes)" << endl;
        }
    }

    stopAlarmTimer();
    cout << MODULE_NAME << " Terminada" << endl;
}

// ============================================
// PROCESS MESSAGE
// ============================================
void C_tAct::processMessage(const ActuatorCmd& msg) {
    // 1. VALIDATION
    if (!isValidActuatorID(msg.actuatorID)) {
        cerr << MODULE_NAME << " ERRO: ID inválido " << (int)msg.actuatorID << endl;
        return;
    }

    C_Actuator* actuator = m_actuators[msg.actuatorID];
    if (!actuator) {
        cerr << MODULE_NAME << " ERRO: Atuador não inicializado" << endl;
        return;
    }

    cout << MODULE_NAME << " Comando: " << ACTUATOR_NAMES[msg.actuatorID]
         << " -> " << (int)msg.value << endl;

    bool sucesso = actuator->set_value(msg.value);

    if (sucesso && msg.actuatorID == ID_ALARM_ACTUATOR) {
        if (msg.value == 1) {
            // Se ligou o alarme, INICIA o timer de 30s
            startAlarmTimer(30);
        } else {
            // Se desligou o alarme (manualmente ou pelo timer), CANCELA o timer
            // Isto evita que o timer dispare se desligarmos manualmente aos 15s
            stopAlarmTimer();
        }
    }

    // 3. Log
    if (sucesso) {
        sendLog(msg.actuatorID, msg.value);
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
void C_tAct::sendLog(ActuatorID_enum id, uint8_t value) {
    //mudar esta shit para mandar msg ahahah(ber verifyroomaccess
    DatabaseMsg msg = {};
    msg.command = DB_CMD_WRITE_LOG; // <--- Fundamental para a DB saber o que fazer

    // 2. Preencher os dados do log dentro do payload
    msg.payload.log.logType = LOG_TYPE_ACTUATOR;
    msg.payload.log.entityID = static_cast<uint8_t>(id);
    msg.payload.log.value = static_cast<uint16_t>(value);
    msg.payload.log.timestamp = static_cast<uint32_t>(time(nullptr));

    generateDescription(id, value, msg.payload.log.description, sizeof(msg.payload.log.description));

    // 3. Enviar a DatabaseMsg completa (o tamanho tem de ser sizeof(DatabaseMsg))
    bool enviado = m_mqToDatabase.send(&msg, sizeof(DatabaseMsg), 0);

    if (!enviado) {
        cerr << MODULE_NAME << " ERRO ao enviar log (DatabaseMsg)" << endl;
    }
}
