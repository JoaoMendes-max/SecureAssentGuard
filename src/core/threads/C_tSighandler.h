

#ifndef SECUREASSETGUARD_C_TSIGHANDLER_H
#define SECUREASSETGUARD_C_TSIGHANDLER_H

#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <iostream>
#include "C_Thread.h"
#include "C_Monitor.h"
#include"SharedTypes.h"

// Definições para comunicação com o teu Kernel Driver
#define IRQ_IOC_MAGIC  'k'
#define REGIST_PID     _IOW(IRQ_IOC_MAGIC, 1, int)

class C_tSighandler : public C_Thread {

    C_Monitor& m_monReed_room;
    C_Monitor& m_monReed_vault;
    C_Monitor& m_monPIR;
    C_Monitor& m_monFinger;
    C_Monitor& m_monRFID_entry;
    C_Monitor& m_monRFID_exit;

    int m_fd;
    sigset_t m_sigSet;

public:
    C_tSighandler(C_Monitor& reed_room,C_Monitor& reed_vault, C_Monitor& pir, C_Monitor& finger, C_Monitor& rfid_entry,C_Monitor& rfid_exit);

     ~C_tSighandler() override;

    // should be the first thing to be called in the main
    static void setupSignalBlock();


    void run() override;
};

#endif
