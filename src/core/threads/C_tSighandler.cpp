#include "C_tSighandler.h"



C_tSighandler::C_tSighandler(C_Monitor& reed, C_Monitor& pir, C_Monitor& finger, C_Monitor& rfid)
    : C_Thread(PRIO_HIGH),m_monReed(reed), m_monPIR(pir), m_monFinger(finger), m_monRFID(rfid), m_fd(-1)
{
    // Preparamos a lista de sinais que esta thread vai gerir
    sigemptyset(&m_sigSet);
    sigaddset(&m_sigSet, 44); // Reed Switch
    sigaddset(&m_sigSet, 45); // PIR Sensor
    sigaddset(&m_sigSet, 46); // Fingerprint
    sigaddset(&m_sigSet, 47); // RFID Reader
}

C_tSighandler::~C_tSighandler() {
    if (m_fd >= 0) close(m_fd);
}

// Bloqueio global de sinais (Estático)
void C_tSighandler::setupSignalBlock() {
    // a cena chamada na main para bloquear os sinais de interromper tudio diretamnete
    // ao chamarmos na main fica na mesma para as outras threads

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, 44); sigaddset(&set, 45);
    sigaddset(&set, 46); sigaddset(&set, 47);

    // Bloqueia a interrupção asíncrona no processo
    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        perror("Erro ao bloquear sinais");
    }
}

void C_tSighandler::run() {
    siginfo_t info;

    // 1. Abrir o driver e registar o nosso PID para o Kernel saber para onde mandar sinais
    m_fd = open("/dev/irq0", O_WRONLY);
    if (m_fd < 0 || ioctl(m_fd, REGIST_PID, 0) < 0) {
        std::cerr << "[Sighandler] ERRO: Não foi possível conectar ao Kernel Driver!" << std::endl;
        return;
    }

    std::cout << "[Sighandler] Pronto. À espera de eventos de hardware..." << std::endl;

    while (true) {
        // 2. A thread dorme aqui até um dos sinais do m_sigSet chegar
        // sigwaitinfo limpa o sinal da fila e devolve-nos os detalhes (como o pino)
        int sig = sigwaitinfo(&m_sigSet, &info);

        if (sig < 0) continue;

        int pino = info.si_int; // O pino GPIO que veio do teu driver

        // 3. Encaminhar para o monitor correto
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