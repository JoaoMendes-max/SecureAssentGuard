/*
 * Core implementation: hardware initialization, thread creation,
 * and system lifecycle management.
 */

#include "C_SecureAsset.h"
#include <iostream>
#include <cstdlib>

C_SecureAsset* C_SecureAsset::s_instance = nullptr;


C_SecureAsset::C_SecureAsset()

    : m_gpio_fingerprint_rst(PIN_FINGERPRINT_RST, OUT),
      m_gpio_yrm1001_enable(PIN_YRM1001_ENABLE, OUT),
      m_gpio_fan(PIN_FAN, OUT),
      m_gpio_alarm_led(PIN_ALARM_LED, OUT),
      m_gpio_alarm_buzzer(PIN_ALARM_BUZZER, OUT),


      m_uart_rfid_entry(UART_RFID_ENTRY),
      m_uart_rfid_exit(UART_RFID_EXIT),
      m_uart_fingerprint(UART_FINGERPRINT),
      m_uart_yrm1001(UART_YRM1001),


      m_i2c_temp_sensor(I2C_BUS, SHT30_ADDR),


      m_pwm_servo_room(PWM_CHIP, PWM_CHANNEL_SERVO_ROOM),
      m_pwm_servo_vault(PWM_CHIP, PWM_CHANNEL_SERVO_VAULT),


      m_temp_sensor(m_i2c_temp_sensor),
      m_rfid_entry(m_uart_rfid_entry),
      m_rfid_exit(m_uart_rfid_exit),
      m_rfid_inventory(m_uart_yrm1001, m_gpio_yrm1001_enable),
      m_fingerprint(m_uart_fingerprint, m_gpio_fingerprint_rst),


      m_servo_room(ID_SERVO_ROOM, m_pwm_servo_room),
      m_servo_vault(ID_SERVO_VAULT, m_pwm_servo_vault),
      m_fan(m_gpio_fan),
      m_alarm(m_gpio_alarm_led, m_gpio_alarm_buzzer),


      m_mq_to_database("/mq_to_db", sizeof(DatabaseMsg), 20, false),
      m_mq_to_actuator("/mq_to_actuator", sizeof(ActuatorCmd), 20, false),
      m_mq_to_verify_room("/mq_rfid_in", sizeof(AuthResponse), 10, false),
      m_mq_to_leave_room("/mq_rfid_out", sizeof(AuthResponse), 10, false),
      m_mq_to_check_movement("/mq_move", sizeof(AuthResponse), 10, false),
      m_mq_to_vault("/mq_finger", sizeof(AuthResponse), 10, false),
      m_mq_to_env_sensor("/mq_db_to_env", sizeof(AuthResponse), 10, false),


      m_monitor_reed_room(),
      m_monitor_reed_vault(),
      m_monitor_pir(),
      m_monitor_fingerprint(),
      m_monitor_rfid_entry(),
      m_monitor_rfid_exit()
{
    std::cout << "[SecureAsset] Construtor executado" << std::endl;

    // Initialize actuator list with null pointers.
    m_actuators_list.fill(nullptr);
}

C_SecureAsset::~C_SecureAsset() {
    std::cout << "[SecureAsset] Destrutor executado" << std::endl;
}

C_SecureAsset* C_SecureAsset::getInstance() {
    if (s_instance == nullptr) {
        s_instance = new C_SecureAsset();
    }
    return s_instance;
}

void C_SecureAsset::destroyInstance() {
    if (s_instance == nullptr) {
        return;
    }
    delete s_instance;
    s_instance = nullptr;
}

bool C_SecureAsset::initSensors() {
    std::cout << "[SecureAsset] A inicializar Sensores..." << std::endl;

    // Required sensors; any failure aborts initialization.
    if (!m_temp_sensor.init()) {
        std::cerr << "[ERRO] Falha no init: Sensor Temperatura" << std::endl;
        return false;
    }

    if (!m_rfid_entry.init()) {
        std::cerr << "[ERRO] Falha no init: RFID Entry" << std::endl;
        return false;
    }

    if (!m_rfid_exit.init()) {
        std::cerr << "[ERRO] Falha no init: RFID Exit" << std::endl;
        return false;
    }

    if (!m_rfid_inventory.init()) {
        std::cerr << "[ERRO] Falha no init: RFID Inventory" << std::endl;
        return false;
    }

    if (!m_fingerprint.init()) {
        std::cerr << "[ERRO] Falha no init: Fingerprint" << std::endl;
        return false;
    }

    std::cout << "[SecureAsset] Sensores inicializados com sucesso" << std::endl;
    return true;
}

