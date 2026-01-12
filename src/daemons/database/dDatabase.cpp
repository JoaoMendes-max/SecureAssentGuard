#include "dDatabase.h"
#include <iostream>
#include <argon2.h>

dDatabase::dDatabase(const std::string& dbPath,
                   C_Mqueue& mqDb,
                   C_Mqueue& mqRfidIn,
                   C_Mqueue& mqRfidOut,
                   C_Mqueue& mqFinger,
                   C_Mqueue& m_mqToCheckMovement,
                   C_Mqueue& mqToWeb,
                   C_Mqueue& mqToEnv)   // <-- NOVO
    : m_db(nullptr),
      m_dbPath(dbPath),
      m_mqToDatabase(mqDb),
      m_mqToVerifyRoom(mqRfidIn),
      m_mqToLeaveRoom(mqRfidOut),
      m_mqToFingerprint(mqFinger),
      m_mqToCheckMovement(m_mqToCheckMovement),
      m_mqToWeb(mqToWeb),
      m_mqToEnvThread(mqToEnv)      // <-- INICIALIZAR
{
}

// O destruidor garante que, se te esqueceres, a base de dados fecha ao sair
dDatabase::~dDatabase() {
    close();
}

bool dDatabase::open() {

    int result = sqlite3_open(m_dbPath.c_str(), &m_db);

    if (result != SQLITE_OK) {
        std::cerr << "ERRO: Não foi possível abrir a base de dados: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }

    std::cout << "SUCESSO: Ligado ao ficheiro: " << m_dbPath << std::endl;
    return true;
}

void dDatabase::close() {
    if (m_db) {
        // Fecha a ligação e liberta a memória
        sqlite3_close(m_db);
        m_db = nullptr; // Importante para não tentarmos usar um ponteiro inválido
        std::cout << "Base de dados fechada corretamente." << std::endl;
    }
}


bool dDatabase::initializeSchema() {
    if (!m_db) {
        std::cerr << "Erro: A base de dados não está aberta!" << std::endl;
        return false;
    }

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
        "Value INTEGER, "
        "Value2 INTEGER DEFAULT 0);"

        "CREATE TABLE IF NOT EXISTS Assets ("
        "AssetID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Name TEXT, "
        "RFID_Tag TEXT UNIQUE, "
        "State TEXT, "
        "LastRead INTEGER);"

        "CREATE TABLE IF NOT EXISTS Sensors ("
        "SensorID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Type TEXT UNIQUE, "
        "Value REAL);"

        "CREATE TABLE IF NOT EXISTS Actuators ("
        "ActuatorID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Type TEXT UNIQUE, "
        "State INTEGER);"

        // ↓ NOVA TABELA DE CONFIGURAÇÃO ↓
        "CREATE TABLE IF NOT EXISTS SystemSettings ("
        "ID INTEGER PRIMARY KEY CHECK (ID = 1), "  // Só 1 linha!
        "TempThreshold INTEGER DEFAULT 30, "
        "SamplingTime INTEGER DEFAULT 600);";  // 10 minutos

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::cerr << "Erro ao criar tabelas: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    // ↓ INICIALIZAR SETTINGS (se não existir) ↓
    const char* initSettings =
        "INSERT OR IGNORE INTO SystemSettings (ID, TempThreshold, SamplingTime) "
        "VALUES (1, 30, 600);";
    sqlite3_exec(m_db, initSettings, nullptr, nullptr, nullptr);

    // Hash admin password
    const char* password = "1234";
    char hash[128];
    uint8_t salt[16];
    srand(time(nullptr));
    for (int i = 0; i < 16; i++) {
        salt[i] = rand() % 256;
    }

    argon2id_hash_encoded(2, 16384, 1, password, strlen(password),
                         salt, sizeof(salt), 32, hash, sizeof(hash));

    // ↓ CRIAR USER ADMIN (se não existir) ↓
    const char* createAdmin =
        "INSERT OR IGNORE INTO Users (UserID, Name, Password, AccessLevel, FingerprintID) "
        "VALUES (1, 'admin', ?, 2, 1);";  // AccessLevel 2 = Admin

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, createAdmin, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Insert sensores/atuadores
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


void dDatabase::processDbMessage(const DatabaseMsg &msg) {
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

        // ↓ NOVOS HANDLERS ↓
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
        case DB_CMD_ADD_USER:  // FINGERPRINT!
            handleAddFingerprint(msg.payload.userId);
            break;
        case DB_CMD_DELETE_USER:  // FINGERPRINT!
            handleDeleteFingerprint(msg.payload.userId);
            break;
    }
}


void dDatabase::handleAccessRequest(const char* rfid, bool isEntering) {
    sqlite3_stmt* stmt;

    AuthResponse resp = {};

    resp.command = isEntering ? DB_CMD_ENTER_ROOM_RFID : DB_CMD_LEAVE_ROOM_RFID;

    // 1. A validação é igual para os dois (o cartão existe?)
    const char* sqlSelect = "SELECT UserID, AccessLevel FROM Users WHERE RFID_Card = ?;";
    if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {//se encontrarem preenchem a resposta
            resp.payload.auth.authorized = true;
            resp.payload.auth.userId = (uint32_t)sqlite3_column_int(stmt, 0);
            resp.payload.auth.accessLevel = (uint32_t)sqlite3_column_int(stmt, 1);

            cout << "\n[CHECK] User Encontrado!" << endl;
            cout << " > UserID:      " << resp.payload.auth.userId << endl;
            cout << " > AccessLevel: " << resp.payload.auth.accessLevel << endl;
            cout << "----------------------------" << endl;
        }
        sqlite3_finalize(stmt);
    }

    // 2. Se autorizado, atualizamos o estado de forma explícita
    if (resp.payload.auth.authorized) {
        // Se isEntering é true, newState = 1. Se é false, newState = 0.
        //
        int newState = isEntering ? 1 : 0;

        const char* sqlUpdate = "UPDATE Users SET IsInside = ? WHERE UserID = ?;";
        if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, newState);
            sqlite3_bind_int(stmt, 2, (int)resp.payload.auth.userId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // 3. Responder à thread que fez o pedido
    // Importante: Cada thread deve ter a sua própria queue de resposta para não haver misturas
    if (isEntering) {
        m_mqToVerifyRoom.send(&resp, sizeof(resp));
    } else {
        m_mqToLeaveRoom.send(&resp, sizeof(resp));
    }
}


void dDatabase::handleInsertLog(const DatabaseLog& log) {
    sqlite3_stmt* stmt;

    // 1. O SQL com placeholders (?) para segurança
    /*
    const char* sql = "INSERT INTO Logs (LogType, EntityID, Value, Timestamp, Description) "
                      "VALUES (?, ?, ?, ?, ?);";
*/
    const char* sql = "INSERT INTO Logs (LogType, EntityID, Value, Value2, Timestamp, Description) "
                  "VALUES (?, ?, ?, ?, ?, ?);";
    // 2. Preparar a query
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {

        // 3. Vincular os dados da struct DatabaseLog aos pontos de interrogação
        // LogType: LOG_TYPE_ACCESS (1) ou LOG_TYPE_ALERT (2)
        sqlite3_bind_int(stmt, 1, static_cast<int>(log.logType));

        // EntityID: ID do utilizador ou do atuador
        sqlite3_bind_int(stmt, 2, static_cast<int>(log.entityID));

        // Value: 1 para Sucesso/Ligado, 0 para Falha/Desligado
        sqlite3_bind_int(stmt, 3, static_cast<int>(log.value));

        sqlite3_bind_int(stmt, 4, static_cast<int>(log.value2));     // ← ADICIONAR!


        // Timestamp: Tempo Unix enviado pela thread
        sqlite3_bind_int(stmt, 4, static_cast<int>(log.timestamp));

        // Description: O texto gerado pela generateDescription()
        sqlite3_bind_text(stmt, 5, log.description, -1, SQLITE_STATIC);

        // 4. Executar a inserção no ficheiro .db
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "[Erro dDatabase] Falha ao gravar log: "
                      << sqlite3_errmsg(m_db) << std::endl;
        } else {
            std::cout << "[dDatabase] Novo log registado com sucesso." << std::endl;
        }

        // 5. Limpar o statement para libertar memória
        sqlite3_finalize(stmt);
    } else {
        std::cerr << "[Erro dDatabase] Erro no prepare do SQL: "
                  << sqlite3_errmsg(m_db) << std::endl;
    }

    if (log.logType == LOG_TYPE_SENSOR) {
        updateSensorTable(log.entityID, log.value, log.value2);
    }
    else if (log.logType == LOG_TYPE_ACTUATOR) {
        updateActuatorTable(log.entityID, log.value);
    }

}


