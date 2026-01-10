#ifndef C_DATABASE_H
#define C_DATABASE_H

#include "sqlcipher/sqlite3.h"
#include <string>
#include "SharedTypes.h"
#include "C_Mqueue.h"
#include "nlohmann/json.hpp"
#include <iostream>


class dDatabase {
public:
    // O construtor agora recebe as TRÊS referências
    dDatabase(const std::string& dbPath,
              C_Mqueue& mqDb,
              C_Mqueue& mqRfidIn,
              C_Mqueue& mqRfidOut,
              C_Mqueue& mqFinger,
              C_Mqueue& mqCheckMovement,
              C_Mqueue& mqToWeb);
    ~dDatabase();

    bool open();
    void close();
    bool initializeSchema();
    void processDbMessage(const DatabaseMsg& msg);
    void handleInsertLog(const DatabaseLog& log);

private:
    sqlite3* m_db;
    std::string m_dbPath;

    // --- OS ATRIBUTOS ADEQUADOS ---
    C_Mqueue& m_mqToDatabase;    // A que recebe os pedidos (input)
    C_Mqueue& m_mqToVerifyRoom;    // responde a thread de entrada da sala rfid
    C_Mqueue& m_mqToLeaveRoom;// a q responde a thread de saida da sala rfid
    C_Mqueue& m_mqToFingerprint; // A que responde ao Dedo (output)
    C_Mqueue& m_mqToCheckMovement;// a q responde ao pir
    C_Mqueue& m_mqToWeb;


    // Handlers
    void handleAccessRequest(const char* rfid, bool isEntering);
    void handleScanInventory(const Data_RFID_Inventory& inventory);
    void handleCheckUserInPir();
    void handleLogin(const LoginRequest& login);
    void handleGetDashboard();
    void handleGetSensors();
    void handleGetActuators();

    void updateSensorTable(uint8_t entityID, uint16_t value, uint16_t value2);
    void updateActuatorTable(uint8_t entityID, uint16_t value);
};

#endif