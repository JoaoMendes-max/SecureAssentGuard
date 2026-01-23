// =======================================
// src/daemons/web/main_web.cpp (dWebServer)
// =======================================
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>

#include "dWebServer.h"
#include "C_Mqueue.h"
#include "SharedTypes.h"

static const char* WEB_PIDFILE = "/var/run/dWebServer.pid";
static dWebServer* g_server = nullptr;
static int g_shutdown_fd = -1;

static void handleSignal(int signum) {
    std::cout << "\n[WebServer] Signal " << signum << " received. Stopping..." << std::endl;
    if (g_server) g_server->stop();
}

static void sendShutdownAck() {
    if (g_shutdown_fd >= 0) {
        const char* ack = "OK\n";
        (void)write(g_shutdown_fd, ack, 3);
        close(g_shutdown_fd);
        g_shutdown_fd = -1;
    }
}

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

    close(STDIN_FILENO);
    open("/dev/null", O_RDONLY);

    if (logfile) {
        int fd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        } else {
            close(STDOUT_FILENO); close(STDERR_FILENO);
            open("/dev/null", O_WRONLY);
            open("/dev/null", O_WRONLY);
        }
    } else {
        close(STDOUT_FILENO); close(STDERR_FILENO);
        open("/dev/null", O_WRONLY);
        open("/dev/null", O_WRONLY);
    }

    int fd = open(pidfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char buf[32];
        ssize_t n = std::snprintf(buf, sizeof(buf), "%d\n", getpid());
        (void)write(fd, buf, n);
        close(fd);
    }
}

int main() {
    int notify_fd = -1;
    if (const char* env = std::getenv("NOTIFY_FD")) notify_fd = std::atoi(env);
    if (const char* env = std::getenv("SHUTDOWN_FD")) g_shutdown_fd = std::atoi(env);

    daemonize(WEB_PIDFILE, "/var/log/dWebServer.log");
    unsetenv("NOTIFY_FD");
    unsetenv("SHUTDOWN_FD");

    // Open existing queues
    C_Mqueue mqToDb("/mq_to_db", sizeof(DatabaseMsg), 20, false);
    C_Mqueue mqFromDb("/mq_db_to_web", sizeof(DbWebResponse), 10, false);

    dWebServer server(mqToDb, mqFromDb, 8080);
    g_server = &server;

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    bool ok = server.start();

    // Startup notify (PID or -1)
    if (notify_fd >= 0) {
        char buf[64];
        int len = ok ? std::snprintf(buf, sizeof(buf), "%d\n", getpid())
                     : std::snprintf(buf, sizeof(buf), "-1\n");
        (void)write(notify_fd, buf, len);
        close(notify_fd);
        notify_fd = -1;
    }

    if (!ok) {
        // Prevent wrapper from waiting until timeout
        sendShutdownAck(); // closes g_shutdown_fd
        unlink(WEB_PIDFILE);
        return -1;
    }

    // Blocks until stop() is called by signal handler
    server.run();

    sendShutdownAck();
    unlink(WEB_PIDFILE);
    return 0;
}
