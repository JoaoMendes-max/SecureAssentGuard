#ifndef C_TACT_H
#define C_TACT_H

#include "C_Thread.h"
#include "SharedTypes.h"
#include <array>
#include <signal.h>  // ← ADICIONAR
#include <time.h>    // ← ADICIONAR

class C_Mqueue;
class C_Actuator;

class C_tAct : public C_Thread {
private:
    C_Mqueue& m_mqToActuator;
    C_Mqueue& m_mqToDatabase;
    std::array<C_Actuator*, ID_ACTUATOR_COUNT> m_actuators;

    timer_t m_alarmTimerId;

public:
    C_tAct(C_Mqueue& mqIn,
           C_Mqueue& mqOut,
           const std::array<C_Actuator*, ID_ACTUATOR_COUNT>& listaAtuadores);

    ~C_tAct() override;

    void run() override;

private:
    void processMessage(const ActuatorCmd& msg);
    void sendLog(ActuatorID_enum id, uint8_t value);

    // Funções auxiliares do Timer
    void initTimer();
    void startAlarmTimer(int seconds);
    void stopAlarmTimer();

    // Callback ESTÁTICA (obrigatório para timer_create)
    static void alarmTimerCallback(union sigval sv);

    static void generateDescription(ActuatorID_enum id,
                                   uint8_t value,
                                   char* buffer,
                                   size_t size);

    bool isValidActuatorID(ActuatorID_enum id) const {
        return id < ID_ACTUATOR_COUNT;
    }
};
#endif // C_TACT_H
