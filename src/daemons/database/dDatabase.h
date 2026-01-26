#ifndef C_DATABASE_H
#define C_DATABASE_H
#define SQLITE_HAS_CODEC
/*
 * Database backend (SQLCipher).
 * Receives queue requests, applies CRUD operations, and sends responses.
 */
#include "sqlcipher/sqlite3.h"
#include <string>
#include "SharedTypes.h"
#include "C_Mqueue.h"
#include "nlohmann/json.hpp"
#include <iostream>

class dDatabase {
public:
    dDatabase(const std::string& dbPath,
              C_Mqueue& mqDb,
              C_Mqueue& mqRfidIn,
              C_Mqueue& mqRfidOut,
              C_Mqueue& mqFinger,
              C_Mqueue& mqCheckMovement,
              C_Mqueue& mqToWeb,
              C_Mqueue& mqToEnv);
    ~dDatabase();

    bool open();
    void close();
    bool initializeSchema();
    void processDbMessage(const DatabaseMsg& msg);

private:
    sqlite3* m_db;
    std::string m_dbPath;

    C_Mqueue& m_mqToDatabase;
    C_Mqueue& m_mqToVerifyRoom;
    C_Mqueue& m_mqToLeaveRoom;
    C_Mqueue& m_mqToFingerprint;
    C_Mqueue& m_mqToCheckMovement;
    C_Mqueue& m_mqToWeb;
    C_Mqueue& m_mqToEnvThread;    

    
    void handleAccessRequest(const char* rfid, bool isEntering);
    void handleScanInventory(const Data_RFID_Inventory& inventory);
    void handleCheckUserInPir();
    void handleLogin(const LoginRequest& login);
    void handleGetDashboard();
    void handleGetSensors();
    void handleGetActuators();
    void handleInsertLog(const DatabaseLog& log);
    void updateSensorTable(uint8_t entityID, double value, double value2, uint32_t timestamp);
    void updateActuatorTable(uint8_t entityID, double value, uint32_t timestamp);

    void handleRegisterUser(const UserData& user);
    void handleGetUsers();
    void handleCreateUser(const UserData& user);
    void handleModifyUser(const UserData& user);
    void handleRemoveUser(uint32_t userId);

    void handleGetAssets();
    void handleCreateAsset(const AssetData& asset);
    void handleModifyAsset(const AssetData& asset);
    void handleRemoveAsset(const char* tag);

    void handleGetSettings();
    void handleGetSettingsForThread();

    void handleUpdateSettings(const SystemSettings& settings);

    void handleFilterLogs(const LogFilter& filter);


};

#endif
