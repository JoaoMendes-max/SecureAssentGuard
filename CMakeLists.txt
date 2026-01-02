cmake_minimum_required(VERSION 3.10)
project(SecureAssetGuard LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================================================
# SYSROOT DO BUILDROOT
# =========================================================
set(SYSROOT "/home/mendes/buildroot/buildroot-2025.02.6/output/host/aarch64-buildroot-linux-gnu/sysroot")

add_compile_definitions(
        MG_ENABLE_HTTP=1
)

# ADICIONE ISSO ao seu CMakeLists.txt:
set(CMAKE_FIND_ROOT_PATH ${SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# =========================================================
# INCLUDES COMUNS AO PROJETO
# (target-based, para o CLion indexar corretamente)
# =========================================================
set(PROJECT_INCLUDES
        ${SYSROOT}/usr/include

        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/core/hal
        ${CMAKE_SOURCE_DIR}/src/core/devices
        ${CMAKE_SOURCE_DIR}/src/core/ipc
        ${CMAKE_SOURCE_DIR}/src/core/threads

        ${CMAKE_SOURCE_DIR}/src/daemons
        ${CMAKE_SOURCE_DIR}/src/daemons/database
        ${CMAKE_SOURCE_DIR}/src/daemons/web
)

# =========================================================
# 1. SECURE ASSET CORE
# =========================================================
add_executable(SecureAssetCore
        src/core/main.cpp
        src/core/C_SecureAsset.cpp

        # HAL
        src/core/hal/C_GPIO.cpp
        src/core/hal/C_I2C.cpp
        src/core/hal/C_PWM.cpp
        src/core/hal/C_UART.cpp

        # Devices
        src/core/devices/C_TH_SHT30.cpp
        src/core/devices/C_RDM6300.cpp
        src/core/devices/C_YRM1001.cpp
        src/core/devices/C_Fingerprint.cpp
        src/core/devices/C_ServoMG996R.cpp
        src/core/devices/C_Fan.cpp
        src/core/devices/C_alarmActuator.cpp

        # IPC & Threads
        src/core/ipc/C_Monitor.cpp
        src/core/ipc/C_Mqueue.cpp

        src/core/threads/C_Thread.cpp
        src/core/threads/C_tAct.cpp
        src/core/threads/C_tReadEnvSensor.cpp
        src/core/threads/C_tVerifyRoomAccess.cpp
        src/core/threads/C_tInventoryScan.cpp
        src/core/threads/C_tVerifyVaultAccess.cpp
        src/core/threads/C_tCheckMovement.cpp
        src/core/threads/C_tLeaveRoomAccess.cpp
        src/core/threads/C_tSighandler.cpp
        src/core/threads/C_tAlarmTimer.cpp
)

target_include_directories(SecureAssetCore PRIVATE
        ${PROJECT_INCLUDES}
)

target_link_libraries(SecureAssetCore
        pthread
        rt
)

# =========================================================
# 2. DAEMON DATABASE
# =========================================================
add_executable(dDatabase
        src/daemons/database/main_db.cpp
        src/daemons/database/dDatabase.cpp
        src/core/ipc/C_Mqueue.cpp
)

target_include_directories(dDatabase PRIVATE
        ${PROJECT_INCLUDES}
)

target_link_libraries(dDatabase
        pthread
        rt
        sqlcipher
        argon2
)

# =========================================================
# 3. DAEMON WEB SERVER
# =========================================================
add_executable(dWebServer
        src/daemons/web/main_web.cpp
        src/daemons/web/dWebServer.cpp
        src/core/ipc/C_Mqueue.cpp
)

target_include_directories(dWebServer PRIVATE
        ${PROJECT_INCLUDES}
)

target_link_libraries(dWebServer
        pthread
        rt
        mongoose
        ssl
        crypto
        argon2
)
