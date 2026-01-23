// ==========================================
// src/daemons/database/main_db.cpp (dDatabase)
// ==========================================
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>

#include "dDatabase.h"
#include "C_Mqueue.h"
#include "SharedTypes.h"

static const char* DB_PIDFILE = "/var/run/dDatabase.pid";
static volatile sig_atomic_t g_stop = 0;
static int g_shutdown_fd = -1;

static void handleSignal(int) { g_stop = 1; }

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

    daemonize(DB_PIDFILE, "/var/log/dDatabase.log");
    unsetenv("NOTIFY_FD");
    unsetenv("SHUTDOWN_FD");

    // Open existing queues (createNew=false)
    C_Mqueue mqToDb("/mq_to_db", sizeof(DatabaseMsg), 20, false);
    C_Mqueue mqRfidIn("/mq_rfid_in", sizeof(AuthResponse), 10, false);
    C_Mqueue mqRfidOut("/mq_rfid_out", sizeof(AuthResponse), 10, false);
    C_Mqueue mqFinger("/mq_finger", sizeof(AuthResponse), 10, false);
    C_Mqueue mqMove("/mq_move", sizeof(AuthResponse), 10, false);
    C_Mqueue mqToWeb("/mq_db_to_web", sizeof(DbWebResponse), 10, false);
    C_Mqueue mqToEnv("/mq_db_to_env", sizeof(AuthResponse), 10, false);

    dDatabase db("secure_asset.db",
                 mqToDb, mqRfidIn, mqRfidOut,
                 mqFinger, mqMove, mqToWeb, mqToEnv);

    bool ok = db.open() && db.initializeSchema();

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
        unlink(DB_PIDFILE);
        return -1;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    while (!g_stop) {
        DatabaseMsg msg{};
        ssize_t bytesRead = mqToDb.timedReceive(&msg, sizeof(DatabaseMsg), 1);
        if (bytesRead > 0) {
            db.processDbMessage(msg);
        }
    }

    db.close();
    sendShutdownAck();
    unlink(DB_PIDFILE);
    return 0;
}
