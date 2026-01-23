#ifndef C_SECUREASSET_H
#define C_SECUREASSET_H

#include <memory>
#include <array>


#include "C_GPIO.h"
#include "C_UART.h"
#include "C_I2C.h"
#include "C_PWM.h"


#include "C_TH_SHT30.h"
#include "C_RDM6300.h"
#include "C_YRM1001.h"
#include "C_Fingerprint.h"


#include "C_ServoMG996R.h"
#include "C_Fan.h"
#include "C_alarmActuator.h"


#include "C_Mqueue.h"
#include "C_Monitor.h"


#include "C_tSighandler.h"
#include "C_tVerifyRoomAccess.h"
#include "C_tLeaveRoomAccess.h"
#include "C_tVerifyVaultAccess.h"
#include "C_tInventoryScan.h"
#include "C_tReadEnvSensor.h"
#include "C_tCheckMovement.h"
#include "C_tAct.h"

#include "SharedTypes.h"


#define PIN_FINGERPRINT_RST  26
#define PIN_YRM1001_ENABLE   25
#define PIN_FAN              18
#define PIN_ALARM_LED        24
#define PIN_ALARM_BUZZER     23

#define UART_RFID_ENTRY      2
#define UART_RFID_EXIT       3
#define UART_FINGERPRINT     0
#define UART_YRM1001         4

#define I2C_BUS              1

#define PWM_CHIP             0
#define PWM_CHANNEL_SERVO_ROOM  0
#define PWM_CHANNEL_SERVO_VAULT 1

#define TEMP_THRESHOLD_DEFAULT   3
#define SAMPLING_INTERVAL_DEFAULT 60

class C_SecureAsset {
private:
    
    static C_SecureAsset* s_instance;
    
    C_SecureAsset();
    ~C_SecureAsset();

    C_SecureAsset(const C_SecureAsset&) = delete;
    C_SecureAsset& operator=(const C_SecureAsset&) = delete;
    C_SecureAsset(C_SecureAsset&&) = delete;
    C_SecureAsset& operator=(C_SecureAsset&&) = delete;

    C_GPIO m_gpio_fingerprint_rst;
    C_GPIO m_gpio_yrm1001_enable;
    C_GPIO m_gpio_fan;
    C_GPIO m_gpio_alarm_led;
    C_GPIO m_gpio_alarm_buzzer;

    C_UART m_uart_rfid_entry;
    C_UART m_uart_rfid_exit;
    C_UART m_uart_fingerprint;
    C_UART m_uart_yrm1001;

    C_I2C m_i2c_temp_sensor;

    C_PWM m_pwm_servo_room;
    C_PWM m_pwm_servo_vault;

    C_TH_SHT30 m_temp_sensor;
    C_RDM6300 m_rfid_entry;
    C_RDM6300 m_rfid_exit;
    C_YRM1001 m_rfid_inventory;
    C_Fingerprint m_fingerprint;

    C_ServoMG996R m_servo_room;
    C_ServoMG996R m_servo_vault;
    C_Fan m_fan;
    C_alarmActuator m_alarm;

    std::array<C_Actuator*, ID_ACTUATOR_COUNT> m_actuators_list;
    
    C_Mqueue m_mq_to_database;
    C_Mqueue m_mq_to_actuator;
    C_Mqueue m_mq_to_verify_room;
    C_Mqueue m_mq_to_leave_room;
    C_Mqueue m_mq_to_check_movement;
    C_Mqueue m_mq_to_vault;
    C_Mqueue m_mq_to_env_sensor;

    C_Monitor m_monitor_reed_room;
    C_Monitor m_monitor_reed_vault;
    C_Monitor m_monitor_pir;
    C_Monitor m_monitor_fingerprint;
    C_Monitor m_monitor_rfid_entry;
    C_Monitor m_monitor_rfid_exit;

    std::unique_ptr<C_tSighandler> m_thread_sighandler;
    std::unique_ptr<C_tVerifyRoomAccess> m_thread_verify_room;
    std::unique_ptr<C_tLeaveRoomAccess> m_thread_leave_room;
    std::unique_ptr<c_tVerifyVaultAccess> m_thread_verify_vault;
    std::unique_ptr<C_tInventoryScan> m_thread_inventory;
    std::unique_ptr<C_tReadEnvSensor> m_thread_env_sensor;
    std::unique_ptr<C_tCheckMovement> m_thread_check_movement;
    std::unique_ptr<C_tAct> m_thread_actuator;

    bool initSensors();
    bool initActuators();
    void initActuatorsList();
    void createThreads();

public:

    static C_SecureAsset* getInstance();
    static void destroyInstance();
    
    bool init();
    void start();
    void stop();
    void waitForThreads();
    void unregisterQueues();
};

#endif 
