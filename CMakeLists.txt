cmake_minimum_required(VERSION 3.10)
project(SecureAssetGuard C CXX)

set(CMAKE_CXX_STANDARD 17)

# Onde estão os headers (.h)
include_directories(
        src/core
        src/core/hal
        src/core/devices
        src/core/ipc
        src/core/threads
        src/daemons
)

# =========================================================
# 1. O CÉREBRO (SecureAssetCore)
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
        src/core/threads/C_tLeaveRoomAccess.h
        src/core/threads/C_tSighandler.cpp
        src/core/threads/C_tSighandler.h
        src/core/threads/C_tTestWorker.h
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
)

target_link_libraries(dDatabase
        pthread
        rt
        sqlcipher   # <--- CORRIGIDO (Usa a lib do SQLCipher)
        argon2      # Linka com libargon2.so
)

# =========================================================
# 3. DAEMON WEB SERVER
# =========================================================
add_executable(dWebServer
        src/daemons/web/main_web.cpp
        src/daemons/web/dWebServer.cpp
)

target_link_libraries(dWebServer
        pthread
        rt
        mongoose
        json-c
        ssl
        crypto
        argon2
)