bool C_SecureAsset::initActuators() {
    std::cout << "[SecureAsset] A inicializar Atuadores..." << std::endl;

    // Required actuators; any failure aborts initialization.
    if (!m_servo_room.init()) {
        std::cerr << "[ERRO] Falha no init: Servo Room" << std::endl;
        return false;
    }

    if (!m_servo_vault.init()) {
        std::cerr << "[ERRO] Falha no init: Servo Vault" << std::endl;
        return false;
    }

    if (!m_fan.init()) {
        std::cerr << "[ERRO] Falha no init: Fan" << std::endl;
        return false;
    }

    if (!m_alarm.init()) {
        std::cerr << "[ERRO] Falha no init: Alarm" << std::endl;
        return false;
    }

    std::cout << "[SecureAsset] Atuadores inicializados com sucesso" << std::endl;
    return true;
}

void C_SecureAsset::initActuatorsList() {
    // Map actuator IDs to concrete instances.
    m_actuators_list.fill(nullptr);
    m_actuators_list[ID_SERVO_ROOM] = &m_servo_room;
    m_actuators_list[ID_SERVO_VAULT] = &m_servo_vault;
    m_actuators_list[ID_FAN] = &m_fan;
    m_actuators_list[ID_ALARM_ACTUATOR] = &m_alarm;

    std::cout << "[SecureAsset] Lista de atuadores configurada" << std::endl;
}




void C_SecureAsset::createThreads() {
    std::cout << "[SecureAsset] A criar threads..." << std::endl;

    // Thread dedicated to signals and monitors (sensor wakeups).
    m_thread_sighandler = std::make_unique<C_tSighandler>(
        m_monitor_reed_room,
        m_monitor_reed_vault,
        m_monitor_pir,
        m_monitor_fingerprint,
        m_monitor_rfid_entry,
        m_monitor_rfid_exit
    );

    // Verify room entry access via RFID.
    m_thread_verify_room = std::make_unique<C_tVerifyRoomAccess>(
        m_monitor_rfid_entry,
        m_monitor_reed_room,
        m_rfid_entry,
        m_mq_to_database,
        m_mq_to_verify_room,
        m_mq_to_actuator
    );

    // Verify room exit via RFID.
    m_thread_leave_room = std::make_unique<C_tLeaveRoomAccess>(
        m_monitor_rfid_exit,
        m_monitor_reed_room,
        m_rfid_exit,
        m_mq_to_database,
        m_mq_to_leave_room,
        m_mq_to_actuator
    );

    // Verify vault access via fingerprint.
    m_thread_verify_vault = std::make_unique<c_tVerifyVaultAccess>(
        m_monitor_fingerprint,
        m_monitor_reed_vault,
        m_fingerprint,
        m_mq_to_database,
        m_mq_to_actuator,
        m_mq_to_vault
    );

    // Inventory via RFID (YRM1001).
    m_thread_inventory = std::make_unique<C_tInventoryScan>(
        m_monitor_reed_vault,
        m_rfid_inventory,
        m_mq_to_database
    );

    // Environmental reading (temperature) and threshold notification.
    m_thread_env_sensor = std::make_unique<C_tReadEnvSensor>(
        m_temp_sensor,
        m_mq_to_actuator,
        m_mq_to_database,
        m_mq_to_env_sensor,
        SAMPLING_INTERVAL_DEFAULT,
        TEMP_THRESHOLD_DEFAULT
    );

    // Movement monitoring (PIR) and DB communication.
    m_thread_check_movement = std::make_unique<C_tCheckMovement>(
        m_mq_to_check_movement,
        m_mq_to_database,
        m_mq_to_actuator,
        m_monitor_pir
    );

    // Execute actuator commands received via queue.
    m_thread_actuator = std::make_unique<C_tAct>(
        m_mq_to_actuator,
        m_mq_to_database,
        m_actuators_list
    );

    std::cout << "[SecureAsset] Threads criadas com sucesso" << std::endl;
}




