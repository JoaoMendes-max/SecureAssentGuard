#include <iostream>
#include "dWebServer.h"
#include "C_Mqueue.h"

int main() {
    std::cout << "[WebServer] Starting daemon..." << std::endl;

    C_Mqueue mqToDb("/mq_web_to_db", sizeof(DatabaseMsg), 10, true);
    C_Mqueue mqFromDb("/mq_db_to_web", sizeof(DbWebResponse), 10, true);

    dWebServer server(mqToDb, mqFromDb, 8080);

    if (!server.start()) {
        std::cerr << "[WebServer] Failed to start" << std::endl;
        return -1;
    }

    server.run();

    return 0;
}