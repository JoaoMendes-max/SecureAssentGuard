#ifndef C_TALARMTIMER_H
#define C_TALARMTIMER_H

#include "C_Thread.h"
#include "SharedTypes.h"

// Forward declarations (para evitar includes desnecessários no header)
class C_Monitor;
class C_Mqueue;

class C_tAlarmTimer : public C_Thread {
private:
    C_Monitor& m_monitor;        // Para o timedWait
    C_Mqueue& m_mqAlarmTrigger;  // Fila de entrada (Gatilho)
    C_Mqueue& m_mqToActuator;    // Fila de saída (Comando)

    int m_alarmDurationSec;

public:
    C_tAlarmTimer(C_Monitor& monitor,
                  C_Mqueue& mqTrigger,
                  C_Mqueue& mqAct,
                  int durationSec = 30);

    ~C_tAlarmTimer() override;

    void run() override;
};

#endif