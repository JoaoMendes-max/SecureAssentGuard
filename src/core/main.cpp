#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <csignal>
#include <cstdlib>
#include <limits.h>
#include <string>
#include "C_SecureAsset.h"

volatile sig_atomic_t g_shutdown = 0;

static void signalHandler(int signum) {
    std::cout << "\n[Main] Sinal " << signum << " recebido. A encerrar..." << std::endl;
    g_shutdown = 1;
}

static std::string getExecutableDir() {
    char path[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) {
        return ".";
    }
    path[len] = '\0';
    std::string full(path);
    size_t pos = full.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    return full.substr(0, pos);
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << "  SECURE ASSET GUARD - Iniciando...  " << std::endl;
    std::cout << "  PID Principal: " << getpid() << std::endl;
    std::cout << "======================================" << std::endl;

    signal(SIGINT, signalHandler);   
    signal(SIGTERM, signalHandler);  

    std::cout << "[Main] A carregar driver de interrupções..." << std::endl;
    
    if (system("insmod /root/my_irq.ko") != 0) {
        std::cerr << "[AVISO] Falha ao carregar driver ou já estava carregado." << std::endl;
    }

    const pid_t pid_db = fork();

    if (pid_db == -1) {
        std::cerr << "[ERRO] Falha ao criar processo Database" << std::endl;
        return EXIT_FAILURE;
    }

    if (pid_db == 0) {
        
        std::cout << "[Database] PID: " << getpid() << std::endl;
        // execl("./dDatabase", "dDatabase", nullptr);
        const std::string execDir = getExecutableDir();
        const std::string dbPath = execDir + "/dDatabase";
        execl(dbPath.c_str(), "dDatabase", nullptr);

        
        std::cerr << "[ERRO] Falha ao executar daemon_db" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "[Main] Database daemon lançado (PID: " << pid_db << ")" << std::endl;

    const pid_t pid_web = fork();

    if (pid_web == -1) {
        std::cerr << "[ERRO] Falha ao criar processo WebServer" << std::endl;
        kill(pid_db, SIGTERM);  
        waitpid(pid_db, nullptr, 0);
        return EXIT_FAILURE;
    }

    if (pid_web == 0) {
        
        std::cout << "[WebServer] PID: " << getpid() << std::endl;
        // execl("./dWebServer", "dWebServer", nullptr);
        const std::string execDir = getExecutableDir();
        const std::string webPath = execDir + "/dWebServer";
        execl(webPath.c_str(), "dWebServer", nullptr);

        
        std::cerr << "[ERRO] Falha ao executar daemon_web" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "[Main] WebServer daemon lançado (PID: " << pid_web << ")" << std::endl;
    
    sleep(2);

    std::cout << "[Core] A inicializar sistema de hardware..." << std::endl;

    C_SecureAsset* core = C_SecureAsset::getInstance();
    if (!core->init()) {
        std::cerr << "[ERRO CRÍTICO] Falha ao inicializar Core!" << std::endl;

        
        kill(pid_db, SIGTERM);
        kill(pid_web, SIGTERM);
        waitpid(pid_db, nullptr, 0);
        waitpid(pid_web, nullptr, 0);

        C_SecureAsset::destroyInstance();
        return EXIT_FAILURE;
    }

     core->start();

    std::cout << "\n======================================" << std::endl;
    std::cout << "  SISTEMA OPERACIONAL" << std::endl;
    std::cout << "  Pressione Ctrl+C para parar" << std::endl;
    std::cout << "======================================\n" << std::endl;

    while (!g_shutdown) {
        sleep(1);  
    }

    std::cout << "\n[Main] A encerrar sistema..." << std::endl;

    core->stop();
    core->waitForThreads();
    
    std::cout << "[Main] A terminar Database daemon..." << std::endl;
    kill(pid_db, SIGTERM);

    std::cout << "[Main] A terminar WebServer daemon..." << std::endl;
    kill(pid_web, SIGTERM);

    
    std::cout << "[Main] A aguardar término dos processos..." << std::endl;
    waitpid(pid_db, nullptr, 0);
    waitpid(pid_web, nullptr, 0);

    
    core->unregisterQueues();
    C_SecureAsset::destroyInstance();

    std::cout << "\n======================================" << std::endl;
    std::cout << "  SISTEMA ENCERRADO" << std::endl;
    std::cout << "======================================" << std::endl;

    std::cout << "  A REMOVER DRIVER" << std::endl;
    system("rmmod my_irq");

    return EXIT_SUCCESS;
}


