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
              C_Mqueue& mqRfid,
              C_Mqueue& mqFinger);
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
    C_Mqueue& m_mqToRoomRfid;    // A que responde ao RFID (output)
    C_Mqueue& m_mqToFingerprint; // A que responde ao Dedo (output)

    // Handlers
    void handleVerifyRFID(const char* rfid);
    void handleVerifyFingerprint(int fingerId);
    void handleUpdateAsset(const char* rfid);
    void handleInsertLog(const DatabaseLog& log);
};

#endif