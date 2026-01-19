#include "C_SecureAsset.h"
#include <iostream>
#include <cstdlib>

// ============================================
// SINGLETON INSTANCE
// ============================================
C_SecureAsset* C_SecureAsset::s_instance = nullptr;

// ============================================
// CONSTRUCTOR - Initialize ALL members
// ============================================
C_SecureAsset::C_SecureAsset()
    // HAL - GPIO
    : m_gpio_fingerprint_rst(PIN_FINGERPRINT_RST, OUT),
      m_gpio_yrm1001_enable(PIN_YRM1001_ENABLE, OUT),
      m_gpio_fan(PIN_FAN, OUT),
      m_gpio_alarm_led(PIN_ALARM_LED, OUT),
      m_gpio_alarm_buzzer(PIN_ALARM_BUZZER, OUT),

      // HAL - UART
      m_uart_rfid_entry(UART_RFID_ENTRY),
      m_uart_rfid_exit(UART_RFID_EXIT),
      m_uart_fingerprint(UART_FINGERPRINT),
      m_uart_yrm1001(UART_YRM1001),

      // HAL - I2C
      m_i2c_temp_sensor(I2C_BUS, SHT30_ADDR),

      // HAL - PWM
      m_pwm_servo_room(PWM_CHIP, PWM_CHANNEL_SERVO_ROOM),
      m_pwm_servo_vault(PWM_CHIP, PWM_CHANNEL_SERVO_VAULT),

      // SENSORS
      m_temp_sensor(m_i2c_temp_sensor),
      m_rfid_entry(m_uart_rfid_entry),
      m_rfid_exit(m_uart_rfid_exit),
      m_rfid_inventory(m_uart_yrm1001, m_gpio_yrm1001_enable),
      m_fingerprint(m_uart_fingerprint, m_gpio_fingerprint_rst),

      // ACTUATORS
      m_servo_room(ID_SERVO_ROOM, m_pwm_servo_room),
      m_servo_vault(ID_SERVO_VAULT, m_pwm_servo_vault),
      m_fan(m_gpio_fan),
      m_alarm(m_gpio_alarm_led, m_gpio_alarm_buzzer),

      // MESSAGE QUEUES
      m_mq_to_database("/mq_to_db", sizeof(DatabaseMsg), 20, true),
      m_mq_to_actuator("/mq_to_actuator", sizeof(ActuatorCmd), 20, true),
      m_mq_to_verify_room("/mq_rfid_in", sizeof(AuthResponse), 10, true),
      m_mq_to_leave_room("/mq_rfid_out", sizeof(AuthResponse), 10, true),
      m_mq_to_check_movement("/mq_move", sizeof(AuthResponse), 10, true),
      m_mq_to_vault("/mq_finger", sizeof(AuthResponse), 10, true),
      m_mq_to_env_sensor("/mq_db_to_env", sizeof(AuthResponse), 10, true),

      // MONITORS (default constructed)
      m_monitor_reed_room(),
      m_monitor_reed_vault(),
      m_monitor_pir(),
      m_monitor_fingerprint(),
      m_monitor_rfid_entry(),
      m_monitor_rfid_exit(),
      m_monitor_env_sensor()
{
    std::cout << "[SecureAsset] Construtor executado" << std::endl;

    // Initialize actuators list
    m_actuators_list.fill(nullptr);
}

// ============================================
// DESTRUCTOR
// ============================================
C_SecureAsset::~C_SecureAsset() {
    std::cout << "[SecureAsset] Destrutor executado" << std::endl;
}

// ============================================
// SINGLETON - Get Instance
// ============================================
C_SecureAsset* C_SecureAsset::getInstance() {
    if (s_instance == nullptr) {
        s_instance = new C_SecureAsset();
    }
    return s_instance;
}

// ============================================
// SINGLETON - Destroy Instance
// ============================================
void C_SecureAsset::destroyInstance() {
    if (s_instance != nullptr) {
        delete s_instance;
        s_instance = nullptr;
    }
}