bool C_SecureAsset::init() {
    std::cout << "============================================" << std::endl;
    std::cout << "    SECURE ASSET GUARD - INITIALIZATION" << std::endl;
    std::cout << "============================================" << std::endl;

    // Block signals before creating threads so they inherit the mask.
    C_tSighandler::setupSignalBlock();
    std::cout << "[SecureAsset] Sinais bloqueados (herança para threads)" << std::endl;

    if (!initSensors()) {
        std::cerr << "[ERRO CRÍTICO] Inicialização dos sensores falhou!" << std::endl;
        return false;
    }

    // Order: sensors -> actuators -> wiring -> threads.
    if (!initActuators()) {
        std::cerr << "[ERRO CRÍTICO] Inicialização dos atuadores falhou!" << std::endl;
        return false;
    }
    
    initActuatorsList();
    createThreads();

    std::cout << "============================================" << std::endl;
    std::cout << "    INITIALIZATION COMPLETE" << std::endl;
    std::cout << "============================================" << std::endl;

    return true;
}

void C_SecureAsset::start() {
    std::cout << "[SecureAsset] A iniciar threads..." << std::endl;

    // Startup order ensures signal handler and actuation are ready.
    if (!m_thread_sighandler->start()) {
        std::cerr << "[ERRO] Falha ao iniciar Signal Handler!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (!m_thread_actuator->start()) {
        std::cerr << "[ERRO] Falha ao iniciar Actuator Thread!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (!m_thread_verify_room->start()) {
        std::cerr << "[ERRO] Falha ao iniciar Verify Room Thread!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (!m_thread_leave_room->start()) {
        std::cerr << "[ERRO] Falha ao iniciar Leave Room Thread!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (!m_thread_verify_vault->start()) {
        std::cerr << "[ERRO] Falha ao iniciar Verify Vault Thread!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (!m_thread_inventory->start()) {
        std::cerr << "[ERRO] Falha ao iniciar Inventory Thread!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (!m_thread_env_sensor->start()) {
        std::cerr << "[ERRO] Falha ao iniciar Environment Sensor Thread!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    if (!m_thread_check_movement->start()) {
        std::cerr << "[ERRO] Falha ao iniciar Check Movement Thread!" << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::cout << "[SecureAsset] Todas as threads iniciadas!" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "    SISTEMA OPERACIONAL" << std::endl;
    std::cout << "============================================" << std::endl;
}

void C_SecureAsset::stop() {
    // Stop requests in reverse order of the main flow.
    if (m_thread_check_movement) m_thread_check_movement->requestStop();
    if (m_thread_env_sensor) m_thread_env_sensor->requestStop();
    if (m_thread_inventory) m_thread_inventory->requestStop();
    if (m_thread_verify_vault) m_thread_verify_vault->requestStop();
    if (m_thread_leave_room) m_thread_leave_room->requestStop();
    if (m_thread_verify_room) m_thread_verify_room->requestStop();
    if (m_thread_actuator) m_thread_actuator->requestStop();
    if (m_thread_sighandler) m_thread_sighandler->requestStop();

    AuthResponse stopMsg = {};
    stopMsg.command = DB_CMD_STOP_ENV_SENSOR;
    // Special message to unblock the env thread (if waiting).
    m_mq_to_env_sensor.send(&stopMsg, sizeof(stopMsg));
    m_monitor_reed_room.signal();
    m_monitor_reed_vault.signal();
    m_monitor_pir.signal();
    m_monitor_fingerprint.signal();
    m_monitor_rfid_entry.signal();
    m_monitor_rfid_exit.signal();
}

void C_SecureAsset::waitForThreads() {
    std::cout << "[SecureAsset] A aguardar término das threads..." << std::endl;

    // Join all threads for clean shutdown.
    if (m_thread_sighandler) m_thread_sighandler->join();
    if (m_thread_actuator) m_thread_actuator->join();
    if (m_thread_verify_room) m_thread_verify_room->join();
    if (m_thread_leave_room) m_thread_leave_room->join();
    if (m_thread_verify_vault) m_thread_verify_vault->join();
    if (m_thread_inventory) m_thread_inventory->join();
    if (m_thread_env_sensor) m_thread_env_sensor->join();
    if (m_thread_check_movement) m_thread_check_movement->join();

    std::cout << "[SecureAsset] Todas as threads terminadas" << std::endl;
}

void C_SecureAsset::unregisterQueues() {
    // Request unlink of queues when the owner closes them.
    m_mq_to_database.unregister();
    m_mq_to_actuator.unregister();
    m_mq_to_verify_room.unregister();
    m_mq_to_leave_room.unregister();
    m_mq_to_check_movement.unregister();
    m_mq_to_vault.unregister();
    m_mq_to_env_sensor.unregister();
}
