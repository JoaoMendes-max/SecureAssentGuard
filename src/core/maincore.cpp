#include <iostream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include "C_SecureAsset.h"
#include "C_Mqueue.h"
#include "SharedTypes.h"

static const char* CORE_PIDFILE = "/var/run/SecureAssetCore.pid";
static volatile sig_atomic_t g_shutdown = 0;

static void handleSignal(int) { g_shutdown = 1; }

static void daemonize(const char* pidfile, const char* logfile = nullptr) {
    pid_t pid = fork();
    if (pid < 0) std::exit(EXIT_FAILURE);
    if (pid > 0) std::exit(EXIT_SUCCESS);
    if (setsid() < 0) std::exit(EXIT_FAILURE);
    std::signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid < 0) std::exit(EXIT_FAILURE);
    if (pid > 0) std::exit(EXIT_SUCCESS);
    umask(0);
    chdir("/");
    close(STDIN_FILENO); open("/dev/null", O_RDONLY);
    if (logfile) {
        int fd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        } else {
            close(STDOUT_FILENO); close(STDERR_FILENO);
            open("/dev/null", O_WRONLY); open("/dev/null", O_WRONLY);
        }
    } else {
        close(STDOUT_FILENO); close(STDERR_FILENO);
        open("/dev/null", O_WRONLY); open("/dev/null", O_WRONLY);
    }
    int fd = open(pidfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char buf[32];
        ssize_t n = std::snprintf(buf, sizeof(buf), "%d\n", getpid());
        write(fd, buf, n);
        close(fd);
    }
}

int main() {
    int notify_fd = -1;
    if (const char* env = std::getenv("NOTIFY_FD")) notify_fd = std::atoi(env);

    daemonize(CORE_PIDFILE, "/var/log/SecureAssetCore.log");
    unsetenv("NOTIFY_FD");

    // Os dispositivos de hardware e filas são inicializados em C_SecureAsset;
    // mas as filas já existem, pelo que o construtor deve usar createNew=false.
    C_SecureAsset* core = C_SecureAsset::getInstance();
    bool ok = core->init();
    if (ok) core->start();

    // Notifica wrapper
    if (notify_fd >= 0) {
        char buf[64];
        int len = ok ? std::snprintf(buf, sizeof(buf), "%d\n", getpid())
                     : std::snprintf(buf, sizeof(buf), "-1\n");
        write(notify_fd, buf, len);
        close(notify_fd);
        notify_fd = -1;
    }
    if (!ok) {
        C_SecureAsset::destroyInstance();
        return -1;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    while (!g_shutdown) {
        sleep(1);
    }

    // Paragem de todas as threads e limpeza (não remove filas)
    core->stop();
    core->waitForThreads();
    C_SecureAsset::destroyInstance();
    unlink(CORE_PIDFILE);
    return 0;
}
