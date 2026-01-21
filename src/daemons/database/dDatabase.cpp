#include "dDatabase.h"
#include <iostream>
#include <argon2.h>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
int computeAccessLevel(bool hasRfid, bool hasFingerprint) {
    if (hasFingerprint) {
        return 2;
    }
    if (hasRfid) {
        return 1;
    }
    return 0;
}

bool isEmptyString(const char* s) {
    return !s || s[0] == '\0';
}

void bindTextOrNull(sqlite3_stmt* stmt, int index, const char* text) {
    if (isEmptyString(text)) {
        sqlite3_bind_null(stmt, index);
    } else {
        sqlite3_bind_text(stmt, index, text, -1, SQLITE_TRANSIENT);
    }
}

std::string getUserNameById(sqlite3* db, uint32_t userId) {
    sqlite3_stmt* stmt = nullptr;
    std::string name;
    const char* sql = "SELECT Name FROM Users WHERE UserID = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, static_cast<int>(userId));
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (value) {
                name = value;
            }
        }
        sqlite3_finalize(stmt);
    }
    return name;
}
}

dDatabase::dDatabase(const std::string& dbPath,
                   C_Mqueue& mqDb,
                   C_Mqueue& mqRfidIn,
                   C_Mqueue& mqRfidOut,
                   C_Mqueue& mqFinger,
                   C_Mqueue& m_mqToCheckMovement,
                   C_Mqueue& mqToWeb,
                   C_Mqueue& mqToEnv)   
    : m_db(nullptr),
      m_dbPath(dbPath),
      m_currentRequestId(0),
      m_mqToDatabase(mqDb),
      m_mqToVerifyRoom(mqRfidIn),
      m_mqToLeaveRoom(mqRfidOut),
      m_mqToFingerprint(mqFinger),
      m_mqToCheckMovement(m_mqToCheckMovement),
      m_mqToWeb(mqToWeb),
      m_mqToEnvThread(mqToEnv)      
{
}


dDatabase::~dDatabase() {
    close();
}

// bool dDatabase::open() { int result = sqlite3_open(m_dbPath.c_str(), &m_db); if (result != SQLITE_OK) { std::cerr << \"ERRO: Não foi possível abrir a base de dados: \" << sqlite3_errmsg(m_db) << std::endl; return false; } std::cout << \"SUCESSO: Ligado ao ficheiro: \" << m_dbPath << std::endl; return true; }
bool dDatabase::open() {
    int result = sqlite3_open(m_dbPath.c_str(), &m_db);

    if (result != SQLITE_OK) {
        std::cerr << "ERRO: Não foi possível abrir a base de dados: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    const char* key = std::getenv("SECUREASSET_DB_KEY");
    if (key && key[0] != '\0') {
        if (sqlite3_key(m_db, key, static_cast<int>(std::strlen(key))) != SQLITE_OK) {
            std::cerr << "ERRO: Falha ao definir chave SQLCipher: "
                      << sqlite3_errmsg(m_db) << std::endl;
            sqlite3_close(m_db);
            m_db = nullptr;
            return false;
        }
    }

    std::cout << "SUCESSO: Ligado ao ficheiro: " << m_dbPath << std::endl;
    return true;
}

void dDatabase::close() {
    if (m_db) {
        
        sqlite3_close(m_db);
        m_db = nullptr; 
        std::cout << "Base de dados fechada corretamente." << std::endl;
    }
}


bool dDatabase::initializeSchema() {
    if (!m_db) {
        std::cerr << "Erro: A base de dados não está aberta!" << std::endl;
        return false;
    }

    // const char* sql = "CREATE TABLE IF NOT EXISTS Users (UserID INTEGER PRIMARY KEY AUTOINCREMENT, Name TEXT, RFID_Card TEXT UNIQUE, FingerprintID INTEGER UNIQUE, Password TEXT, AccessLevel INTEGER, IsInside INTEGER DEFAULT 0);" "CREATE TABLE IF NOT EXISTS Logs (LogsID INTEGER PRIMARY KEY AUTOINCREMENT, EntityID INTEGER, Timestamp INTEGER, LogType INTEGER, Description TEXT, Value INTEGER, Value2 INTEGER DEFAULT 0);" "CREATE TABLE IF NOT EXISTS Assets (AssetID INTEGER PRIMARY KEY AUTOINCREMENT, Name TEXT, RFID_Tag TEXT UNIQUE, State TEXT, LastRead INTEGER);" "CREATE TABLE IF NOT EXISTS Sensors (SensorID INTEGER PRIMARY KEY AUTOINCREMENT, Type TEXT UNIQUE, Value INTEGER, LastUpdate INTEGER DEFAULT 0);" "CREATE TABLE IF NOT EXISTS Actuators (ActuatorID INTEGER PRIMARY KEY AUTOINCREMENT, Type TEXT UNIQUE, State INTEGER, LastUpdate INTEGER DEFAULT 0) ;" "CREATE TABLE IF NOT EXISTS SystemSettings (ID INTEGER PRIMARY KEY CHECK (ID = 1), TempThreshold INTEGER DEFAULT 30, SamplingTime INTEGER DEFAULT 600);";
    const char* sql =
        "CREATE TABLE IF NOT EXISTS Users ("
        "UserID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Name TEXT, "
        "RFID_Card TEXT UNIQUE, "
        "FingerprintID INTEGER UNIQUE, "
        "Password TEXT, "
        "AccessLevel INTEGER, "
        "IsInside INTEGER DEFAULT 0);"

        "CREATE TABLE IF NOT EXISTS Logs ("
        "LogsID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "EntityID INTEGER, "
        "Timestamp INTEGER, "
        "LogType INTEGER, "
        "Description TEXT, "
        "Value REAL, "
        "Value2 REAL DEFAULT 0);"

        "CREATE TABLE IF NOT EXISTS Assets ("
        "AssetID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Name TEXT, "
        "RFID_Tag TEXT UNIQUE, "
        "LastRead INTEGER);"

        "CREATE TABLE IF NOT EXISTS Sensors ("
        "SensorID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Type TEXT UNIQUE, "
        "Value REAL, "
        "LastUpdate INTEGER DEFAULT 0);"

        "CREATE TABLE IF NOT EXISTS Actuators ("
        "ActuatorID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Type TEXT UNIQUE, "
        "State INTEGER, "
        "LastUpdate INTEGER DEFAULT 0) ;"

        "CREATE TABLE IF NOT EXISTS SystemSettings ("
        "ID INTEGER PRIMARY KEY CHECK (ID = 1), "
        "TempThreshold INTEGER DEFAULT 30, "
        "SamplingTime INTEGER DEFAULT 600);";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::cerr << "Erro ao criar tabelas: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    
    const char* initSettings =
        "INSERT OR IGNORE INTO SystemSettings (ID, TempThreshold, SamplingTime) "
        "VALUES (1, 30, 600);";
    sqlite3_exec(m_db, initSettings, nullptr, nullptr, nullptr);

    
    const char* password = "1234";
    char hash[128];
    uint8_t salt[16];
    srand(time(nullptr));
    for (int i = 0; i < 16; i++) {
        salt[i] = rand() % 256;
    }

    argon2id_hash_encoded(2, 16384, 1, password, strlen(password),
                         salt, sizeof(salt), 32, hash, sizeof(hash));

    
    const char* createAdmin =
        "INSERT OR IGNORE INTO Users (UserID, Name, Password, AccessLevel, FingerprintID) "
        "VALUES (1, 'admin_viewer', ?, 2, 1);";  

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, createAdmin, -1, &stmt, nullptr) == SQLITE_OK) {
        // sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    
    // const char* insertSensors = "INSERT OR IGNORE INTO Sensors (Type, Value) VALUES " "('TEMPERATURE', 22.5), ('HUMIDITY', 45.0);";
    const char* insertSensors =
        "INSERT OR IGNORE INTO Sensors (Type, Value) VALUES "
        "('TEMPERATURE', 22.5), ('HUMIDITY', 45.0);";
    sqlite3_exec(m_db, insertSensors, nullptr, nullptr, nullptr);

    const char* insertActuators =
        "INSERT OR IGNORE INTO Actuators (Type, State) VALUES "
        "('SERVO_ROOM', 1), ('SERVO_VAULT', 1), ('FAN', 0), ('ALARM', 0);";
    sqlite3_exec(m_db, insertActuators, nullptr, nullptr, nullptr);

    return true;
}


