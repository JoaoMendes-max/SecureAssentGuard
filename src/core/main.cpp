#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>

// --- Configurações iguais ao Driver ---
#define IRQ_IOC_MAGIC  'k'
#define REGIST_PID     _IOW(IRQ_IOC_MAGIC, 1, int)
#define SIG_REED       44  // Reed Switch (Pino 17)

// =================================================================
// HANDLER DO SINAL
// =================================================================
void tratar_reed_switch(int n, siginfo_t *info, void *unused) {
    std::cout << "\n**************************************************" << std::endl;
    std::cout << "* *" << std::endl;
    std::cout << "* !!! A FUNCAO 'tratar_reed_switch' CORREU !!!  *" << std::endl;
    std::cout << "* *" << std::endl;
    std::cout << "**************************************************" << std::endl;
    std::cout << "-> Recebi o Sinal: " << n << std::endl;
    std::cout << "-> Pino que disparou: " << info->si_int << std::endl;
}

int main() {
    struct sigaction act;
    sigset_t set;
    int fd;

    std::cout << "--- A PREPARAR O TESTE ---" << std::endl;

    // 1. DESBLOQUEAR O SINAL (Crítico!)
    sigemptyset(&set);
    sigaddset(&set, SIG_REED);
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    // 2. CONFIGURAR O HANDLER
    sigemptyset(&act.sa_mask);
    act.sa_flags = (SA_SIGINFO | SA_RESTART);
    act.sa_sigaction = tratar_reed_switch;
    sigaction(SIG_REED, &act, NULL);

    std::cout << "-> Handler do sinal 44 configurado." << std::endl;

    // 3. ABRIR O DRIVER (irq0 = Reed Switch no pino 17)
    fd = open("/dev/irq0", O_RDWR);
    if (fd < 0) {
        perror("Erro ao abrir /dev/irq0");
        return -1;
    }

    // 4. REGISTAR O PID
    if (ioctl(fd, REGIST_PID, 0) < 0) {
        perror("Erro no IOCTL");
        close(fd);
        return -1;
    }
    std::cout << "-> PID registado no driver." << std::endl;

    // 5. GARANTIR QUE A INTERRUPÇÃO ESTÁ ATIVA
    char cmd = '1';
    if (write(fd, &cmd, 1) < 0) {
        perror("Erro no write");
        close(fd);
        return -1;
    }
    std::cout << "-> Interrupção do Reed Switch ATIVADA." << std::endl;

    // 6. LOOP DE ESPERA
    std::cout << "\n[AGUARDANDO] Toca no Pino 17 (GPIO 17, Físico 11)..." << std::endl;
    std::cout << "Pressiona CTRL+C para sair.\n" << std::endl;

    while(true) {
        sleep(1);
    }

    close(fd);
    return 0;
}