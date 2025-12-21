#ifndef SHAREDTYPES_H
#define SHAREDTYPES_H

#include <cstdint>
// ============================================
// thread priority
// ============================================
enum ThreadPriority_enum : int {
    PRIO_LOW        = 10,
    PRIO_MEDIUM   = 30,
    PRIO_HIGH      = 50
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
// IDs of Sensors
// ============================================
enum SensorID_enum : uint8_t {
    ID_SHT31 = 0,
    ID_RDM6300 = 1,
    ID_YRM1001 = 2,
    ID_FINGERPRINT = 3,
    ID_SENSOR_COUNT = 4
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
// commands for actuator
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
// DATABASE LOG
// ============================================
struct DatabaseLog {
    LogType_enum logType;
    uint8_t entityID;
    uint16_t value;
    uint32_t timestamp;
    char description[48]{};

    DatabaseLog()
        : logType(LOG_TYPE_SYSTEM),
          entityID(0),
          value(0),
          timestamp(0)
    {
        description[0] = '\0';
    }
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
    "RFID_ENTR  Y",
    "RFID_INVENTORY",
    "FINGERPRINT",
    "PIR_MOTION",
    "REED_ROOM",
    "REED_VAULT"
};




//databaseeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee shii
// Comandos apenas para as threads de Hardware
enum e_DbCommand {
    DB_CMD_VERIFY_RFID,      // VerifyRoomAccess (Tabela Users)
    DB_CMD_UPDATE_ASSET,     // InventoryScan (Tabela Assets)
    DB_CMD_WRITE_LOG         // Logs gerais: Vault, Movement, Actuators
};


// Mensagem de Pedido (Entrada no Daemon)
struct DatabaseMsg {
    e_DbCommand command;
    char respQueueName[32];  // Onde a thread espera a resposta (se necessário)

    union {
        char rfid[16];       // Para validar User ou atualizar Asset
        DatabaseLog log;     // Para registo de histórico
    } payload;
};

// Mensagem de Resposta (Saída para as Threads)
struct DbResponse {
    bool authorized;         // Usado no RFID (Encontrado na BD?)
    uint32_t accessLevel;    // Permissão do utilizador
};



#endif // SHAREDTYPES_H


