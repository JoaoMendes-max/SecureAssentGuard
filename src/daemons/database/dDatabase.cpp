#include "dDatabase.h"


dDatabase::dDatabase(const std::string& dbPath,
                   C_Mqueue& mqDb,
                   C_Mqueue& mqRfid,
                   C_Mqueue& mqFinger)
    : m_db(nullptr),
      m_dbPath(dbPath),
      m_mqToDatabase(mqDb),      // <--- A receber aqui!
      m_mqToRoomRfid(mqRfid),
      m_mqToFingerprint(mqFinger)
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
        case DB_CMD_VERIFY_RFID:
            handleVerifyRFID(msg.payload.rfid);
            break;
        case DB_CMD_WRITE_LOG:
            handleInsertLog(msg.payload.log);
        default:
            break;

    }
}


void dDatabase::handleVerifyRFID(const char* rfid) {
    sqlite3_stmt* stmt;
    AuthResponse resp = {false, 0, 0, false};
    int currentPresence = 0;

    // 1. PROCURAR O UTILIZADOR NO SQLITE
    const char* sqlSelect = "SELECT UserID, AccessLevel, IsInside FROM Users WHERE RFID_Card = ?;";

    if (sqlite3_prepare_v2(m_db, sqlSelect, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, rfid, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            resp.authorized = true;
            resp.userId = (uint32_t)sqlite3_column_int(stmt, 0);
            resp.accessLevel = (uint32_t)sqlite3_column_int(stmt, 1);
            currentPresence = sqlite3_column_int(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }

    // 2. ATUALIZAR PRESENÇA (TOGGLE)
    if (resp.authorized) {
        int newPresence = (currentPresence == 0) ? 1 : 0;
        resp.isInside = (newPresence == 1);

        const char* sqlUpdate = "UPDATE Users SET IsInside = ? WHERE UserID = ?;";
        if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, newPresence);
            sqlite3_bind_int(stmt, 2, (int)resp.userId);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        std::cout << "[DB] RFID " << rfid << " (User " << resp.userId << ") -> "
                  << (newPresence ? "ENTROU" : "SAIU") << std::endl;
    }

    // 3. ENVIAR RESPOSTA À THREAD USANDO A TUA CLASSE C_Mqueue
    // Como a m_mqToRoomRfid é uma referência ao objeto já criado,
    // usamos o teu métod .send() que valida o tamanho internamente.
    m_mqToRoomRfid.send(&resp, sizeof(resp),0);
}


void dDatabase::handleInsertLog(const DatabaseLog &log) {

}
