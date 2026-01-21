#ifndef C_TINVENTORYSCAN_H
#define C_TINVENTORYSCAN_H

#include "C_Thread.h"
#include "C_Monitor.h"
#include "C_YRM1001.h" 
#include "C_Mqueue.h"
#include "SharedTypes.h"

class C_tInventoryScan : public C_Thread {
private:
    C_Monitor& m_monitorservovault;
    C_YRM1001& m_rfidInventoy; 
    C_Mqueue& m_mqToDatabase;

    void sendLog(int count);
    void generateDescription(int count, char* buffer, size_t size);

public:
    C_tInventoryScan(C_Monitor& m_monitorservovault, C_YRM1001& m_rfidInventoy, C_Mqueue& m_mqToDatabase);
    virtual ~C_tInventoryScan() override = default;

    void run() override;
};

#endif