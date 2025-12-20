#ifndef C_DATABASE_H
#define C_DATABASE_H

#include <sqlcipher/sqlite3.h>
#include <string>
#include <iostream>
#include "SharedTypes.h"



class dDatabase {
public:
    dDatabase(const std::string& dbPath);
    ~dDatabase();

    bool open();
    void close();
    bool initializeSchema();

    //imagina aqui recebemos ja a a msg porque vamos chamar na main da db amq receive q vai receber isto ne
    void processDbMessage(const DatabaseMsg& msg);

private:
    sqlite3* m_db;
    std::string m_dbPath;

    // Funções Privadas: Onde o SQL acontece a sério
    void handleVerifyRFID(const char* rfid, const char* responseQueue);
    void handleUpdateAsset(const char* rfid);
    void handleInsertLog(const DatabaseLog& log);

    // Auxiliar para responder às threads
    void sendResponse(const char* queueName, const DbResponse& resp);
};


#endif