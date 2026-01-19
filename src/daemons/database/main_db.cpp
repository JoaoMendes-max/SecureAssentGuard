#include <iostream>
#include <unistd.h>
//#include "dDatabase.h"

/*
int main() {
    std::cout << "[DB_Daemon] A iniciar processo de Base de Dados..." << std::endl;

    // 1. DEFINIÇÃO DAS FILAS (Os nomes têm de ser iguais aos da App Principal)
    // Nota: O dDatabase recebe estas filas no construtor para poder responder às threads
    C_Mqueue mqDb("/mq_database");             // Entrada principal (comandos para a DB)
    C_Mqueue mqRfidIn("/mq_rfid_verify");      // Saída para a thread de entrada
    C_Mqueue mqRfidOut("/mq_rfid_leave");      // Saída para a thread de saída
    C_Mqueue mqFinger("/mq_fingerprint");      // Saída para a thread biométrica
    C_Mqueue mqCheckMov("/mq_check_movement"); // Saída para a thread de PIR/Movimento

    // 2. INSTANCIAR A CLASSE
    // Passamos o caminho do ficheiro .db e todas as filas necessárias
    dDatabase db("sistema_seguranca.db", mqDb, mqRfidIn, mqRfidOut, mqFinger, mqCheckMov);

    // 3. PREPARAR O FICHEIRO
    if (!db.open()) {
        std::cerr << "[DB_Daemon] Erro fatal: Não foi possível abrir o SQLite." << std::endl;
        return -1;
    }

    if (!db.initializeSchema()) {
        std::cerr << "[DB_Daemon] Erro fatal: Falha ao criar tabelas." << std::endl;
        return -1;
    }

    std::cout << "[DB_Daemon] Pronto para processar pedidos." << std::endl;

    // 4. O LOOP INFINITO (O "Coração" do Daemon)
    DatabaseMsg msg; // Estrutura que definiste no SharedTypes.h

    while (true) {
        // O receive fica bloqueado aqui. O processo não gasta CPU
        // enquanto ninguém enviar nada para a fila "/mq_database".
        if (mqDb.receive(&msg, sizeof(msg))) {

            // Chama a função que já criaste para fazer o switch/case
            db.processDbMessage(msg);

            // Opcional: Log para saberes o que está a acontecer
            std::cout << "[DB_Daemon] Comando processado: " << msg.command << std::endl;
        }
    }

    db.close(); // Nunca será alcançado, mas por boa prática
    return 0;
}*/


#include <iostream>
#include <ctime>
#include "dDatabase.h"


int main() {
    C_Mqueue mqToDb("/mq_to_db",sizeof(DatabaseMsg),10,true);
    // Filas de Saída (Onde a DB responde às threads do Core)
    C_Mqueue mqRfidIn("/mq_rfid_in", sizeof(AuthResponse), 10, true);
    C_Mqueue mqRfidOut("/mq_rfid_out", sizeof(AuthResponse), 10, true);
    C_Mqueue mqFinger("/mq_finger", sizeof(AuthResponse), 10, true);
    C_Mqueue mqMove("/mq_move", sizeof(AuthResponse), 10,true);
    C_Mqueue mqToWeb("/mq_db_to_web", sizeof(DbWebResponse), 10, true);
    C_Mqueue mqToenv("/mq_db_to_env", sizeof(AuthResponse), 10, true);


    dDatabase db("secure_asset.db", mqToDb, mqRfidIn, mqRfidOut, mqFinger, mqMove, mqToWeb,mqToenv);

    if (!db.open()) {
        cerr << "[ERRO FATAL] Não foi possível abrir o ficheiro .db!" << endl;
        return -1;
    }

    if (!db.initializeSchema()) {
        cerr << "[ERRO FATAL] Falha ao criar tabelas no SQLite!" << endl;
        return -1;
    }

    cout << "[Sucesso] Base de Dados pronta e tabelas verificadas." << endl;
    cout << "[Status] A aguardar pedidos do Core na fila: mq_to_db..." << endl;

    // 3. LOOP PRINCIPAL DE PROCESSAMENTO (O MOTOR)
    while (true) {
        DatabaseMsg msg={}; // A struct que define o comando (Enter, Leave, Log, etc.)

        // O receive bloqueia aqui até o Core enviar alguma coisa
        ssize_t bytesRead = mqToDb.receive(&msg, sizeof(DatabaseMsg));

        if (bytesRead > 0) {
            cout << "[Pedido] Nova mensagem recebida. Comando: " << (int)msg.command << endl;
            cout<< "cnas da mensagem"<< msg.payload.rfid << endl;

            // Chama a função da classe para decidir o que fazer com a mensagem
            db.processDbMessage(msg);
        }
    }

    // O código nunca deve chegar aqui, mas por boa prática:
    db.close();
    return 0;
}