// void dDatabase::processDbMessage(const DatabaseMsg &msg) { switch (msg.command) {
void dDatabase::processDbMessage(const DatabaseMsg &msg) {
    m_currentRequestId = msg.requestId;
    switch (msg.command) {
        case DB_CMD_ENTER_ROOM_RFID:
            handleAccessRequest(msg.payload.rfid, true);
            break;
        case DB_CMD_LEAVE_ROOM_RFID:
            handleAccessRequest(msg.payload.rfid, false);
            break;
        case DB_CMD_UPDATE_ASSET:
            handleScanInventory(msg.payload.rfidInventory);
            break;
        case DB_CMD_WRITE_LOG:
            handleInsertLog(msg.payload.log);
            break;
        case DB_CMD_USER_IN_PIR:
            handleCheckUserInPir();
            break;
        case DB_CMD_LOGIN:
            handleLogin(msg.payload.login);
            break;
        case DB_CMD_GET_DASHBOARD:
            handleGetDashboard();
            break;
        case DB_CMD_GET_SENSORS:
            handleGetSensors();
            break;
        case DB_CMD_GET_ACTUATORS:
            handleGetActuators();
            break;

        
        case DB_CMD_REGISTER_USER:
            handleRegisterUser(msg.payload.user);
            break;
        case DB_CMD_GET_USERS:
            handleGetUsers();
            break;
        case DB_CMD_CREATE_USER:
            handleCreateUser(msg.payload.user);
            break;
        case DB_CMD_MODIFY_USER:
            handleModifyUser(msg.payload.user);
            break;
        case DB_CMD_REMOVE_USER:
            handleRemoveUser(msg.payload.userId);
            break;
        case DB_CMD_GET_ASSETS:
            handleGetAssets();
            break;
        case DB_CMD_CREATE_ASSET:
            handleCreateAsset(msg.payload.asset);
            break;
        case DB_CMD_MODIFY_ASSET:
            handleModifyAsset(msg.payload.asset);
            break;
        case DB_CMD_REMOVE_ASSET:
            handleRemoveAsset(msg.payload.asset.tag);
            break;
        case DB_CMD_GET_SETTINGS:
            handleGetSettings();
            break;
        case DB_CMD_UPDATE_SETTINGS:
            handleUpdateSettings(msg.payload.settings);
            break;
        case DB_CMD_FILTER_LOGS:
            handleFilterLogs(msg.payload.logFilter);
            break;
        default: ;
    }
}


void dDatabase::handleAccessRequest(const char* rfid, bool isEntering) {
    sqlite3_stmt* stmt;

    AuthResponse resp = {};

    resp.command = isEntering ? DB_CMD_ENTER_ROOM_RFID : DB_CMD_LEAVE_ROOM_RFID;

    
    const char* sqlSelect = "SELECT UserID, AccessLevel FROM Users WHERE RFID_Card = ?;";
    if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &stmt, nullptr) == SQLITE_OK) {
        // sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            resp.payload.auth.authorized = true;
            resp.payload.auth.userId = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
            resp.payload.auth.accessLevel = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));

            cout << "\n[CHECK] User Encontrado!" << endl;
            cout << " > UserID:      " << resp.payload.auth.userId << endl;
            cout << " > AccessLevel: " << resp.payload.auth.accessLevel << endl;
            cout << "----------------------------" << endl;
        }
        sqlite3_finalize(stmt);
    }

    
    if (resp.payload.auth.authorized) {
        
        
        int newState = isEntering ? 1 : 0;

        const char* sqlUpdate = "UPDATE Users SET IsInside = ? WHERE UserID = ?;";
        if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, newState);
            sqlite3_bind_int(stmt, 2, static_cast<int>(resp.payload.auth.userId));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    
    
    if (isEntering) {
        m_mqToVerifyRoom.send(&resp, sizeof(resp));
    } else {
        m_mqToLeaveRoom.send(&resp, sizeof(resp));
    }
}


