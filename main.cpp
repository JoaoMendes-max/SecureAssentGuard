#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <csignal>
#include <string>
#include <limits.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <memory>
#include <vector>

#include "C_Mqueue.h"
#include "SharedTypes.h"

static volatile sig_atomic_t g_stop = 0;

static void signalHandler(int signum) {
    std::cout << "\n[Wrapper] Signal " << signum << " received. Shutting down..." << std::endl;
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

static bool clearCloExec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) return false;
    return (fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC) == 0);
}

struct DaemonInfo {
    pid_t pid = -1;
    int shutdown_fd = -1;
    std::string name;
};

static DaemonInfo launchDaemon(const std::string& binName, const std::string& binPath) {
    DaemonInfo info;
    info.name = binName;

    int sv_startup[2];
    int sv_shutdown[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv_startup) < 0) {
        std::perror("socketpair startup");
        return info;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv_shutdown) < 0) {
        std::perror("socketpair shutdown");
        close(sv_startup[0]);
        close(sv_startup[1]);
        return info;
    }

    // Daemon ends must survive exec()
    if (!clearCloExec(sv_startup[1]) || !clearCloExec(sv_shutdown[1])) {
        std::perror("fcntl FD_CLOEXEC");
        close(sv_startup[0]); close(sv_startup[1]);
        close(sv_shutdown[0]); close(sv_shutdown[1]);
        return info;
    }

    char fdStartupStr[16], fdShutdownStr[16];
    std::snprintf(fdStartupStr, sizeof(fdStartupStr), "%d", sv_startup[1]);
    std::snprintf(fdShutdownStr, sizeof(fdShutdownStr), "%d", sv_shutdown[1]);
    setenv("NOTIFY_FD", fdStartupStr, 1);
    setenv("SHUTDOWN_FD", fdShutdownStr, 1);

    pid_t child = fork();
    if (child < 0) {
        std::perror("fork");
        close(sv_startup[0]); close(sv_startup[1]);
        close(sv_shutdown[0]); close(sv_shutdown[1]);
        unsetenv("NOTIFY_FD");
        unsetenv("SHUTDOWN_FD");
        return info;
    }

    if (child == 0) {
        // Child: keep write ends, close read ends
        close(sv_startup[0]);
        close(sv_shutdown[0]);
        execl(binPath.c_str(), binName.c_str(), nullptr);
        std::perror("execl");
        std::_Exit(EXIT_FAILURE);
    }

    // Wrapper: close write ends, keep read ends
    close(sv_startup[1]);
    close(sv_shutdown[1]);
    unsetenv("NOTIFY_FD");
    unsetenv("SHUTDOWN_FD");

    // Wait intermediate child (exits after daemonize first fork)
    waitpid(child, nullptr, 0);

    // Wait PID from daemon (startup handshake), timeout 5s
    pollfd pfd{};
    pfd.fd = sv_startup[0];
    pfd.events = POLLIN | POLLHUP;

    int ret = poll(&pfd, 1, 5000);
    if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
        info.pid = readPidFromFd(sv_startup[0]);
    } else {
        std::cerr << "[Wrapper] ERROR: startup timeout for " << binName << std::endl;
    }
    close(sv_startup[0]);

    if (info.pid <= 0) {
        // startup failed: close shutdown channel too
        close(sv_shutdown[0]);
        info.shutdown_fd = -1;
        info.pid = -1;
        return info;
    }

    // Keep shutdown read end for later
    info.shutdown_fd = sv_shutdown[0];
    return info;
}

static void stopDaemon(DaemonInfo& daemon) {
    if (daemon.pid <= 0) return;

    std::cout << "[Wrapper] Stopping " << daemon.name << " (PID " << daemon.pid << ")..." << std::endl;

    // Request graceful termination
    if (kill(daemon.pid, SIGTERM) != 0 && errno == ESRCH) {
        if (daemon.shutdown_fd >= 0) { close(daemon.shutdown_fd); daemon.shutdown_fd = -1; }
        return;
    }

    if (daemon.shutdown_fd >= 0) {
        pollfd pfd{};
        pfd.fd = daemon.shutdown_fd;
        pfd.events = POLLIN | POLLHUP;

        int ret = poll(&pfd, 1, 5000);
        if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
            // Accept either explicit "OK" or just EOF
            char buf[16];
            (void)read(daemon.shutdown_fd, buf, sizeof(buf));
            close(daemon.shutdown_fd);
            daemon.shutdown_fd = -1;
            std::cout << "[Wrapper] " << daemon.name << " stopped (ACK/EOF)." << std::endl;
            return;
        }

        // Timeout or error
        close(daemon.shutdown_fd);
        daemon.shutdown_fd = -1;
    }

    std::cout << "[Wrapper] " << daemon.name << " did not respond. Forcing SIGKILL." << std::endl;
    kill(daemon.pid, SIGKILL);
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "======================================\n";
    std::cout << "  SECURE ASSET GUARD - LAUNCHER\n";
    std::cout << "======================================\n";

    // Create message queues (launcher is the owner)
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
        std::cout << "[Wrapper] Message queues created successfully.\n";
    } catch (const std::exception& e) {
        std::cerr << "[Wrapper] ERROR creating queues: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::string execDir = getExecutableDir();

    DaemonInfo db = launchDaemon("dDatabase", execDir + "/dDatabase");
    if (db.pid < 0) return EXIT_FAILURE;
    std::cout << "[Wrapper] dDatabase PID " << db.pid << std::endl;

    DaemonInfo web = launchDaemon("dWebServer", execDir + "/dWebServer");
    if (web.pid < 0) {
        stopDaemon(db);
        return EXIT_FAILURE;
    }
    std::cout << "[Wrapper] dWebServer PID " << web.pid << std::endl;

    DaemonInfo core = launchDaemon("SecureAssetCore", execDir + "/SecureAssetCore");
    if (core.pid < 0) {
        stopDaemon(web);
        stopDaemon(db);
        return EXIT_FAILURE;
    }
    std::cout << "[Wrapper] SecureAssetCore PID " << core.pid << std::endl;

    std::cout << "\nSystem running — press Ctrl+C to stop.\n" << std::endl;

    while (!g_stop) pause();

    std::cout << "\n[Wrapper] Starting ordered shutdown...\n";
    stopDaemon(core);
    stopDaemon(web);
    stopDaemon(db);

    std::cout << "[Wrapper] Unlinking message queues...\n";
    for (auto& mq : mqs) mq->unregister();
    mqs.clear();

    unlink("/var/run/SecureAssetCore.pid");
    unlink("/var/run/dWebServer.pid");
    unlink("/var/run/dDatabase.pid");

    std::cout << "System terminated.\n";
    return 0;
}
