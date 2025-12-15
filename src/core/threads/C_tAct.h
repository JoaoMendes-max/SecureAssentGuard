#ifndef C_TACT_H
#define C_TACT_H

#include "C_Thread.h"
#include "SharedTypes.h"
#include <array>

class C_Mqueue;
class C_Actuator;

class C_tAct : public C_Thread {
private:
    C_Mqueue& m_mqToActuator;
    C_Mqueue& m_mqToDatabase;
    std::array<C_Actuator*, ID_ACTUATOR_COUNT> m_actuators;

public:
    // Construtor (Prioridade fixa em PRIO_ACTUATORS)
    C_tAct(C_Mqueue& mqIn,
           C_Mqueue& mqOut,
           const std::array<C_Actuator*, ID_ACTUATOR_COUNT>& listaAtuadores);

    // Destrutor (Vazio, a limpeza da thread é garantida pelo Pai virtual)
    ~C_tAct() {}

    void run() override; // O Loop Principal

private:
    void processMessage(const ActuatorCmd& msg);
    void logToDatabase(ActuatorID_enum id, uint8_t value);

    static void generateDescription(ActuatorID_enum id,
                                   uint8_t value,
                                   char* buffer,
                                   size_t size);

    bool isValidActuatorID(ActuatorID_enum id) const {
        return id < ID_ACTUATOR_COUNT;
    }
};

#endif // C_TACT_H