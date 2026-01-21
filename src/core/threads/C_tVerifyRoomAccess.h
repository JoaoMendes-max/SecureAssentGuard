#ifndef C_TVERIFYROOMACCESS_H
#define C_TVERIFYROOMACCESS_H

#include "C_Thread.h"
#include "C_Mqueue.h"
#include "C_Monitor.h"
#include "C_RDM6300.h"
#include "SharedTypes.h"

class C_tVerifyRoomAccess : public C_Thread {
private:
    
    C_Monitor& m_monitorrfid;
    C_Monitor& m_monitorservoroom;
    C_RDM6300& m_rfidEntry;
    C_Mqueue& m_mqToDatabase;   
    C_Mqueue& m_mqToVerifyRoom;
    C_Mqueue& m_mqToActuator;

    int m_failedAttempts;
    int m_maxAttempts;

    // void sendLog(uint8_t userId, uint16_t accessLevel, bool isInside);
    void sendLog(uint32_t userId, uint32_t accessLevel, bool isInside);

public:
    
    C_tVerifyRoomAccess(C_Monitor& monitorrfid,
                        C_Monitor& m_monitorservoroom,
                        C_RDM6300& rfid,
                        C_Mqueue& mqDB,
                        C_Mqueue& mqFromDB,
                        C_Mqueue& mqAct);

    virtual ~C_tVerifyRoomAccess();
    // void generateDescription(uint8_t userId, bool authorized, char* buffer, size_t size);
    void generateDescription(uint32_t userId, bool authorized, char* buffer, size_t size);
    void run() override; 
};

#endif
