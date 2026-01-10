#include "C_tSighandler.h"



C_tSighandler::C_tSighandler(C_Monitor& reed, C_Monitor& pir, C_Monitor& finger, C_Monitor& rfid)
    : C_Thread(PRIO_HIGH),m_monReed(reed), m_monPIR(pir), m_monFinger(finger), m_monRFID(rfid), m_fd(-1)
{

    //prepare the list of signals the thread will take care of
    sigemptyset(&m_sigSet);
    sigaddset(&m_sigSet, 44); // Reed Switch
    sigaddset(&m_sigSet, 45); // PIR Sensor
    sigaddset(&m_sigSet, 46); // Fingerprint
    sigaddset(&m_sigSet, 47); // RFID Reader
}

C_tSighandler::~C_tSighandler() {
    if (m_fd >= 0) close(m_fd);
}


void C_tSighandler::setupSignalBlock() {
    //first funtion to be called
    //all the threads cretaed after will inherit this mask
    //the signals wont stop any thread

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, 44); sigaddset(&set, 45);
    sigaddset(&set, 46); sigaddset(&set, 47);

    // blocks assinchronous interruption of the process
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        perror("Erro ao bloquear sinais");
    }
}

void C_tSighandler::run() {
    siginfo_t info;

    // open driver and register PID
    m_fd = open("/dev/irq0", O_WRONLY);
    if (m_fd < 0 || ioctl(m_fd, REGIST_PID, 0) < 0) {
        std::cerr << "[Sighandler] ERRO: Não foi possível conectar ao Kernel Driver!" << std::endl;
        return;
    }

    std::cout << "[Sighandler] Pronto. À espera de eventos de hardware..." << std::endl;

    while (true) {
        //wait for signals
        int sig = sigwaitinfo(&m_sigSet, &info);

        if (sig < 0) continue;

        int pino = info.si_int; // O pino GPIO que veio do teu driver


        switch (sig) {
            case 44:
                std::cout << "[Hardware] Reed Switch detetado no pino " << pino << std::endl;
                m_monReed.signal();
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
                std::cout << "[Hardware] RFID aproximado no pino " << pino << std::endl;
                m_monRFID.signal();
                break;
        }
    }
}