#include <iostream>
#include <unistd.h>





#include <iostream>
#include <ctime>
#include <csignal>
#include "dDatabase.h"

static volatile sig_atomic_t g_stop = 0;

static void handleSignal(int) {
    g_stop = 1;
}


int main() {
    C_Mqueue mqToDb("/mq_to_db",sizeof(DatabaseMsg),10,true);
    
    C_Mqueue mqRfidIn("/mq_rfid_in", sizeof(AuthResponse), 10, true);
    C_Mqueue mqRfidOut("/mq_rfid_out", sizeof(AuthResponse), 10, true);
    C_Mqueue mqFinger("/mq_finger", sizeof(AuthResponse), 10, true);
    C_Mqueue mqMove("/mq_move", sizeof(AuthResponse), 10, true);
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

    
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    while (!g_stop) {
        DatabaseMsg msg={};
        ssize_t bytesRead = mqToDb.timedReceive(&msg, sizeof(DatabaseMsg), 1);

        if (bytesRead > 0) {
            db.processDbMessage(msg);
        }
    }

    db.close();
    mqToDb.unregister();
    mqRfidIn.unregister();
    mqRfidOut.unregister();
    mqFinger.unregister();
    mqMove.unregister();
    mqToWeb.unregister();
    mqToenv.unregister();
    return 0;
}
