#include "dDatabase.h"


dDatabase::dDatabase(const std::string& dbPath,
                   C_Mqueue& mqDb,
                   C_Mqueue& mqRfidIn,C_Mqueue& mqRfidOut,
                   C_Mqueue& mqFinger,C_Mqueue& m_mqToCheckMovement)
    : m_db(nullptr),
      m_dbPath(dbPath),
      m_mqToDatabase(mqDb),      // <--- A receber aqui!
      m_mqToVerifyRoom(mqRfidIn),
      m_mqToLeaveRoom(mqRfidOut),
      m_mqToFingerprint(mqFinger),
      m_mqToCheckMovement(m_mqToCheckMovement)
{
}

// O destruidor garante que, se te esqueceres, a base de dados fecha ao sair
dDatabase::~dDatabase() {
    close();
}

bool dDatabase::open() {
    // Tenta abrir o ficheiro. Se não existir, o SQLite cria-o automaticamente.
    // .c_str() converte a string do C++ para o formato que o SQLite (em C) entende.
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
        // 1. Users: ID automático, RFID único para buscas rápidas
        "CREATE TABLE IF NOT EXISTS Users ("
        "UserID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Name TEXT, "
        "RFID_Card TEXT UNIQUE, "
        "FingerprintID INTEGER UNIQUE, " // O ID que vem do sensor biométrico
        "AccessLevel INTEGER, "
        "IsInside INTEGER DEFAULT 0);"   // 0 = Fora, 1 = Dentro

        // 2. Logs: ID automático para manter a ordem dos eventos
        "CREATE TABLE IF NOT EXISTS Logs ("
        "LogsID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "EntityID INTEGER, "
        "Timestamp INTEGER, "
        "LogType INTEGER, "
        "Description TEXT,"
        "Value INTEGER);"


        // 3. Assets: ID automático, Tag única para o inventário
        "CREATE TABLE IF NOT EXISTS Assets ("
        "AssetID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Name TEXT, "
        "RFID_Tag TEXT UNIQUE, "
        "State TEXT, "
        "LastRead INTEGER);"

        // 4. Sensors: ID automático, procuramos pelo 'Type'
        "CREATE TABLE IF NOT EXISTS Sensors ("
        "SensorID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Type TEXT UNIQUE, "
        "Value REAL);"

        // 5. Actuators: ID automático, procuramos pelo 'Type'
        "CREATE TABLE IF NOT EXISTS Actuators ("
        "ActuatorID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "Type TEXT UNIQUE, "
        "State INTEGER);"

        // 6. MainSystem: Apenas uma linha de configuração
        "CREATE TABLE IF NOT EXISTS MainSystem ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
        "TempThreshold REAL, "
        "SamplingTime INTEGER);";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::cerr << "Erro ao criar as tabelas: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

void dDatabase::processDbMessage(const DatabaseMsg &msg) {
    switch (msg.command) {
        case DB_CMD_ENTER_ROOM_RFID:
            handleAccessRequest(msg.payload.rfid, true);
            break;
        case DB_CMD_LEAVE_ROOM_RFID:
            handleAccessRequest(msg.payload.rfid, false);
        case DB_CMD_WRITE_LOG:
            handleInsertLog(msg.payload.log);
            break;
        case DB_CMD_USER_IN_PIR:
            handleCheckUserInPir();
            break;

    }
}



void dDatabase::handleAccessRequest(const char* rfid, bool isEntering) {
    sqlite3_stmt* stmt;
    AuthResponse resp = {false, 0};

    // 1. A validação é igual para os dois (o cartão existe?)
    const char* sqlSelect = "SELECT UserID, AccessLevel FROM Users WHERE RFID_Card = ?;";
    if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            resp.authorized = true;
            resp.userId = (uint32_t)sqlite3_column_int(stmt, 0);
            resp.accessLevel = (uint32_t)sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }

    // 2. Se autorizado, atualizamos o estado de forma explícita
    if (resp.authorized) {
        // Se isEntering é true, newState = 1. Se é false, newState = 0.
        int newState = isEntering ? 1 : 0;

        const char* sqlUpdate = "UPDATE Users SET IsInside = ? WHERE UserID = ?;";
        if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, newState);
            sqlite3_bind_int(stmt, 2, (int)resp.userId);
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
    const char* sql = "INSERT INTO Logs (LogType, EntityID, Value, Timestamp, Description) "
                      "VALUES (?, ?, ?, ?, ?);";

    // 2. Preparar a query
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {

        // 3. Vincular os dados da struct DatabaseLog aos pontos de interrogação
        // LogType: LOG_TYPE_ACCESS (1) ou LOG_TYPE_ALERT (2)
        sqlite3_bind_int(stmt, 1, static_cast<int>(log.logType));

        // EntityID: ID do utilizador ou do atuador
        sqlite3_bind_int(stmt, 2, static_cast<int>(log.entityID));

        // Value: 1 para Sucesso/Ligado, 0 para Falha/Desligado
        sqlite3_bind_int(stmt, 3, static_cast<int>(log.value));

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
}


void dDatabase::handleCheckUserInPir() {
    sqlite3_stmt* stmt;
    AuthResponse resp = {false, 0}; // authorized = false por defeito

    // SQL: Conta quantos utilizadores têm IsInside = 1
    const char* sql = "SELECT COUNT(*) FROM Users WHERE IsInside = 1;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            if (count > 0) {
                resp.authorized = true; // Há pessoas na sala
            }
        }
        sqlite3_finalize(stmt);
    }

    // RESPONDER À THREAD (Usamos a fila m_mqCheckMovement)
    // Nota: Como o m_mqCheckMovement já está aberto na dDatabase, enviamos o resultado
    m_mqToCheckMovement.send(&resp, sizeof(resp));
}