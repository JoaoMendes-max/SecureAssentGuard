#include <iostream>
#include <csignal>
#include "dWebServer.h"
#include "C_Mqueue.h"

static volatile sig_atomic_t g_stop = 0;
static dWebServer* g_server = nullptr;

static void handleSignal(int) {
    g_stop = 1;
    if (g_server) {
        g_server->stop();
    }
}

int main() {
    std::cout << "[WebServer] Starting daemon..." << std::endl;

    C_Mqueue mqToDb("/mq_to_db", sizeof(DatabaseMsg), 10, true);
    C_Mqueue mqFromDb("/mq_db_to_web", sizeof(DbWebResponse), 10, true);

    dWebServer server(mqToDb, mqFromDb, 8080);
    g_server = &server;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (!server.start()) {
        std::cerr << "[WebServer] Failed to start" << std::endl;
        return -1;
    }

    server.run();

    if (g_stop) {
        mqToDb.unregister();
        mqFromDb.unregister();
    }

    return 0;
}
