# Architecture

The system uses a supervisor plus daemons model. A single launcher owns the shared IPC
resources and supervises three independent processes. A kernel module delivers sensor events
as Real-Time Signals.

## Processes

| Process | Binary | Responsibility |
|---------|--------|----------------|
| Launcher | wrapper | Creates message queues, starts and stops daemons, owns lifecycle. |
| Core | SecureAssetCore | Hardware orchestration and business logic, runs the worker threads. |
| Web | dWebServer | HTTP and REST API (Mongoose), bridge between browser and database. |
| Database | dDatabase | All persistence (SQLite, SQLCipher), authentication (Argon2id). |

The web and core processes never touch the database directly. They send requests to dDatabase
through message queues and wait for the response.

## Message queues

The launcher creates every queue at startup and is the only component that unlinks them.
Daemons only open queues that already exist.

| Queue | Message type | Purpose |
|-------|--------------|---------|
| /mq_to_db | DatabaseMsg | Requests to the database. |
| /mq_to_actuator | ActuatorCmd | Commands to the actuator thread. |
| /mq_rfid_in | AuthResponse | Room entry authentication result. |
| /mq_rfid_out | AuthResponse | Room exit authentication result. |
| /mq_move | AuthResponse | Movement check result. |
| /mq_finger | AuthResponse | Vault access result. |
| /mq_db_to_env | AuthResponse | Settings and responses to the environment thread. |
| /mq_db_to_web | DbWebResponse | Responses to the web daemon. |

All message formats are defined once in `src/core/SharedTypes.h`, using fixed-size structs and
unions so each queue has a deterministic message size.

## Core threads

The core runs worker threads at three priority levels. Each thread waits on a condition
variable (signalled by the kernel driver via Real-Time Signals) or on a message queue.

| Thread | Priority | Trigger | Action |
|--------|----------|---------|--------|
| tAct | high | mq_to_actuator | Drives servos, fan, buzzer and LED. |
| tVerifyRoomAccess | medium | RFID entry IRQ | Validates room entry, unlocks door or raises alarm. |
| tLeaveRoomAccess | medium | RFID exit IRQ | Registers room exit. |
| tVerifyVaultAccess | medium | Fingerprint IRQ | Validates vault access. |
| tCheckMovement | medium | PIR IRQ | Checks for an authorized presence, else alarms. |
| tReadEnvSensor | low | Periodic | Reads temperature/humidity, triggers fan, logs values. |
| tInventoryScan | low | Reed switch IRQ | Scans UHF inventory when the vault door closes. |
| tSighandler | n/a | Real-Time Signals | Maps kernel signals to the condition variables above. |

Thread objects are owned by the C_SecureAsset singleton through unique_ptr and are joined on
shutdown.

## Startup

1. The launcher installs signal handlers for SIGINT and SIGTERM.
2. It creates all message queues. If any creation fails, startup aborts.
3. It starts the daemons in order: dDatabase, then dWebServer, then SecureAssetCore.
4. For each daemon it performs a readiness handshake over a UNIX socket pair (NOTIFY_FD). The
   daemon daemonizes, opens its queues, initializes its subsystem, and only then writes its
   final PID. The launcher waits up to 5 seconds; a timeout or a reported failure aborts
   startup and stops the daemons already running.
5. The launcher then blocks in pause() until a termination signal arrives.

## Shutdown

On SIGINT or SIGTERM the launcher stops the daemons in reverse dependency order:
SecureAssetCore, then dWebServer, then dDatabase. Each daemon is sent SIGTERM and the launcher
waits on a second socket pair (SHUTDOWN_FD) for an acknowledgement or EOF, up to 5 seconds,
escalating to SIGKILL otherwise. Finally the launcher unlinks the message queues and removes
the daemon PID files, leaving no runtime artifacts for the next run.