// void dDatabase::handleInsertLog(const DatabaseLog& log) { sqlite3_stmt* stmt; const char* sql = "INSERT INTO Logs (LogType, EntityID, Value, Value2, Timestamp, Description) " "VALUES (?, ?, ?, ?, ?, ?);"; if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) { sqlite3_bind_int(stmt, 1, static_cast<int>(log.logType)); sqlite3_bind_int(stmt, 2, static_cast<int>(log.entityID)); sqlite3_bind_int(stmt, 3, static_cast<int>(log.value)); sqlite3_bind_int(stmt, 4, static_cast<int>(log.value2)); sqlite3_bind_int(stmt, 5, static_cast<int>(log.timestamp)); sqlite3_bind_text(stmt, 6, log.description, -1, SQLITE_STATIC); if (sqlite3_step(stmt) != SQLITE_DONE) { std::cerr << "[Erro dDatabase] Falha ao gravar log: " << sqlite3_errmsg(m_db) << std::endl; } else { std::cout << "[dDatabase] Novo log registado com sucesso." << std::endl; } sqlite3_finalize(stmt); } else { std::cerr << "[Erro dDatabase] Erro no prepare do SQL: " << sqlite3_errmsg(m_db) << std::endl; } if (log.logType == LOG_TYPE_SENSOR) { updateSensorTable(log.entityID, log.value, log.value2, log.timestamp); } else if (log.logType == LOG_TYPE_ACTUATOR) { updateActuatorTable(log.entityID, log.value, log.timestamp); } }
void dDatabase::handleInsertLog(const DatabaseLog& log) {
    sqlite3_stmt* stmt;

    std::string description = log.description;
    if (log.logType == LOG_TYPE_ACCESS && log.entityID > 0) {
        std::string name = getUserNameById(m_db, log.entityID);
        if (!name.empty()) {
            if (description.find("ENTROU") != std::string::npos) {
                description = "Utilizador " + name + " ENTROU na sala";
            } else if (description.find("SAIU") != std::string::npos) {
                description = "Utilizador " + name + " SAIU da sala";
            } else if (description.find("Cofre") != std::string::npos) {
                description = "Cofre Aberto - Utilizador: " + name;
            }
        }
    }

    const char* sql = "INSERT INTO Logs (LogType, EntityID, Value, Value2, Timestamp, Description) "
                      "VALUES (?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, static_cast<int>(log.logType));
        sqlite3_bind_int(stmt, 2, static_cast<int>(log.entityID));
        sqlite3_bind_double(stmt, 3, log.value);
        sqlite3_bind_double(stmt, 4, log.value2);
        sqlite3_bind_int(stmt, 5, static_cast<int>(log.timestamp));
        sqlite3_bind_text(stmt, 6, description.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "[Erro dDatabase] Falha ao gravar log: "
                      << sqlite3_errmsg(m_db) << std::endl;
        } else {
            std::cout << "[dDatabase] Novo log registado com sucesso." << std::endl;
        }

        sqlite3_finalize(stmt);
    } else {
        std::cerr << "[Erro dDatabase] Erro no prepare do SQL: "
                  << sqlite3_errmsg(m_db) << std::endl;
    }

    if (log.logType == LOG_TYPE_SENSOR) {
        updateSensorTable(static_cast<uint8_t>(log.entityID), log.value, log.value2, log.timestamp);
    } else if (log.logType == LOG_TYPE_ACTUATOR) {
        updateActuatorTable(static_cast<uint8_t>(log.entityID), log.value, log.timestamp);
    }
}


// void dDatabase::updateSensorTable(uint8_t entityID, uint16_t value, uint16_t value2, uint32_t timestamp) {
void dDatabase::updateSensorTable(uint8_t entityID, double value, double value2, uint32_t timestamp) {
    if (entityID == ID_SHT31) {
        sqlite3_stmt* stmt;

        const char* sqlTemp = "UPDATE Sensors SET Value = ?, LastUpdate = ? WHERE Type = 'TEMPERATURE';";
        if (sqlite3_prepare_v2(m_db, sqlTemp, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(stmt, 1, value);
            sqlite3_bind_int(stmt, 2, static_cast<int>(timestamp));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        const char* sqlHum = "UPDATE Sensors SET Value = ? , LastUpdate = ? WHERE Type = 'HUMIDITY';";
        if (sqlite3_prepare_v2(m_db, sqlHum, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(stmt, 1, value2);
            sqlite3_bind_int(stmt, 2, static_cast<int>(timestamp));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

// void dDatabase::updateActuatorTable(uint8_t entityID, uint16_t value, uint32_t timestamp) {
void dDatabase::updateActuatorTable(uint8_t entityID, double value, uint32_t timestamp) {
    sqlite3_stmt* stmt;
    const char* type = nullptr;

    switch(entityID) {
        case ID_SERVO_ROOM:  type = "SERVO_ROOM";  break;
        case ID_SERVO_VAULT: type = "SERVO_VAULT"; break;
        case ID_FAN:         type = "FAN";         break;
        case ID_ALARM_ACTUATOR: type = "ALARM";    break;
        default: return;
    }

    const char* sql = "UPDATE Actuators SET State = ?, LastUpdate = ? WHERE Type = ?;";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, value > 0.0 ? 1 : 0);
        sqlite3_bind_int(stmt, 2, static_cast<int>(timestamp));
        // sqlite3_bind_text(stmt, 3, type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, type, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}


void dDatabase::handleCheckUserInPir() {
    sqlite3_stmt* stmt;

    AuthResponse resp = {};

    resp.command = DB_CMD_USER_IN_PIR;
    
    const char* sql = "SELECT COUNT(*) FROM Users WHERE IsInside = 1;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            if (count > 0) {
                resp.payload.auth.authorized = true; 
            }
        }
        sqlite3_finalize(stmt);
    }

    
    
    m_mqToCheckMovement.send(&resp, sizeof(resp));
}

// void dDatabase:: handleScanInventory(const Data_RFID_Inventory& inventory) { sqlite3_stmt* stmt; uint32_t now = static_cast<uint32_t>(time(nullptr)); const char* sqlReset = "UPDATE Assets SET State = 'Outside';"; int rc = sqlite3_exec(m_db, sqlReset, nullptr, nullptr, nullptr); if (rc != SQLITE_OK) { std::cerr << "[DB] Erro ao resetar estados: " << sqlite3_errmsg(m_db) << std::endl; return; } const char* sqlUpdate = "UPDATE Assets SET State = 'Inside', LastRead = ? WHERE RFID_Tag = ?;"; if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &stmt, nullptr) == SQLITE_OK) {
void dDatabase:: handleScanInventory(const Data_RFID_Inventory& inventory) {
    sqlite3_stmt* stmt;
    uint32_t now = static_cast<uint32_t>(time(nullptr));

    const char* sqlUpdate = "UPDATE Assets SET LastRead = ? WHERE RFID_Tag = ?;";

    if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &stmt, nullptr) == SQLITE_OK) {

        
        for (int i = 0; i < inventory.tagCount; ++i) {

            
            sqlite3_bind_int(stmt, 1, static_cast<int>(now));
            // sqlite3_bind_text(stmt, 2, inventory.tagList[i], -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, inventory.tagList[i], -1, SQLITE_TRANSIENT);

            
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "[DB] Erro ao atualizar Tag: " << inventory.tagList[i] << std::endl;
            }
            if (sqlite3_changes(m_db) == 0) {
                const char* sqlInsert = "INSERT INTO Assets (Name, RFID_Tag, LastRead) VALUES ('Item Desconhecido', ?, ?);";
                sqlite3_stmt* stmtIns;

                if (sqlite3_prepare_v2(m_db, sqlInsert, -1, &stmtIns, nullptr) == SQLITE_OK) {
                    // sqlite3_bind_text(stmtIns, 1, inventory.tagList[i], -1, SQLITE_STATIC);
                    sqlite3_bind_text(stmtIns, 1, inventory.tagList[i], -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmtIns, 2, static_cast<int>(now));
                    sqlite3_step(stmtIns);
                    sqlite3_finalize(stmtIns);
                    std::cout << "[DB] Novo ativo detetado e registado: " << inventory.tagList[i] << std::endl;
                }
            }

            
            sqlite3_reset(stmt);
        }

        sqlite3_finalize(stmt);
        std::cout << "[DB] Inventário atualizado com sucesso (" << inventory.tagCount << " itens)." << std::endl;
    } else {
        std::cerr << "[DB] Erro no prepare do inventário: " << sqlite3_errmsg(m_db) << std::endl;
    }
}


