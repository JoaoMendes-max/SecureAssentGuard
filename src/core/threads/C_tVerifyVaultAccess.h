#ifndef C_TVERIFYVAULTACCESS_H
#define C_TVERIFYVAULTACCESS_H

#include "C_Fingerprint.h"
#include "C_Thread.h"
#include "C_Monitor.h"
#include "C_Mqueue.h"

class c_tVerifyVaultAccess : public C_Thread {
        C_Monitor& m_monitorfgp;
        C_Monitor& m_monitorservovault;
        C_Fingerprint& m_fingerprint;
        C_Mqueue& m_mqToDatabase;
        C_Mqueue& m_mqToActuator;
        C_Mqueue& m_mqFromDatabase;
    public:
        c_tVerifyVaultAccess(C_Monitor& m_monitor,C_Monitor& m_monitorservovault, C_Fingerprint& m_fingerprint,C_Mqueue& m_mqToDatabase,C_Mqueue& m_mqToActuator, C_Mqueue& m_mqFromDatabase);
        ~c_tVerifyVaultAccess() override;
        void run() override;
        void generateDescription(uint32_t userId, bool authorized, char* buffer, size_t size);
        void sendLog(uint32_t userId, bool authorized);
};

#endif
