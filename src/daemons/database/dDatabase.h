#ifndef C_DATABASE_H
#define C_DATABASE_H

#include <sqlcipher/sqlite3.h>
#include <iostream>
#include <string>
#include "SharedTypes.h"
#include "C_Mqueue.h"

class dDatabase {
public:
    // O construtor agora recebe as TRÊS referências
    dDatabase(const std::string& dbPath,
              C_Mqueue& mqDb,
              C_Mqueue& mqRfidIn,
              C_Mqueue& mqRfidOut,
              C_Mqueue& mqFinger,
              C_Mqueue& mqCheckMovement);
    ~dDatabase();

    bool open();
    void close();
    bool initializeSchema();
    void processDbMessage(const DatabaseMsg& msg);

private:
    sqlite3* m_db;
    std::string m_dbPath;

    // --- OS ATRIBUTOS ADEQUADOS ---
    C_Mqueue& m_mqToDatabase;    // A que recebe os pedidos (input)
    C_Mqueue& m_mqToVerifyRoom;    // responde a thread de entrada da sala rfid
    C_Mqueue& m_mqToLeaveRoom;// a q responde a thread de saida da sala rfid
    C_Mqueue& m_mqToFingerprint; // A que responde ao Dedo (output)
    C_Mqueue& m_mqToCheckMovement;// a q responde ao pir


    // Handlers
    void handleAccessRequest(const char* rfid, bool isEntering);
    void handleUpdateAsset(const char* rfid);
    void handleInsertLog(const DatabaseLog& log);
    void handleCheckUserInPir();
};

#endif