#include "C_tSighandler.h"


C_tSighandler::C_tSighandler(C_Monitor& reed_room,C_Monitor& reed_vault, C_Monitor& pir, C_Monitor& finger, C_Monitor& rfid_entry,C_Monitor& rfid_exit)
    : C_Thread(PRIO_HIGH),m_monReed_room(reed_room),m_monReed_vault(reed_vault), m_monPIR(pir), m_monFinger(finger), m_monRFID_entry(rfid_entry),m_monRFID_exit(rfid_exit), m_fd(-1)
{
    sigemptyset(&m_sigSet);
    sigaddset(&m_sigSet, 43); 
    sigaddset(&m_sigSet, 44);
    sigaddset(&m_sigSet, 45); 
    sigaddset(&m_sigSet, 46); 
    sigaddset(&m_sigSet, 47); 
    sigaddset(&m_sigSet, 48); 
}

C_tSighandler::~C_tSighandler() {
    if (m_fd >= 0) close(m_fd);
}

void C_tSighandler::setupSignalBlock() {

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, 43);
    sigaddset(&set, 44);
    sigaddset(&set, 45);
    sigaddset(&set, 46);
    sigaddset(&set, 47);
    sigaddset(&set, 48);
    
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        perror("Erro ao bloquear sinais");
    }
}

void C_tSighandler::run() {
    siginfo_t info;

    
    m_fd = open("/dev/irq0", O_WRONLY);
    if (m_fd < 0 || ioctl(m_fd, REGIST_PID, 0) < 0) {
        std::cerr << "[Sighandler] ERRO: Não foi possível conectar ao Kernel Driver!" << std::endl;
        return;
    }

    std::cout << "[Sighandler] Pronto. À espera de eventos de hardware..." << std::endl;

    while (!stopRequested()) {

        struct timespec timeout;
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;
        int sig = sigtimedwait(&m_sigSet, &info, &timeout);

        if (sig < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            continue;
        }

        int pino = info.si_int; 

        switch (sig) {
            case 43:
                std::cout << "[Hardware] Reed Switch detetado no pino " << pino << std::endl;
                m_monReed_vault.signal();
                break;

            case 44:
                std::cout << "[Hardware] Reed Switch detetado no pino " << pino << std::endl;
                m_monReed_room.signal();
                break;
            case 45:
                std::cout << "[Hardware] Movimento PIR detetado no pino " << pino << std::endl;
                m_monPIR.signal();
                break;
            case 46:
                std::cout << "[Hardware] Digital lida no pino " << pino << std::endl;
                m_monFinger.signal();
                break;
            case 47:
                std::cout << "[Hardware] RFID entrada no pino " << pino << std::endl;
                m_monRFID_entry.signal();
                break;
            case 48:
                std::cout << "[Hardware] RFID saida aproximado no pino " << pino << std::endl;
                m_monRFID_exit.signal();
        }
    }
}