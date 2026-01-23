cmake_minimum_required(VERSION 3.10)
project(SecureAssentGuard C CXX)

set(CMAKE_CXX_STANDARD 17)

include_directories(
        src
        src/core
        src/core/hal
        src/core/devices
        src/core/ipc
        src/core/threads
        src/daemons
        src/daemons/database
        src/daemons/web

)

set(SHARED_SOURCES
        src/core/ipc/C_Mqueue.cpp
)

add_executable(SecureAssetCore
        src/core/maincore.cpp
        src/core/C_SecureAsset.cpp

        src/core/hal/C_GPIO.cpp
        src/core/hal/C_I2C.cpp
        src/core/hal/C_PWM.cpp
        src/core/hal/C_UART.cpp

        src/core/devices/C_TH_SHT30.cpp
        src/core/devices/C_RDM6300.cpp
        src/core/devices/C_YRM1001.cpp
        src/core/devices/C_Fingerprint.cpp
        src/core/devices/C_ServoMG996R.cpp
        src/core/devices/C_Fan.cpp
        src/core/devices/C_alarmActuator.cpp

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
)

target_link_libraries(SecureAssetCore
        pthread
        rt
)

add_executable(dDatabase
        src/daemons/database/main_db.cpp
        src/daemons/database/dDatabase.cpp
        src/core/ipc/C_Mqueue.cpp
)

target_link_libraries(dDatabase
        pthread
        rt
        sqlcipher
        argon2
)

add_executable(dWebServer
        src/daemons/web/main_web.cpp
        src/daemons/web/dWebServer.cpp
        src/core/ipc/C_Mqueue.cpp
)

target_link_libraries(dWebServer
        pthread
        rt
        mongoose
        ssl
        crypto
)

add_executable(wrapper
        main.cpp
        src/core/ipc/C_Mqueue.cpp
)

target_link_libraries(wrapper
        pthread
        rt
)