void dDatabase::handleLogin(const LoginRequest& login) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};
    nlohmann::json result;

    
    char fullName[70];
    snprintf(fullName, sizeof(fullName), "%s_viewer", login.username);

    
    const char* sql = "SELECT UserID, Name, AccessLevel, Password FROM Users "
                      "WHERE Name = ? AND Password IS NOT NULL;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        // sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* storedHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            if (storedHash && argon2id_verify(storedHash, login.password, std::strlen(login.password)) == ARGON2_OK) {
                result["userId"] = sqlite3_column_int(stmt, 0);

                
                const char* dbName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                std::string displayName = dbName ? dbName : "";
                if (displayName.size() > 7 && displayName.substr(displayName.size() - 7) == "_viewer") {
                    displayName = displayName.substr(0, displayName.size() - 7);
                }
                result["username"] = displayName;

                result["accessLevel"] = sqlite3_column_int(stmt, 2);
                resp.success = true;
            } else {
                resp.success = false;
                strncpy(resp.errorMsg, "Invalid credentials", sizeof(resp.errorMsg) - 1);
                resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
            }
        } else {
            resp.success = false;
            strncpy(resp.errorMsg, "Invalid credentials", sizeof(resp.errorMsg) - 1);
            resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (resp.success) {
        std::string json = result.dump();
        strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
        resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';
    } else {
        resp.jsonData[0] = '\0';
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetDashboard() {
    nlohmann::json response;
    sqlite3_stmt* stmt;

    
    const char* sql =
      "SELECT LogType FROM Logs "
      "WHERE LogType IN (?, ?) "
      "ORDER BY Timestamp DESC LIMIT 1;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, LOG_TYPE_ALERT);
        sqlite3_bind_int(stmt, 2, LOG_TYPE_ACCESS);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int lt = sqlite3_column_int(stmt, 0);
            response["security"] = (lt == LOG_TYPE_ALERT) ? "ALERT" : "Secure";
        } else {
            response["security"] = "Secure";
        }
        sqlite3_finalize(stmt);
    }

    
    sql = "SELECT State FROM Actuators WHERE Type = 'SERVO_VAULT';";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["vault"] = sqlite3_column_int(stmt, 0) > 0 ? "Locked" : "Unlocked";
        } else {
            response["vault"] = "Locked";
        }
        sqlite3_finalize(stmt);
    }

    
    sql = "SELECT Value FROM Sensors WHERE Type = 'TEMPERATURE';";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["temp"] = sqlite3_column_double(stmt, 0);
        } else {
            response["temp"] = 22.5;
        }
        sqlite3_finalize(stmt);
    }

    
    sql = "SELECT Value FROM Sensors WHERE Type = 'HUMIDITY';";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["hum"] = sqlite3_column_double(stmt, 0);
        } else {
            response["hum"] = 45.0;
        }
        sqlite3_finalize(stmt);
    }

    DbWebResponse resp = {};
    resp.requestId = m_currentRequestId;
    resp.success = true;
    std::string json = response.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetSensors() {
    nlohmann::json response;
    sqlite3_stmt* stmt;

    
    
    
    
    const char* sqlEnv = "SELECT Type, Value, LastUpdate FROM Sensors;";

    if (sqlite3_prepare_v2(m_db, sqlEnv, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            double value = sqlite3_column_double(stmt, 1);
            time_t ts = sqlite3_column_int(stmt, 2); 

            
            char timeBuf[10] = "--:--";
            if (ts > 0) {
                struct tm* timeinfo = localtime(&ts);
                strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
            }

            if (type == "TEMPERATURE") {
                response["env"]["temp"] = value;
                response["env"]["tempTime"] = timeBuf; 
            }
            else if (type == "HUMIDITY") {
                response["env"]["hum"] = value;
                response["env"]["humTime"] = timeBuf; 
            }
        }
        sqlite3_finalize(stmt);
    }

    
    
    
    // const char* sqlVault = "SELECT Description, Timestamp FROM Logs " "WHERE LogType = ? AND EntityID != 0 " "ORDER BY Timestamp DESC LIMIT 1;";
    const char* sqlVault =
        "SELECT u.Name, l.Timestamp, l.EntityID FROM Logs l "
        "LEFT JOIN Users u ON l.EntityID = u.UserID "
        "WHERE l.LogType = ? AND l.Description LIKE '%Cofre%' "
        "ORDER BY l.Timestamp DESC LIMIT 1;";

    if (sqlite3_prepare_v2(m_db, sqlVault, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, LOG_TYPE_ACCESS);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            response["vault"]["user"] = name ? name : "--";
            time_t ts = sqlite3_column_int(stmt, 1);
            struct tm* timeinfo = localtime(&ts);
            char buffer[6];
            strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);
            response["vault"]["time"] = buffer;
        } else {
            response["vault"]["user"] = "--";
            response["vault"]["time"] = "--";
        }
        sqlite3_finalize(stmt);
    }

    
    const char* sqlVaultDoor = "SELECT State FROM Actuators WHERE Type = 'SERVO_VAULT';";
    if (sqlite3_prepare_v2(m_db, sqlVaultDoor, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["vault"]["door"] = sqlite3_column_int(stmt, 0) > 0 ? "Locked" : "Unlocked";
        }
        sqlite3_finalize(stmt);
    }

    
    
    
    const char* sqlRoom = "SELECT Name FROM Users WHERE IsInside = 1 LIMIT 1;";
    if (sqlite3_prepare_v2(m_db, sqlRoom, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["room"]["rfidIn"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        } else {
            response["room"]["rfidIn"] = "--";
        }
        sqlite3_finalize(stmt);
    }

    
    
    
    // const char* sqlRoomTime = "SELECT Timestamp FROM Logs WHERE LogType = ? " "ORDER BY Timestamp DESC LIMIT 1;";
    const char* sqlRoomTime =
        "SELECT Timestamp FROM Logs WHERE LogType = ? AND Description LIKE '%ENTROU%' "
        "ORDER BY Timestamp DESC LIMIT 1;";

    if (sqlite3_prepare_v2(m_db, sqlRoomTime, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, LOG_TYPE_ACCESS);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            time_t ts = sqlite3_column_int(stmt, 0);
            struct tm* timeinfo = localtime(&ts);
            char buffer[6];
            strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);
            response["room"]["timeIn"] = buffer;
        } else {
            response["room"]["timeIn"] = "--";
        }
        sqlite3_finalize(stmt);
    }

    
    const char* sqlRoomDoor = "SELECT State FROM Actuators WHERE Type = 'SERVO_ROOM';";
    if (sqlite3_prepare_v2(m_db, sqlRoomDoor, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["room"]["door"] = sqlite3_column_int(stmt, 0) > 0 ? "Closed" : "Open";
        }
        sqlite3_finalize(stmt);
    }

    
    
    
    // const char* sqlRoomOut = "SELECT u.Name, l.Timestamp FROM Logs l " "JOIN Users u ON l.EntityID = u.UserID " "WHERE l.LogType = ? AND l.Description LIKE '%SAIU%' " "ORDER BY l.Timestamp DESC LIMIT 1;";
    const char* sqlRoomOut =
        "SELECT u.Name, l.Timestamp FROM Logs l "
        "LEFT JOIN Users u ON l.EntityID = u.UserID "
        "WHERE l.LogType = ? AND l.Description LIKE '%SAIU%' "
        "ORDER BY l.Timestamp DESC LIMIT 1;";

    if (sqlite3_prepare_v2(m_db, sqlRoomOut, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, LOG_TYPE_ACCESS);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* userName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

            
            std::string displayName(userName ? userName : "Unknown");
            

            response["room"]["rfidOut"] = displayName;

            time_t ts = sqlite3_column_int(stmt, 1);
            struct tm* timeinfo = localtime(&ts);
            char buffer[6];
            strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);
            response["room"]["timeOut"] = buffer;
        } else {
            response["room"]["rfidOut"] = "--";
            response["room"]["timeOut"] = "--";
        }
        sqlite3_finalize(stmt);
    }

    
    
    
    const char* sqlUHF =
        "SELECT Timestamp FROM Logs WHERE LogType = ? "
        "ORDER BY Timestamp DESC LIMIT 1;";

    if (sqlite3_prepare_v2(m_db, sqlUHF, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, LOG_TYPE_INVENTORY);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            time_t lastScan = sqlite3_column_int(stmt, 0);
            time_t now = time(nullptr);

            if ((now - lastScan) < 300) { 
                response["uhf"] = "Active";
            } else {
                response["uhf"] = "Idle";
            }
        } else {
            response["uhf"] = "Never Scanned";
        }
        sqlite3_finalize(stmt);
    }

    
    
    
    DbWebResponse resp = {};
    resp.requestId = m_currentRequestId;
    resp.success = true;
    std::string json = response.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetActuators() {
    nlohmann::json response;
    sqlite3_stmt* stmt;

    
    const char* sql = "SELECT Type, State, LastUpdate FROM Actuators;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int state = sqlite3_column_int(stmt, 1);
            time_t ts = sqlite3_column_int(stmt, 2); 
            
            char timeBuf[6] = "--:--";
            if (ts > 0) { 
                struct tm* ti = localtime(&ts);
                strftime(timeBuf, sizeof(timeBuf), "%H:%M", ti);
            }

            if (type == "FAN") {
                response["fan"]["state"] = state ? "ON" : "OFF";
                response["fan"]["timestamp"] = timeBuf;
            } else if (type == "ALARM") {
                response["buzzer"]["state"] = state ? "ACTIVE" : "INACTIVE";
                response["buzzer"]["timestamp"] = timeBuf; 
                response["led"]["state"] = state ? "ON" : "OFF";
                response["led"]["timestamp"] = timeBuf; 
            } else if (type == "SERVO_VAULT") {
                response["vaultDoor"]["state"] = state ? "LOCKED" : "UNLOCKED";
                response["vaultDoor"]["timestamp"] = timeBuf;
            } else if (type == "SERVO_ROOM") {
                response["roomDoor"]["state"] = state ? "LOCKED" : "UNLOCKED";
                response["roomDoor"]["timestamp"] = timeBuf;
            }
        }
        sqlite3_finalize(stmt);
    }

    DbWebResponse resp = {};
    resp.requestId = m_currentRequestId;
    resp.success = true;
    std::string json = response.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}


