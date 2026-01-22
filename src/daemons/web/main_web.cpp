#include <iostream>
#include <csignal>
#include "dWebServer.h"
#include "C_Mqueue.h"

// Ponteiro global para o servidor; usado no handler de sinal
static dWebServer* g_server = nullptr;

// Handler de sinais: pede ao servidor para parar
static void handleSignal(int signum) {
    std::cout << "\n[WebServer] Sinal " << signum
              << " recebido. A terminar..." << std::endl;
    if (g_server) {
        g_server->stop();  // isto irá fazer com que run() saia do ciclo
    }
}

int main() {
    std::cout << "[WebServer] Starting daemon..." << std::endl;

    // Cria filas de mensagens
    C_Mqueue mqToDb("/mq_to_db", sizeof(DatabaseMsg), 10, true);
    C_Mqueue mqFromDb("/mq_db_to_web", sizeof(DbWebResponse), 10, true);

    // Instancia o servidor e regista-o na variável global
    dWebServer server(mqToDb, mqFromDb, 8080);
    g_server = &server;

    // Regista handlers de sinal para SIGINT e SIGTERM
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Arranca o servidor; se falhar, sai
    if (!server.start()) {
        std::cerr << "[WebServer] Failed to start" << std::endl;
        return -1;
    }

    // Entra no loop do servidor; termina quando stop() for chamado
    server.run();

    // Ao sair do bloco, ~dWebServer e ~C_Mqueue limpam recursos
    std::cout << "[WebServer] Terminou com sucesso." << std::endl;
    return 0;
}
