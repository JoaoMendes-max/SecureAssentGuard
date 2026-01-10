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

    bool open();//open or create db file and get sqlite3* m_db
    void close();
    bool initializeSchema();//initialize database structure

    void processDbMessage(const DatabaseMsg& msg);
    void handleInsertLog(const DatabaseLog& log);

private:
    sqlite3* m_db;
    std::string m_dbPath;


    C_Mqueue& m_mqToDatabase;
    C_Mqueue& m_mqToVerifyRoom;
    C_Mqueue& m_mqToLeaveRoom;
    C_Mqueue& m_mqToFingerprint;
    C_Mqueue& m_mqToCheckMovement;


    // Handlers
    void handleAccessRequest(const char* rfid, bool isEntering);
    void handleUpdateAsset(const char* rfid);
    void handleScanInventory(const Data_RFID_Inventory& inventory);
    void handleCheckUserInPir();
};

#endif