#ifndef _C_TCHECKMOVEMENT_H_
#define _C_TCHECKMOVEMENT_H_
/*
 * PIR movement thread: validates active user and triggers alarm if needed.
 */
#include "C_Monitor.h"
#include "C_Thread.h"
#include "SharedTypes.h"
#include "C_Mqueue.h"

class C_tCheckMovement : public C_Thread{
public:
    C_tCheckMovement(C_Mqueue& m_mqToCheckMovement, C_Mqueue& m_mqToDatabase,C_Mqueue& m_mqToActuator,C_Monitor& m_monitor);
    ~C_tCheckMovement() override = default;
    void run() override;

private:
    void sendLog(bool authorized);
    void generateDescription(bool authorized, char* buffer, size_t size);
    C_Mqueue& m_mqToCheckMovement;
    C_Mqueue& m_mqToDatabase;
    C_Mqueue& m_mqToActuator;
    C_Monitor& m_monitor;

};


#endif
