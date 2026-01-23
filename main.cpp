#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <csignal>
#include <string>
#include <limits.h>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <memory>
#include <vector>
#include "C_Mqueue.h"
#include "SharedTypes.h"

static volatile sig_atomic_t g_stop = 0;

static void signalHandler(int signum) {
    std::cout << "\n[Wrapper] Sinal " << signum << " recebido. A encerrar..." << std::endl;
    g_stop = 1;
}

static std::string getExecutableDir() {
    char path[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) return ".";
    path[len] = '\0';
    std::string full(path);
    size_t pos = full.find_last_of('/');
    return (pos == std::string::npos) ? "." : full.substr(0, pos);
}

static pid_t readPidFromFd(int fd) {
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';
    long pid = std::strtol(buf, nullptr, 10);
    return (pid > 0) ? static_cast<pid_t>(pid) : -1;
}

// Lança um daemon e espera pela sua mensagem de “pronto” no descritor sv[0]
static pid_t launchDaemon(const std::string& binName, const std::string& binPath) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        std::perror("socketpair");
        return -1;
    }
    // FD usado pelo daemon; tem de sobreviver ao exec
    int flags = fcntl(sv[1], F_GETFD);
    fcntl(sv[1], F_SETFD, flags & ~FD_CLOEXEC);

    char fdStr[16];
    std::snprintf(fdStr, sizeof(fdStr), "%d", sv[1]);
    setenv("NOTIFY_FD", fdStr, 1);

    pid_t child = fork();
    if (child < 0) {
        std::perror("fork");
        close(sv[0]);
        close(sv[1]);
        unsetenv("NOTIFY_FD");
        return -1;
    }
    if (child == 0) {
        close(sv[0]);
        execl(binPath.c_str(), binName.c_str(), nullptr);
        std::perror("execl");
        std::_Exit(EXIT_FAILURE);
    }

    // Processo wrapper: fecha o lado de escrita e remove a env var
    close(sv[1]);
    unsetenv("NOTIFY_FD");
    // espera que o filho intermédio termine (sai após daemonizar)
    waitpid(child, nullptr, 0);

    // espera mensagem do daemon com timeout (5 s)
    struct pollfd pfd{sv[0], POLLIN, 0};
    int ret = poll(&pfd, 1, 5000);
    pid_t pid_daemon = -1;
    if (ret > 0 && (pfd.revents & POLLIN)) {
        pid_daemon = readPidFromFd(sv[0]);
    } else {
        std::cerr << "[Wrapper] ERRO: timeout ao arrancar " << binName << std::endl;
    }
    close(sv[0]);
    return pid_daemon;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "======================================" << std::endl;
    std::cout << "  SECURE ASSET GUARD - LAUNCHER" << std::endl;
    std::cout << "======================================" << std::endl;

    // Cria todas as Message Queues (wrapper é o owner)
    std::vector<std::unique_ptr<C_Mqueue>> mqs;
    try {
        mqs.push_back(std::make_unique<C_Mqueue>("/mq_to_db", sizeof(DatabaseMsg), 20, true));
        mqs.push_back(std::make_unique<C_Mqueue>("/mq_to_actuator", sizeof(ActuatorCmd), 20, true));
        mqs.push_back(std::make_unique<C_Mqueue>("/mq_rfid_in", sizeof(AuthResponse), 10, true));
        mqs.push_back(std::make_unique<C_Mqueue>("/mq_rfid_out", sizeof(AuthResponse), 10, true));
        mqs.push_back(std::make_unique<C_Mqueue>("/mq_move", sizeof(AuthResponse), 10, true));
        mqs.push_back(std::make_unique<C_Mqueue>("/mq_finger", sizeof(AuthResponse), 10, true));
        mqs.push_back(std::make_unique<C_Mqueue>("/mq_db_to_env", sizeof(AuthResponse), 10, true));
        mqs.push_back(std::make_unique<C_Mqueue>("/mq_db_to_web", sizeof(DbWebResponse), 10, true));
        std::cout << "[Wrapper] Filas criadas com sucesso" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Wrapper] ERRO ao criar filas: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // Lança daemons com handshake
    std::string execDir = getExecutableDir();
    pid_t pid_db  = launchDaemon("dDatabase", execDir + "/dDatabase");
    if (pid_db < 0) return EXIT_FAILURE;
    std::cout << "[Wrapper] dDatabase PID " << pid_db << std::endl;

    pid_t pid_web = launchDaemon("dWebServer", execDir + "/dWebServer");
    if (pid_web < 0) {
        kill(pid_db, SIGTERM);
        return EXIT_FAILURE;
    }
    std::cout << "[Wrapper] dWebServer PID " << pid_web << std::endl;

    pid_t pid_core = launchDaemon("SecureAssetCore", execDir + "/SecureAssetCore");
    if (pid_core < 0) {
        kill(pid_web, SIGTERM);
        kill(pid_db, SIGTERM);
        return EXIT_FAILURE;
    }
    std::cout << "[Wrapper] SecureAssetCore PID " << pid_core << std::endl;

    std::cout << "\nSistema em execução — prima Ctrl+C para encerrar.\n" << std::endl;

    // Aguarda sinal
    while (!g_stop) {
        pause();
    }

    // Paragem ordenada: Core → Web → Database
    auto stopDaemon = [](pid_t pid, const std::string& name) {
        if (pid <= 0) return;
        std::cout << "[Wrapper] A parar " << name << "..." << std::endl;
        kill(pid, SIGTERM);
        for (int i = 0; i < 50; ++i) {
            if (kill(pid, 0) != 0) return;
            usleep(100000);
        }
        std::cout << "[Wrapper] Forçando SIGKILL em " << name << std::endl;
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    };
    stopDaemon(pid_core, "SecureAssetCore");
    stopDaemon(pid_web,  "dWebServer");
    stopDaemon(pid_db,   "dDatabase");

    // Limpa filas (unlink)
    std::cout << "[Wrapper] A remover filas..." << std::endl;
    for (auto& mq : mqs) {
        mq->unregister();
    }
    mqs.clear();

    // Remove ficheiros PID
    unlink("/var/run/SecureAssetCore.pid");
    unlink("/var/run/dWebServer.pid");
    unlink("/var/run/dDatabase.pid");

    std::cout << "Sistema encerrado.\n";
    return 0;
}
