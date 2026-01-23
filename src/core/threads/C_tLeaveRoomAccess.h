#ifndef C_TVERIFYLEAVEROOM_H
#define C_TVERIFYLEAVEROOM_H

#include "C_Thread.h"
#include "C_Monitor.h"
#include "C_RDM6300.h"
#include "C_Mqueue.h"
#include "SharedTypes.h"

class C_tLeaveRoomAccess : public C_Thread {
private:
    C_Monitor& m_monitorrfid;
    C_Monitor& m_monitorservoroom;
    C_RDM6300& m_rfidExit;       
    C_Mqueue& m_mqToDatabase;
    C_Mqueue& m_mqToLeaveRoom;   
    C_Mqueue& m_mqToActuator;

    int m_failedAttempts;
    int m_maxAttempts;

public:
    C_tLeaveRoomAccess(C_Monitor& monitorrfid, C_Monitor& monitorservoroom,
                       C_RDM6300& rfid,
                       C_Mqueue& mqDB,
                       C_Mqueue& mqFromDB,
                       C_Mqueue& mqAct);

    virtual ~C_tLeaveRoomAccess();

    void generateDescription(uint32_t userId, char* buffer, size_t size);
    void sendLog(uint32_t userId, uint32_t accessLevel);
    void run() override;
};

#endif
