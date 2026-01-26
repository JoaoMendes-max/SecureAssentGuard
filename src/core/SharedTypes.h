#ifndef SHAREDTYPES_H
#define SHAREDTYPES_H

#include <cstdint>

/*
 * Shared IPC types between core, daemons, and web layer.
 */

enum SensorID_enum : uint8_t {
    ID_SHT31 = 0,
    ID_RDM6300 = 1,
    ID_YRM1001 = 2,
    ID_FINGERPRINT = 3,
    ID_SENSOR_COUNT = 4
};

struct Data_SHT31 {
    float temp;
    float hum;
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

enum ActuatorID_enum : uint8_t {
    ID_SERVO_ROOM = 0,
    ID_SERVO_VAULT = 1,
    ID_FAN = 2,
    ID_ALARM_ACTUATOR = 3,
    ID_ACTUATOR_COUNT = 4
};

struct ActuatorCmd {
    ActuatorID_enum actuatorID;
    uint8_t value;

    // Default is room servo at 0.
    ActuatorCmd() : actuatorID(ID_SERVO_ROOM), value(0) {}
    ActuatorCmd(ActuatorID_enum id, uint8_t val)
        : actuatorID(id), value(val) {}
};

inline constexpr const char* ACTUATOR_NAMES[] = {
    "SERVO_ROOM",
    "SERVO_VAULT",
    "FAN",
    "ALARM_ACTUATOR"
};

enum ThreadPriority_enum : int {
    PRIO_LOW        = 10,
    PRIO_MEDIUM   = 30,
    PRIO_HIGH      = 50
};

enum LogType_enum : uint8_t {
    LOG_TYPE_ACTUATOR = 0,
    LOG_TYPE_SENSOR = 1,
    LOG_TYPE_ACCESS = 2,
    LOG_TYPE_SYSTEM = 3,
    LOG_TYPE_ALERT = 4,
    LOG_TYPE_INVENTORY = 5
};

struct DatabaseLog {
    LogType_enum logType;
    uint32_t entityID;
    double value;
    double value2;
    uint32_t timestamp;
    char description[266]{};
    DatabaseLog()
        : logType(LOG_TYPE_SYSTEM),
          entityID(0),
          value(0.0),
          value2(0.0),
          timestamp(0) {
        description[0] = '\0';
    }
};


struct LoginRequest {
    char username[64];  
    char password[64];  
};

enum e_DbCommand {
    DB_CMD_ENTER_ROOM_RFID, 
    DB_CMD_LEAVE_ROOM_RFID,
    DB_CMD_UPDATE_ASSET, 
    DB_CMD_USER_IN_PIR,      
    DB_CMD_WRITE_LOG,         
    DB_CMD_LOGIN,             
    DB_CMD_GET_DASHBOARD,     
    DB_CMD_GET_SENSORS,       
    DB_CMD_GET_ACTUATORS,      
    DB_CMD_ADD_USER, 
    DB_CMD_DELETE_USER,
    DB_CMD_UPDATE_TEMP_THRESHOLD,
    DB_CMD_UPDATE_SAMPLING_TIME,
    DB_CMD_REGISTER_USER,          
    DB_CMD_GET_USERS,              
    DB_CMD_CREATE_USER,            
    DB_CMD_MODIFY_USER,            
    DB_CMD_REMOVE_USER,            
    DB_CMD_GET_ASSETS,             
    DB_CMD_CREATE_ASSET,           
    DB_CMD_MODIFY_ASSET,           
    DB_CMD_REMOVE_ASSET,           
    DB_CMD_GET_SETTINGS,
    DB_CMD_GET_SETTINGS_THREAD,
    DB_CMD_UPDATE_SETTINGS,        
    DB_CMD_FILTER_LOGS,
    DB_CMD_STOP_ENV_SENSOR
};

struct UserData {
    int userID;         
    char name[64];
    char rfid[11];
    int fingerprintID;  
    int accessLevel;    
    char password[64];  
};

struct AssetData {
    char name[64];
    char tag[32];
};

struct LogFilter {
    char timeRange[16];  
    char logType[32];    
};

struct SystemSettings {
    int tempThreshold;
    int samplingInterval; 
};

struct DatabaseMsg {
    e_DbCommand command;
    union {
        char rfid[11];
        DatabaseLog log;
        Data_RFID_Inventory rfidInventory;
        LoginRequest login;
        UserData user;        
        AssetData asset;      
        SystemSettings settings; 
        LogFilter logFilter;  
        uint32_t userId;
    } payload;
};


struct AuthResponse {
    e_DbCommand command;

    union {
        struct {
            bool authorized;
            uint32_t userId;
            uint32_t accessLevel;
        } auth;

        SystemSettings settings;
    } payload;
};


struct DbWebResponse {
    bool success;
    char jsonData[8192];    
    char errorMsg[256];     
};

#endif 
