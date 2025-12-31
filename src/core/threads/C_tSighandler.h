

#ifndef SECUREASSETGUARD_C_TSIGHANDLER_H
#define SECUREASSETGUARD_C_TSIGHANDLER_H

#include <csignal>
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
private:
    // Monitores: Cada sinal tem o seu próprio monitor
    C_Monitor& m_monReed;
    C_Monitor& m_monPIR;
    C_Monitor& m_monFinger;
    C_Monitor& m_monRFID;

    int m_fd;
    sigset_t m_sigSet;

public:
    // Construtor: Recebe as referências dos monitores
    C_tSighandler(C_Monitor& reed, C_Monitor& pir, C_Monitor& finger, C_Monitor& rfid);

    virtual ~C_tSighandler() override;

    // MÉTODO STATIC: Deve ser chamado no início do main()
    static void setupSignalBlock();

    // Lógica da thread (o "mordomo")
    void run() override;
};

#endif