void dDatabase::updateSensorTable(uint8_t entityID, uint16_t value, uint16_t value2) {
    if (entityID == ID_SHT31) {
        sqlite3_stmt* stmt;

        const char* sqlTemp = "UPDATE Sensors SET Value = ? WHERE Type = 'TEMPERATURE';";
        if (sqlite3_prepare_v2(m_db, sqlTemp, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(stmt, 1, (double)value);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        const char* sqlHum = "UPDATE Sensors SET Value = ? WHERE Type = 'HUMIDITY';";
        if (sqlite3_prepare_v2(m_db, sqlHum, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(stmt, 1, (double)value2);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
}

void dDatabase::updateActuatorTable(uint8_t entityID, uint16_t value) {
    sqlite3_stmt* stmt;
    const char* type = nullptr;

    switch(entityID) {
        case ID_SERVO_ROOM:  type = "SERVO_ROOM";  break;
        case ID_SERVO_VAULT: type = "SERVO_VAULT"; break;
        case ID_FAN:         type = "FAN";         break;
        case ID_ALARM_ACTUATOR: type = "ALARM";    break;
        default: return;
    }

    const char* sql = "UPDATE Actuators SET State = ? WHERE Type = ?;";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, value > 0 ? 1 : 0);
        sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}


void dDatabase::handleCheckUserInPir() {
    sqlite3_stmt* stmt;

    AuthResponse resp = {};

    resp.command = DB_CMD_USER_IN_PIR;
    // SQL: Conta quantos utilizadores têm IsInside = 1
    const char* sql = "SELECT COUNT(*) FROM Users WHERE IsInside = 1;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            if (count > 0) {
                resp.payload.auth.authorized = true; // Há pessoas na sala
            }
        }
        sqlite3_finalize(stmt);
    }

    // RESPONDER À THREAD (Usamos a fila m_mqCheckMovement)
    // Nota: Como o m_mqCheckMovement já está aberto na dDatabase, enviamos o resultado
    m_mqToCheckMovement.send(&resp, sizeof(resp));
}

void dDatabase:: handleScanInventory(const Data_RFID_Inventory& inventory) {
    sqlite3_stmt* stmt;
    uint32_t now = static_cast<uint32_t>(time(nullptr));

    // 1. Marcar todos os ativos como 'OUT'
    // Se um objeto não aparecer no scan, assume-se que saiu do cofre.
    const char* sqlReset = "UPDATE Assets SET State = 'OUT';";
    int rc = sqlite3_exec(m_db, sqlReset, nullptr, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        std::cerr << "[DB] Erro ao resetar estados: " << sqlite3_errmsg(m_db) << std::endl;
        return;
    }

    // 2. Preparar a query de atualização (usamos placeholders '?' por performance)
    const char* sqlUpdate = "UPDATE Assets SET State = 'IN', LastRead = ? WHERE RFID_Tag = ?;";

    if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &stmt, nullptr) == SQLITE_OK) {

        // Loop 1 a 1 pelas tags detetadas pela thread C_tInventoryScan
        for (int i = 0; i < inventory.tagCount; ++i) {

            // Vincular o timestamp e a Tag ID atual
            sqlite3_bind_int(stmt, 1, now);
            sqlite3_bind_text(stmt, 2, inventory.tagList[i], -1, SQLITE_STATIC);

            // Executar a atualização para este item específico
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cerr << "[DB] Erro ao atualizar Tag: " << inventory.tagList[i] << std::endl;
            }
            if (sqlite3_changes(m_db) == 0) {
                const char* sqlInsert = "INSERT INTO Assets (Name, RFID_Tag, State, LastRead) VALUES ('Item Desconhecido', ?, 'IN', ?);";
                sqlite3_stmt* stmtIns;

                if (sqlite3_prepare_v2(m_db, sqlInsert, -1, &stmtIns, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmtIns, 1, inventory.tagList[i], -1, SQLITE_STATIC);
                    sqlite3_bind_int(stmtIns, 2, now);
                    sqlite3_step(stmtIns);
                    sqlite3_finalize(stmtIns);
                    std::cout << "[DB] Novo ativo detetado e registado: " << inventory.tagList[i] << std::endl;
                }
            }

            // Limpar os binds para a próxima volta do loop
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

    // ✅ ADICIONAR "_viewer" ao nome automaticamente
    char fullName[70];
    snprintf(fullName, sizeof(fullName), "%s_viewer", login.username);

    // Só procura users COM password (viewers)
    const char* sql = "SELECT UserID, Name, AccessLevel, Password FROM Users "
                      "WHERE Name = ? AND Password IS NOT NULL;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_STATIC);  // ← Mudou aqui

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* storedHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            // Verify password with Argon2
            if (argon2i_verify(storedHash, login.password, strlen(login.password)) == ARGON2_OK) {
                result["userId"] = sqlite3_column_int(stmt, 0);

                // ✅ REMOVER "_viewer" ao mostrar ao user
                const char* dbName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                std::string displayName(dbName);
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

    if (resp.success) {
        std::string json = result.dump();
        strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
        resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetDashboard() {
    nlohmann::json response;
    sqlite3_stmt* stmt;

    // Security status
    const char* sql = "SELECT Description FROM Logs WHERE LogType = ? ORDER BY Timestamp DESC LIMIT 1;";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, LOG_TYPE_ALERT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["security"] = "ALERT";
        } else {
            response["security"] = "Secure";
        }
        sqlite3_finalize(stmt);
    }

    // Vault status
    sql = "SELECT State FROM Actuators WHERE Type = 'SERVO_VAULT';";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["vault"] = sqlite3_column_int(stmt, 0) > 0 ? "Locked" : "Unlocked";
        } else {
            response["vault"] = "Locked";
        }
        sqlite3_finalize(stmt);
    }

    // Temperature
    sql = "SELECT Value FROM Sensors WHERE Type = 'TEMPERATURE';";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["temp"] = sqlite3_column_double(stmt, 0);
        } else {
            response["temp"] = 22.5;
        }
        sqlite3_finalize(stmt);
    }

    // Humidity
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
    resp.success = true;
    std::string json = response.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetSensors() {
    nlohmann::json response;
    sqlite3_stmt* stmt;

    // Environment (Temperature and Humidity)
    const char* sqlTemp = "SELECT Value FROM Sensors WHERE Type = 'TEMPERATURE';";
    if (sqlite3_prepare_v2(m_db, sqlTemp, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["env"]["temp"] = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    const char* sqlHum = "SELECT Value FROM Sensors WHERE Type = 'HUMIDITY';";
    if (sqlite3_prepare_v2(m_db, sqlHum, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["env"]["hum"] = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // Vault (last fingerprint access)
    const char* sqlVault = "SELECT Description, Timestamp FROM Logs WHERE LogType = ? AND EntityID != 0 ORDER BY Timestamp DESC LIMIT 1;";
    if (sqlite3_prepare_v2(m_db, sqlVault, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, LOG_TYPE_ACCESS);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            // Extract user from description
            response["vault"]["user"] = "User";

            // Convert timestamp to time string
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

    // Vault door state
    const char* sqlVaultDoor = "SELECT State FROM Actuators WHERE Type = 'SERVO_VAULT';";
    if (sqlite3_prepare_v2(m_db, sqlVaultDoor, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["vault"]["door"] = sqlite3_column_int(stmt, 0) > 0 ? "Locked" : "Unlocked";
        }
        sqlite3_finalize(stmt);
    }

    // Room (who is inside)
    const char* sqlRoom = "SELECT Name FROM Users WHERE IsInside = 1 LIMIT 1;";
    if (sqlite3_prepare_v2(m_db, sqlRoom, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["room"]["rfidIn"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        } else {
            response["room"]["rfidIn"] = "--";
        }
        sqlite3_finalize(stmt);
    }

    // Room last entry time
    const char* sqlRoomTime = "SELECT Timestamp FROM Logs WHERE LogType = ? ORDER BY Timestamp DESC LIMIT 1;";
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

    // Room door state
    const char* sqlRoomDoor = "SELECT State FROM Actuators WHERE Type = 'SERVO_ROOM';";
    if (sqlite3_prepare_v2(m_db, sqlRoomDoor, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            response["room"]["door"] = sqlite3_column_int(stmt, 0) > 0 ? "Closed" : "Open";
        }
        sqlite3_finalize(stmt);
    }

    response["room"]["rfidOut"] = "--";
    response["room"]["timeOut"] = "--";
    response["uhf"] = "Idle";

    DbWebResponse resp = {};
    resp.success = true;
    std::string json = response.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetActuators() {
    nlohmann::json response;
    sqlite3_stmt* stmt;

    // Get all actuator states
    const char* sql = "SELECT Type, State FROM Actuators;";
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int state = sqlite3_column_int(stmt, 1);

            if (type == "FAN") {
                response["fan"]["state"] = state ? "ON" : "OFF";
                response["fan"]["timestamp"] = "10:00";
            } else if (type == "ALARM") {
                response["buzzer"]["state"] = state ? "ACTIVE" : "INACTIVE";
                response["buzzer"]["timestamp"] = "--";
                response["led"]["state"] = state ? "ON" : "OFF";
                response["led"]["timestamp"] = "--";
            } else if (type == "SERVO_VAULT") {
                response["vaultDoor"]["state"] = state ? "LOCKED" : "UNLOCKED";
                response["vaultDoor"]["timestamp"] = "10:05";
            } else if (type == "SERVO_ROOM") {
                response["roomDoor"]["state"] = state ? "LOCKED" : "UNLOCKED";
                response["roomDoor"]["timestamp"] = "09:00";
            }
        }
        sqlite3_finalize(stmt);
    }

    DbWebResponse resp = {};
    resp.success = true;
    std::string json = response.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);
    resp.jsonData[sizeof(resp.jsonData) - 1] = '\0';

    m_mqToWeb.send(&resp, sizeof(resp));
}


void dDatabase::handleRegisterUser(const UserData& user) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    // ✅ PASSO 1: Adicionar "_viewer" ao nome
    char fullName[70];
    snprintf(fullName, sizeof(fullName), "%s_viewer", user.name);

    // ✅ PASSO 2: VERIFICAR se já existe antes de criar
    const char* checkSql = "SELECT COUNT(*) FROM Users WHERE Name = ?;";
    int count = 0;

    if (sqlite3_prepare_v2(m_db, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // Se já existe, rejeitar
    if (count > 0) {
        resp.success = false;
        strcpy(resp.errorMsg, "Username already taken");
        m_mqToWeb.send(&resp, sizeof(resp));
        return;  // ← IMPORTANTE: Sair aqui!
    }

    // ✅ PASSO 3: Se não existe, criar normalmente
    // Hash da password
    char hash[128];
    uint8_t salt[16];
    srand(time(nullptr) + rand());
    for (int i = 0; i < 16; i++) salt[i] = rand() % 256;

    argon2id_hash_encoded(2, 16384, 1, user.password, strlen(user.password),
                         salt, sizeof(salt), 32, hash, sizeof(hash));

    // Inserir na BD
    const char* sql = "INSERT INTO Users (Name, Password, AccessLevel) VALUES (?, ?, 0);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fullName, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strcpy(resp.jsonData, R"({"status":"ok"})");
        } else {
            resp.success = false;
            strcpy(resp.errorMsg, "Database error");
        }
        sqlite3_finalize(stmt);
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetUsers() {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};
    nlohmann::json users = nlohmann::json::array();

    const char* sql = "SELECT UserID, Name, RFID_Card, FingerprintID, AccessLevel "
                          "FROM Users "
                          "WHERE (RFID_Card IS NOT NULL OR FingerprintID IS NOT NULL);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            nlohmann::json user;
            user["id"] = sqlite3_column_int(stmt, 0);
            user["name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

            const char* rfid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            user["rfid"] = rfid ? rfid : "";

            user["fingerprint"] = sqlite3_column_int(stmt, 3);

            int level = sqlite3_column_int(stmt, 4);
            if (level == 0) user["access"] = "Viewer";
            else if (level == 1) user["access"] = "Room";
            else user["access"] = "Room/Vault";

            user["lastLogin"] = "N/A";  // Podes melhorar isto depois
            users.push_back(user);
        }
        sqlite3_finalize(stmt);
    }

    resp.success = true;
    std::string json = users.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleCreateUser(const UserData& user) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    // Converter AccessLevel
    int level = 1;  // Default Room
    if (strcmp(user.password, "Room/Vault") == 0) level = 2;

    const char* sql =
        "INSERT INTO Users (Name, RFID_Card, FingerprintID, AccessLevel) "
        "VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, user.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, user.rfid, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, user.fingerprintID);
        sqlite3_bind_int(stmt, 4, level);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strcpy(resp.jsonData, "{\"status\":\"ok\"}");

            // Se tem fingerprint, avisar a thread!
            if (user.fingerprintID > 0) {
                AuthResponse cmd = {};
                cmd.command = DB_CMD_ADD_USER;
                cmd.payload.auth.userId = user.fingerprintID;
                m_mqToFingerprint.send(&cmd, sizeof(cmd));
            }
        } else {
            resp.success = false;
            strcpy(resp.errorMsg, "Failed to create user");
        }
        sqlite3_finalize(stmt);
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleModifyUser(const UserData& user) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    int level = 1;
    if (strcmp(user.password, "Room/Vault") == 0) level = 2;

    const char* sql =
        "UPDATE Users SET Name=?, RFID_Card=?, FingerprintID=?, AccessLevel=? "
        "WHERE UserID=?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, user.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, user.rfid, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, user.fingerprintID);
        sqlite3_bind_int(stmt, 4, level);
        sqlite3_bind_int(stmt, 5, user.fingerprintID);  // ID é o mesmo!

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strcpy(resp.jsonData, "{\"status\":\"ok\"}");
        }
        sqlite3_finalize(stmt);
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleRemoveUser(int userId) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    const char* sql = "DELETE FROM Users WHERE UserID = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, userId);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strcpy(resp.jsonData, "{\"status\":\"ok\"}");

            // Avisar thread para apagar fingerprint físico
            AuthResponse cmd = {};
            cmd.command = DB_CMD_DELETE_USER;
            cmd.payload.auth.userId = userId;
            m_mqToFingerprint.send(&cmd, sizeof(cmd));
        }
        sqlite3_finalize(stmt);
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleGetAssets() {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};
    nlohmann::json assets = nlohmann::json::array();

    const char* sql = "SELECT Name, RFID_Tag, State, LastRead FROM Assets;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            nlohmann::json asset;
            asset["name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            asset["tag"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            asset["state"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            time_t ts = sqlite3_column_int(stmt, 3);
            struct tm* timeinfo = localtime(&ts);
            char buffer[20];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
            asset["lastSeen"] = buffer;

            assets.push_back(asset);
        }
        sqlite3_finalize(stmt);
    }

    resp.success = true;
    std::string json = assets.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleCreateAsset(const AssetData& asset) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    const char* sql =
        "INSERT INTO Assets (Name, RFID_Tag, State, LastRead) "
        "VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, asset.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, asset.tag, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, asset.state, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 4, (int)time(nullptr));

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strcpy(resp.jsonData, "{\"status\":\"ok\"}");
        }
        sqlite3_finalize(stmt);
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleModifyAsset(const AssetData& asset) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    const char* sql =
        "UPDATE Assets SET Name=?, State=? WHERE RFID_Tag=?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, asset.name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, asset.state, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, asset.tag, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strcpy(resp.jsonData, "{\"status\":\"ok\"}");
        }
        sqlite3_finalize(stmt);
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleRemoveAsset(const char* tag) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};

    const char* sql = "DELETE FROM Assets WHERE RFID_Tag = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, tag, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            resp.success = true;
            strcpy(resp.jsonData, "{\"status\":\"ok\"}");
        }
        sqlite3_finalize(stmt);
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
            settings["sampleTime"] = sqlite3_column_int(stmt, 1) / 60;  // Converter para minutos
        }
        sqlite3_finalize(stmt);
    }

    resp.success = true;
    std::string json = settings.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);

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
            strcpy(resp.jsonData, "{\"status\":\"saved\"}");

            // Avisar thread de temperatura!
            // CORRETO (envia 1 comando com ambos):
            AuthResponse cmd = {};
            cmd.command = DB_CMD_UPDATE_SETTINGS;
            cmd.payload.settings.tempThreshold = settings.tempThreshold;
            cmd.payload.settings.samplingInterval = settings.samplingInterval;
            m_mqToEnvThread.send(&cmd, sizeof(cmd));
        }
        sqlite3_finalize(stmt);
    }

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleFilterLogs(const LogFilter& filter) {
    sqlite3_stmt* stmt;
    DbWebResponse resp = {};
    nlohmann::json result;

    // Calcular timestamp baseado no filtro
    time_t now = time(nullptr);
    time_t startTime = now;

    if (strcmp(filter.timeRange, "1 Hour") == 0) startTime -= 3600;
    else if (strcmp(filter.timeRange, "1 Day") == 0) startTime -= 86400;
    else if (strcmp(filter.timeRange, "1 Week") == 0) startTime -= 604800;

    // Se for Temperature ou Humidity -> Gráfico
    if (strcmp(filter.logType, "Temperature") == 0 ||
        strcmp(filter.logType, "Humidity") == 0) {

        int sensorType = strcmp(filter.logType, "Temperature") == 0 ? 0 : 1;
        const char* field = strcmp(filter.logType, "Temperature") == 0 ? "Value" : "Value2";

        std::string colName = (strcmp(filter.logType, "Temperature") == 0) ? "Value" : "Value2";

        // 2. Montar a query injetando o nome da coluna diretamente na string
        std::string sqlStr = "SELECT Timestamp, " + colName +
                             " FROM Logs WHERE LogType = ? AND EntityID = 0 AND Timestamp > ? "
                             " ORDER BY Timestamp ASC LIMIT 50;"; // Mudei para ASC para o gráfico fazer sentido (esquerda -> direita)

        if (sqlite3_prepare_v2(m_db, sqlStr.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            // Agora só fazemos bind de VALORES (o LogType e o Tempo)
            sqlite3_bind_int(stmt, 1, LOG_TYPE_SENSOR);
            sqlite3_bind_int(stmt, 2, (int)startTime);

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
    // Outros tipos -> Lista
    else {
        int logTypeFilter = LOG_TYPE_SYSTEM;
        if (strcmp(filter.logType, "Intrusion") == 0) logTypeFilter = LOG_TYPE_ALERT;
        else if (strcmp(filter.logType, "Access") == 0) logTypeFilter = LOG_TYPE_ACCESS;

        const char* sql =
            "SELECT Timestamp, LogType, Description FROM Logs "
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

                log["desc"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

                logs.push_back(log);
            }
            sqlite3_finalize(stmt);

            result["logs"] = logs;
        }
    }

    resp.success = true;
    std::string json = result.dump();
    strncpy(resp.jsonData, json.c_str(), sizeof(resp.jsonData) - 1);

    m_mqToWeb.send(&resp, sizeof(resp));
}

void dDatabase::handleAddFingerprint(int userId) {
    // Simplesmente envia para a thread do cofre
    AuthResponse cmd = {};
    cmd.command = DB_CMD_ADD_USER;
    cmd.payload.auth.userId = userId;
    m_mqToFingerprint.send(&cmd, sizeof(cmd));
}

void dDatabase::handleDeleteFingerprint(int userId) {
    AuthResponse cmd = {};
    cmd.command = DB_CMD_DELETE_USER;
    cmd.payload.auth.userId = userId;
    m_mqToFingerprint.send(&cmd, sizeof(cmd));
}