// ============================================
// INITIALIZE SENSORS
// ============================================
bool C_SecureAsset::initSensors() {
    std::cout << "[SecureAsset] A inicializar Sensores..." << std::endl;

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

// ============================================
// INITIALIZE ACTUATORS
// ============================================
bool C_SecureAsset::initActuators() {
    std::cout << "[SecureAsset] A inicializar Atuadores..." << std::endl;

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

// ============================================
// INITIALIZE ACTUATORS LIST (for C_tAct)
// ============================================
void C_SecureAsset::initActuatorsList() {
    m_actuators_list.fill(nullptr);
    m_actuators_list[ID_SERVO_ROOM] = &m_servo_room;
    m_actuators_list[ID_SERVO_VAULT] = &m_servo_vault;
    m_actuators_list[ID_FAN] = &m_fan;
    m_actuators_list[ID_ALARM_ACTUATOR] = &m_alarm;

    std::cout << "[SecureAsset] Lista de atuadores configurada" << std::endl;
}

// ============================================
// CREATE AND START THREADS
// ============================================
void C_SecureAsset::createThreads() {
    std::cout << "[SecureAsset] A criar threads..." << std::endl;

    // Signal Handler (MUST be first - handles hardware interrupts) pila hu
    m_thread_sighandler = std::make_unique<C_tSighandler>(
        m_monitor_reed_room,
        m_monitor_reed_vault,
        m_monitor_pir,
        m_monitor_fingerprint,
        m_monitor_rfid_entry,
        m_monitor_rfid_exit
    );

    // Verify Room Access (RFID Entry)
    m_thread_verify_room = std::make_unique<C_tVerifyRoomAccess>(
        m_monitor_rfid_entry,
        m_monitor_reed_room,
        m_rfid_entry,
        m_mq_to_database,
        m_mq_to_verify_room,
        m_mq_to_actuator
    );

    // Leave Room Access (RFID Exit)
    m_thread_leave_room = std::make_unique<C_tLeaveRoomAccess>(
        m_monitor_rfid_exit,
        m_monitor_reed_room,
        m_rfid_exit,
        m_mq_to_database,
        m_mq_to_leave_room,
        m_mq_to_actuator
    );

    // Verify Vault Access (Fingerprint)
    m_thread_verify_vault = std::make_unique<c_tVerifyVaultAccess>(
        m_monitor_fingerprint,
        m_monitor_reed_vault,
        m_fingerprint,
        m_mq_to_database,
        m_mq_to_actuator,
        m_mq_to_vault
    );

    // Inventory Scan (RFID UHF)
    m_thread_inventory = std::make_unique<C_tInventoryScan>(
        m_monitor_reed_vault,
        m_rfid_inventory,
        m_mq_to_database
    );

    // Environmental Sensor (Temperature/Humidity)
    m_thread_env_sensor = std::make_unique<C_tReadEnvSensor>(
        m_monitor_env_sensor,
        m_temp_sensor,
        m_mq_to_actuator,
        m_mq_to_database,
        m_mq_to_env_sensor,
        SAMPLING_INTERVAL_DEFAULT,
        TEMP_THRESHOLD_DEFAULT
    );

    // Check Movement (PIR)
    m_thread_check_movement = std::make_unique<C_tCheckMovement>(
        m_mq_to_check_movement,
        m_mq_to_database,
        m_mq_to_actuator,
        m_monitor_pir
    );

    // Actuator Control Thread
    m_thread_actuator = std::make_unique<C_tAct>(
        m_mq_to_actuator,
        m_mq_to_database,
        m_actuators_list
    );

    std::cout << "[SecureAsset] Threads criadas com sucesso" << std::endl;
}

// ============================================
// MAIN INITIALIZATION
// ============================================
bool C_SecureAsset::init() {
    std::cout << "============================================" << std::endl;
    std::cout << "    SECURE ASSET GUARD - INITIALIZATION" << std::endl;
    std::cout << "============================================" << std::endl;

    // CRITICAL: Block signals BEFORE creating threads
    C_tSighandler::setupSignalBlock();
    std::cout << "[SecureAsset] Sinais bloqueados (herança para threads)" << std::endl;


    // Step 1: Initialize Sensors
    if (!initSensors()) {
        std::cerr << "[ERRO CRÍTICO] Inicialização dos sensores falhou!" << std::endl;
        return false;
    }

    // Step 2: Initialize Actuators
    if (!initActuators()) {
        std::cerr << "[ERRO CRÍTICO] Inicialização dos atuadores falhou!" << std::endl;
        return false;
    }

    // Step 3: Setup Actuators List
    initActuatorsList();

    // Step 4: Create Threads (but don't start yet)
    createThreads();

    std::cout << "============================================" << std::endl;
    std::cout << "    INITIALIZATION COMPLETE" << std::endl;
    std::cout << "============================================" << std::endl;

    return true;
}

// ============================================
// START ALL THREADS
// ============================================
void C_SecureAsset::start() {
    std::cout << "[SecureAsset] A iniciar threads..." << std::endl;

    // Start in order of priority
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

// ============================================
// STOP ALL THREADS
// ============================================
void C_SecureAsset::stop() {
    if (m_thread_check_movement) m_thread_check_movement->requestStop();
    if (m_thread_env_sensor) m_thread_env_sensor->requestStop();
    if (m_thread_inventory) m_thread_inventory->requestStop();
    if (m_thread_verify_vault) m_thread_verify_vault->requestStop();
    if (m_thread_leave_room) m_thread_leave_room->requestStop();
    if (m_thread_verify_room) m_thread_verify_room->requestStop();
    if (m_thread_actuator) m_thread_actuator->requestStop();
    if (m_thread_sighandler) m_thread_sighandler->requestStop();

    // Wake monitors to allow threads to exit promptly
    m_monitor_reed_room.signal();
    m_monitor_reed_vault.signal();
    m_monitor_pir.signal();
    m_monitor_fingerprint.signal();
    m_monitor_rfid_entry.signal();
    m_monitor_rfid_exit.signal();
    m_monitor_env_sensor.signal();
}

// ============================================
// WAIT FOR THREADS TO FINISH
// ============================================
void C_SecureAsset::waitForThreads() {
    std::cout << "[SecureAsset] A aguardar término das threads..." << std::endl;

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