void dDatabase::handleRegisterUser(const UserData& user) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    
    char fullName[70];
    snprintf(fullName, sizeof(fullName), "%s_viewer", user.name);

    
    const char* checkSql = "SELECT COUNT(*) FROM Users WHERE Name = ?;";
    int count = 0;

    if (sqlite3_prepare_v2(m_db, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        // sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    
    if (count > 0) {
        resp.requestId = m_currentRequestId;
        resp.success = false;
        strncpy(resp.errorMsg, "Username already taken", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        m_mqToWeb.send(&resp, sizeof(resp));
        return;  
    }

    
    
    char hash[128];
    uint8_t salt[16];
    srand(time(nullptr) + rand());
    for (int i = 0; i < 16; i++) salt[i] = rand() % 256;

    argon2id_hash_encoded(2, 16384, 1, user.password, strlen(user.password),
                         salt, sizeof(salt), 32, hash, sizeof(hash));

    
    const char* sql = "INSERT INTO Users (Name, Password, AccessLevel) VALUES (?, ?, 0);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        // sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_TRANSIENT);
        // sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strncpy(resp.jsonData, R"({"status":"ok"})", sizeof(resp.jsonData) - 1);
            resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';
        } else {
            resp.success = false;
            strncpy(resp.errorMsg, "Database error", sizeof(resp.errorMsg) - 1);
            resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (!resp.success) {
        resp.jsonData[0] = '\0';
    }
    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetUsers() {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};
    nlohmann::json users = nlohmann::json::array();

    // const char* sql = "SELECT UserID, Name, RFID_Card, FingerprintID, AccessLevel " "FROM Users " "WHERE (RFID_Card IS NOT NULL OR FingerprintID IS NOT NULL)" "AND AccessLevel > 0;";
    const char* sql = "SELECT UserID, Name, RFID_Card, FingerprintID "
                      "FROM Users "
                      "WHERE UserID != 1;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            nlohmann::json user;
            user["id"] = sqlite3_column_int(stmt, 0);
            user["name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

            const char* rfid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            user["rfid"] = rfid ? rfid : "";

            int fingerprintId = sqlite3_column_int(stmt, 3);
            user["fingerprint"] = fingerprintId;

            bool hasRfid = rfid && rfid[0] != '\0';
            bool hasFingerprint = fingerprintId > 0;
            int level = computeAccessLevel(hasRfid, hasFingerprint);
            if (level == 1) {
                user["access"] = "Room";
            } else if (level == 2) {
                user["access"] = "Room/Vault";
            } else {
                user["access"] = "None";
            }

            users.push_back(user);
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    resp.success = true;
    std::string json = users.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}

// void dDatabase::handleCreateUser(const UserData& user) {
    void dDatabase::handleCreateUser(const UserData& user) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    bool hasRfid = !isEmptyString(user.rfid);
    bool hasFingerprint = user.fingerprintID > 0;
    int accessLevel = computeAccessLevel(hasRfid, hasFingerprint);

    if (!hasRfid && !hasFingerprint) {
        resp.requestId = m_currentRequestId;
        resp.success = false;
        strncpy(resp.errorMsg, "RFID or fingerprint is required", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        m_mqToWeb.send(&resp, sizeof(resp));
        return;
    }

    const char* sql =
        "INSERT INTO Users (Name, RFID_Card, FingerprintID, AccessLevel) "
        "VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        bindTextOrNull(stmt, 1, user.name);
        bindTextOrNull(stmt, 2, user.rfid);
        if (hasFingerprint) {
            sqlite3_bind_int(stmt, 3, user.fingerprintID);
        } else {
            sqlite3_bind_null(stmt, 3);
        }
        sqlite3_bind_int(stmt, 4, accessLevel);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strncpy(resp.jsonData, "{\"status\":\"ok\"}", sizeof(resp.jsonData) - 1);
            resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

            if (user.fingerprintID > 0) {
                AuthResponse cmd = {};
                cmd.command = DB_CMD_ADD_USER;
                cmd.payload.auth.userId = user.fingerprintID;
                m_mqToFingerprint.send(&cmd, sizeof(cmd));
            }
        } else {
            resp.success = false;
            strncpy(resp.errorMsg, "Failed to create user", sizeof(resp.errorMsg) - 1);
            resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (!resp.success) {
        resp.jsonData[0] = '\0';
    }
    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleModifyUser(const UserData& user) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    if (user.userID == 1) {
        resp.requestId = m_currentRequestId;
        resp.success = false;
        strncpy(resp.errorMsg, "Cannot modify admin user", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        m_mqToWeb.send(&resp, sizeof(resp));
        return;
    }

    int existingFingerprint = 0;
    bool hasExisting = false;
    const char* sqlSelect = "SELECT FingerprintID FROM Users WHERE UserID = ?;";
    if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user.userID);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            existingFingerprint = sqlite3_column_int(stmt, 0);
            hasExisting = true;
        }
        sqlite3_finalize(stmt);
    }

    if (!hasExisting) {
        resp.requestId = m_currentRequestId;
        resp.success = false;
        strncpy(resp.errorMsg, "User not found", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        m_mqToWeb.send(&resp, sizeof(resp));
        return;
    }

    int finalFingerprint = existingFingerprint;
    bool shouldAddFingerprint = false;
    if (existingFingerprint <= 0 && user.fingerprintID > 0) {
        finalFingerprint = user.fingerprintID;
        shouldAddFingerprint = true;
    }

    bool hasRfid = !isEmptyString(user.rfid);
    bool hasFingerprint = finalFingerprint > 0;
    int accessLevel = computeAccessLevel(hasRfid, hasFingerprint);

    if (!hasRfid && !hasFingerprint) {
        resp.requestId = m_currentRequestId;
        resp.success = false;
        strncpy(resp.errorMsg, "RFID or fingerprint is required", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        m_mqToWeb.send(&resp, sizeof(resp));
        return;
    }

    const char* sql =
        "UPDATE Users SET Name=?, RFID_Card=?, FingerprintID=?, AccessLevel=? "
        "WHERE UserID=?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        bindTextOrNull(stmt, 1, user.name);
        bindTextOrNull(stmt, 2, user.rfid);
        if (finalFingerprint > 0) {
            sqlite3_bind_int(stmt, 3, finalFingerprint);
        } else {
            sqlite3_bind_null(stmt, 3);
        }
        sqlite3_bind_int(stmt, 4, accessLevel);
        sqlite3_bind_int(stmt, 5, user.userID);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strncpy(resp.jsonData, "{\"status\":\"ok\"}", sizeof(resp.jsonData) - 1);
            resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';
            if (shouldAddFingerprint) {
                AuthResponse cmd = {};
                cmd.command = DB_CMD_ADD_USER;
                cmd.payload.auth.userId = static_cast<uint32_t>(finalFingerprint);
                m_mqToFingerprint.send(&cmd, sizeof(cmd));
            }
        } else {
            resp.success = false;
            strncpy(resp.errorMsg, "Failed to modify user", sizeof(resp.errorMsg) - 1);
            resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (!resp.success) {
        resp.jsonData[0] = '\0';
    }
    m_mqToWeb.send(&resp, sizeof(resp));
}

// void dDatabase::handleRemoveUser(int userId) {
void dDatabase::handleRemoveUser(uint32_t userId) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    if (userId == 1) {
        resp.requestId = m_currentRequestId;
        resp.success = false;
        strncpy(resp.errorMsg, "Cannot delete admin user", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        m_mqToWeb.send(&resp, sizeof(resp));
        return;
    }

    int fingerprintId = 0;
    const char* sqlSelect = "SELECT FingerprintID FROM Users WHERE UserID = ?;";
    if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &stmt, nullptr) == SQLITE_OK) {
        // sqlite3_bind_int(stmt, 1, userId);
        sqlite3_bind_int(stmt, 1, static_cast<int>(userId));
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            fingerprintId = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    const char* sql = "DELETE FROM Users WHERE UserID = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        // sqlite3_bind_int(stmt, 1, userId);
        sqlite3_bind_int(stmt, 1, static_cast<int>(userId));

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strncpy(resp.jsonData, "{\"status\":\"ok\"}", sizeof(resp.jsonData) - 1);
            resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

            if (fingerprintId > 0) {
                AuthResponse cmd = {};
                cmd.command = DB_CMD_DELETE_USER;
                cmd.payload.auth.userId = static_cast<uint32_t>(fingerprintId);
                m_mqToFingerprint.send(&cmd, sizeof(cmd));
            }
        } else {
            resp.success = false;
            strncpy(resp.errorMsg, "Failed to delete user", sizeof(resp.errorMsg) - 1);
            resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (!resp.success) {
        resp.jsonData[0] = '\0';
    }
    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetAssets() {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};
    nlohmann::json assets = nlohmann::json::array();

    // const char* sql = "SELECT Name, RFID_Tag, State, LastRead FROM Assets;";
    const char* sql = "SELECT Name, RFID_Tag, LastRead FROM Assets;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        time_t lastScan = 0;
        sqlite3_stmt* scanStmt = nullptr;
        const char* sqlScan = "SELECT Timestamp FROM Logs WHERE LogType = ? ORDER BY Timestamp DESC LIMIT 1;";
        if (sqlite3_prepare_v2(m_db, sqlScan, -1, &scanStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(scanStmt, 1, LOG_TYPE_INVENTORY);
            if (sqlite3_step(scanStmt) == SQLITE_ROW) {
                lastScan = sqlite3_column_int(scanStmt, 0);
            }
            sqlite3_finalize(scanStmt);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            nlohmann::json asset;
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* tag = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            asset["name"] = name ? name : "";
            asset["tag"] = tag ? tag : "";

            time_t ts = sqlite3_column_int(stmt, 2);
            if (lastScan > 0 && ts >= lastScan) {
                asset["state"] = "Inside";
            } else {
                asset["state"] = "Outside";
            }

            if (ts > 0) {
                struct tm* timeinfo = localtime(&ts);
                char buffer[20];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
                asset["lastSeen"] = buffer;
            } else {
                asset["lastSeen"] = "--";
            }

            assets.push_back(asset);
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    resp.success = true;
    std::string json = assets.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleCreateAsset(const AssetData& asset) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    // const char* sql = "INSERT INTO Assets (Name, RFID_Tag, State, LastRead) " "VALUES (?, ?, ?, ?);";
    const char* sql =
        "INSERT INTO Assets (Name, RFID_Tag, LastRead) "
        "VALUES (?, ?, ?);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        bindTextOrNull(stmt, 1, asset.name);
        bindTextOrNull(stmt, 2, asset.tag);
        sqlite3_bind_int(stmt, 3, 0);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strncpy(resp.jsonData, "{\"status\":\"ok\"}", sizeof(resp.jsonData) - 1);
            resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (!resp.success) {
        strncpy(resp.errorMsg, "Failed to create asset", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        resp.jsonData[0] = '\0';
    }
    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleModifyAsset(const AssetData& asset) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    // const char* sql = "UPDATE Assets SET Name=?, State=? WHERE RFID_Tag=?;";
    const char* sql =
        "UPDATE Assets SET Name=? WHERE RFID_Tag=?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        bindTextOrNull(stmt, 1, asset.name);
        bindTextOrNull(stmt, 2, asset.tag);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strncpy(resp.jsonData, "{\"status\":\"ok\"}", sizeof(resp.jsonData) - 1);
            resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (!resp.success) {
        strncpy(resp.errorMsg, "Failed to modify asset", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        resp.jsonData[0] = '\0';
    }
    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleRemoveAsset(const char* tag) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    const char* sql = "DELETE FROM Assets WHERE RFID_Tag = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        // sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strncpy(resp.jsonData, "{\"status\":\"ok\"}", sizeof(resp.jsonData) - 1);
            resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (!resp.success) {
        strncpy(resp.errorMsg, "Failed to delete asset", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        resp.jsonData[0] = '\0';
    }
    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetSettings() {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};
    nlohmann::json settings;

    const char* sql = "SELECT TempThreshold, SamplingTime FROM SystemSettings WHERE ID = 1;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            settings["tempLimit"] = sqlite3_column_int(stmt, 0);
            settings["sampleTime"] = sqlite3_column_int(stmt, 1) / 60;  
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    resp.success = true;
    std::string json = settings.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleUpdateSettings(const SystemSettings& settings) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    const char* sql = "UPDATE SystemSettings SET TempThreshold=?, SamplingTime=? WHERE ID=1;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, settings.tempThreshold);
        sqlite3_bind_int(stmt, 2, settings.samplingInterval);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strncpy(resp.jsonData, "{\"status\":\"saved\"}", sizeof(resp.jsonData) - 1);
            resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

            AuthResponse cmd = {};
            cmd.command = DB_CMD_UPDATE_SETTINGS;
            cmd.payload.settings.tempThreshold = settings.tempThreshold;
            cmd.payload.settings.samplingInterval = settings.samplingInterval;
            m_mqToEnvThread.send(&cmd, sizeof(cmd));
        }
        sqlite3_finalize(stmt);
    }

    resp.requestId = m_currentRequestId;
    if (!resp.success) {
        strncpy(resp.errorMsg, "Failed to update settings", sizeof(resp.errorMsg) - 1);
        resp.errorMsg[sizeof(resp.errorMsg) - 1] = '\0';
        resp.jsonData[0] = '\0';
    }
    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleFilterLogs(const LogFilter& filter) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};
    nlohmann::json result;

    
    time_t now = time(nullptr);
    time_t startTime = now;

    if (strcmp(filter.timeRange, "1 Hour") == 0) startTime -= 3600;
    else if (strcmp(filter.timeRange, "1 Day") == 0) startTime -= 86400;
    else if (strcmp(filter.timeRange, "1 Week") == 0) startTime -= 604800;
    else if (strcmp(filter.timeRange, "1 Month") == 0) startTime -= 2592000;

    
    if (strcmp(filter.logType, "Temperature") == 0 ||
        strcmp(filter.logType, "Humidity") == 0) {

        std::string colName = (strcmp(filter.logType, "Temperature") == 0) ? "Value" : "Value2";

        
        std::string sqlStr = "SELECT Timestamp, " + colName +
                             " FROM Logs WHERE LogType = ? AND EntityID = ? AND Timestamp > ? "
                             " ORDER BY Timestamp ASC LIMIT 50;"; 

        if (sqlite3_prepare_v2(m_db, sqlStr.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            
            sqlite3_bind_int(stmt, 1, LOG_TYPE_SENSOR);
            sqlite3_bind_int(stmt, 2, ID_SHT31); 
            // sqlite3_bind_int(stmt, 3, (int)startTime);
            sqlite3_bind_int(stmt, 3, static_cast<int>(startTime));

            nlohmann::json labels = nlohmann::json::array();
            nlohmann::json data = nlohmann::json::array();

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                time_t ts = sqlite3_column_int(stmt, 0);
                struct tm* timeinfo = localtime(&ts);
                char buffer[6];
                strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);

                labels.push_back(buffer);
                data.push_back(sqlite3_column_double(stmt, 1));
            }
            sqlite3_finalize(stmt);

            result["graph"]["labels"] = labels;
            if (strcmp(filter.logType, "Temperature") == 0) {
                result["graph"]["temp"] = data;
            } else {
                result["graph"]["hum"] = data;
            }
        }
    }
    
    else {
        int logTypeFilter = LOG_TYPE_SYSTEM;
        if (strcmp(filter.logType, "Intrusion") == 0) logTypeFilter = LOG_TYPE_ALERT;
        else if (strcmp(filter.logType, "Accesses") == 0) logTypeFilter = LOG_TYPE_ACCESS;
        else if (strcmp(filter.logType, "Assets") == 0) logTypeFilter = LOG_TYPE_INVENTORY;   
        else if (strcmp(filter.logType, "Users") == 0) logTypeFilter = LOG_TYPE_ACCESS;       

        // const char* sql = "SELECT Timestamp, LogType, Description FROM Logs " "WHERE LogType = ? AND Timestamp > ? " "ORDER BY Timestamp DESC LIMIT 100;";
        const char* sql =
            "SELECT Timestamp, LogType, Description, EntityID FROM Logs "
            "WHERE LogType = ? AND Timestamp > ? "
            "ORDER BY Timestamp DESC LIMIT 100;";

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, logTypeFilter);
            sqlite3_bind_int(stmt, 2, startTime);

            nlohmann::json logs = nlohmann::json::array();

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                nlohmann::json log;

                time_t ts = sqlite3_column_int(stmt, 0);
                struct tm* timeinfo = localtime(&ts);
                char buffer[20];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
                log["time"] = buffer;

                int type = sqlite3_column_int(stmt, 1);
                log["type"] = (type == LOG_TYPE_ALERT) ? "Alert" : "Info";

                const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                uint32_t entityId = static_cast<uint32_t>(sqlite3_column_int(stmt, 3));
                std::string description = desc ? desc : "";
                if (type == LOG_TYPE_ACCESS && entityId > 0) {
                    std::string name = getUserNameById(m_db, entityId);
                    if (!name.empty()) {
                        if (description.find("ENTROU") != std::string::npos) {
                            description = "Utilizador " + name + " ENTROU na sala";
                        } else if (description.find("SAIU") != std::string::npos) {
                            description = "Utilizador " + name + " SAIU da sala";
                        } else if (description.find("Cofre") != std::string::npos) {
                            description = "Cofre Aberto - Utilizador: " + name;
                        }
                    }
                }

                log["desc"] = description;

                logs.push_back(log);
            }
            sqlite3_finalize(stmt);

            result["logs"] = logs;
        }
    }

    resp.requestId = m_currentRequestId;
    resp.success = true;
    std::string json = result.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}
