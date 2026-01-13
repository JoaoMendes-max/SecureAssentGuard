#ifndef SHAREDTYPES_H
#define SHAREDTYPES_H

#include <cstdint>
//=============================================
//SENSOR SPECIFIC
//=============================================

//SENSOR IDS
enum SensorID_enum : uint8_t {
    ID_SHT31 = 0,
    ID_RDM6300 = 1,
    ID_YRM1001 = 2,
    ID_FINGERPRINT = 3,
    ID_SENSOR_COUNT = 4
};

struct Data_SHT31 {
    int temp;
    int hum;
};

struct Data_RFID_Single {
    char tagID[11];
};

struct Data_RFID_Inventory {
    int tagCount;
    char tagList[4][32];
};

struct Data_Fingerprint {
    bool authenticated;
    int userID;
};

union SensorData_Union {
    Data_SHT31 tempHum;
    Data_RFID_Single rfid_single;
    Data_RFID_Inventory rfid_inventory;
    Data_Fingerprint fingerprint;
};

struct SensorData {
    SensorID_enum type;
    SensorData_Union data;
};






// ============================================
// IDs of actuators
// ============================================
enum ActuatorID_enum : uint8_t {
    ID_SERVO_ROOM = 0,
    ID_SERVO_VAULT = 1,
    ID_FAN = 2,
    ID_ALARM_ACTUATOR = 3,
    ID_ACTUATOR_COUNT = 4
};




// ============================================
// commands for actuator mqueu
// ============================================
struct ActuatorCmd {
    ActuatorID_enum actuatorID;
    uint8_t value;
    // 0 = PWM OFF/LIVRE (Destrancar)
    // > 0 = Ângulo/PWM ON (Trancar)

    ActuatorCmd() : actuatorID(ID_SERVO_ROOM), value(0) {}
    ActuatorCmd(ActuatorID_enum id, uint8_t val)
        : actuatorID(id), value(val) {}
};





// ============================================
//  NAMES FOR LOGGING
// ============================================
inline constexpr const char* ACTUATOR_NAMES[] = {
    "SERVO_ROOM",
    "SERVO_VAULT",
    "FAN",
    "ALARM_ACTUATOR"
};

inline constexpr const char* SENSOR_NAMES[] = {
    "TEMP_HUMIDITY",
    "RFID_ENTRY",
    "RFID_INVENTORY",
    "FINGERPRINT",
    /*
    "PIR_MOTION",
    "REED_ROOM",
    "REED_VAULT"
    */
};




// ============================================
// thread priority
// ============================================
enum ThreadPriority_enum : int {
    PRIO_LOW        = 10,
    PRIO_MEDIUM   = 30,
    PRIO_HIGH      = 50
};





// ============================================
// Types of Logs
// ============================================
enum LogType_enum : uint8_t {
    LOG_TYPE_ACTUATOR = 0,
    LOG_TYPE_SENSOR = 1,
    LOG_TYPE_ACCESS = 2,
    LOG_TYPE_SYSTEM = 3,
    LOG_TYPE_ALERT = 4,
    LOG_TYPE_INVENTORY = 5
};


// ============================================
// DATABASE LOG
// ============================================
struct DatabaseLog {
    LogType_enum logType;
    uint8_t entityID;
    uint16_t value;
    uint16_t value2;     // Novo campo para humidade
    uint32_t timestamp;
    char description[48]{};

    DatabaseLog()
        : logType(LOG_TYPE_SYSTEM),
          entityID(0),
          value(0),
          value2(0),      // Inicializar o novo campo

          timestamp(0)
    {
        description[0] = '\0';
    }
};

/*
 * Estrutura para pedido de login via interface web
 * Contém as credenciais inseridas pelo utilizador no formulário web
 */
struct LoginRequest {
    char username[64];  // Nome de utilizador (máx 63 caracteres + terminador)
    char password[64];  // Password em texto claro (será hashada antes de comparar)
};


//databaseeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee shii
// Comandos apenas para as threads de Hardware
enum e_DbCommand {
    DB_CMD_ENTER_ROOM_RFID, // VerifyRoomAccess (Tabela Users)
    DB_CMD_LEAVE_ROOM_RFID,//
    DB_CMD_UPDATE_ASSET, // InventoryScan (Tabela Assets)
    DB_CMD_USER_IN_PIR,      // usado para ver se algum dos users esta inside
    DB_CMD_WRITE_LOG,         // Logs gerais: Vault, Movement, Actuators
    DB_CMD_LOGIN,             // Autenticação de utilizador web
    DB_CMD_GET_DASHBOARD,     // Obter dados para o dashboard
    DB_CMD_GET_SENSORS,       // Obter estado dos sensores
    DB_CMD_GET_ACTUATORS,      // Obter estado dos atuadores
    DB_CMD_ADD_USER, // dar add a user
    DB_CMD_DELETE_USER,
    DB_CMD_UPDATE_TEMP_THRESHOLD,
    DB_CMD_UPDATE_SAMPLING_TIME,
    DB_CMD_REGISTER_USER,          // Criar conta viewer
    DB_CMD_GET_USERS,              // Listar utilizadores
    DB_CMD_CREATE_USER,            // Admin cria user
    DB_CMD_MODIFY_USER,            // Editar user
    DB_CMD_REMOVE_USER,            // Apagar user
    DB_CMD_GET_ASSETS,             // Listar assets
    DB_CMD_CREATE_ASSET,           // Criar asset
    DB_CMD_MODIFY_ASSET,           // Editar asset
    DB_CMD_REMOVE_ASSET,           // Apagar asset
    DB_CMD_GET_SETTINGS,           // Ler settings
    DB_CMD_UPDATE_SETTINGS,        // Atualizar settings
    DB_CMD_FILTER_LOGS             // Filtrar logs
};

// ADICIONAR estrutura para USER management
struct UserData {
    int userID;         // ← NOVO (ID da BD)
    char name[64];
    char rfid[11];
    int fingerprintID;  // Mesmo que userID!
    int accessLevel;    // 0=Viewer, 1=Room, 2=Room+Vault
    char password[64];  // Só usado para login web
};

struct AssetData {
    char name[64];
    char tag[32];
    char state[16];  // "Inside" ou "Outside"
};

struct LogFilter {
    char timeRange[16];  // "1 Hour", "1 Day", "1 Week"
    char logType[32];    // "Temperature", "Intrusion", etc
};

struct SystemSettings {
    int tempThreshold;
    int samplingInterval; // em segundos
};



// ATUALIZAR DatabaseMsg union:
struct DatabaseMsg {
    e_DbCommand command;
    union {
        char rfid[11];
        DatabaseLog log;
        Data_RFID_Inventory rfidInventory;
        LoginRequest login;
        UserData user;        // ← NOVO
        AssetData asset;      // ← NOVO
        SystemSettings settings; // ← NOVO
        LogFilter logFilter;  // ← NOVO
        int userId;           // Para comandos simples (delete, etc)
    } payload;
};

//db response //possivelkmente vai ser mudada para struct
struct AuthResponse {
    e_DbCommand command;

    union {
        struct {
            bool authorized;
            uint8_t userId;
            uint32_t accessLevel;
        } auth;

        SystemSettings settings;
    } payload;
};


struct DbWebResponse {
    bool success;           // true se operação foi bem sucedida
    char jsonData[8192];    // Dados da resposta em formato JSON (8KB)
    char errorMsg[256];     // Mensagem de erro em caso de falha
};

#endif // SHAREDTYPES_H


