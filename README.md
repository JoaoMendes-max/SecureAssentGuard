# Secure Asset Guard

Embedded security system for vaults and protected rooms, running on a Raspberry Pi 4 with a
custom Buildroot Linux image. The user-space application is written in C++17 and is backed by
a custom Linux kernel module for interrupt-driven sensing.

It integrates multi-layer access control, intrusion detection, environmental monitoring and
asset inventory, all managed through a local web interface.

Academic project for the MSc in Industrial Electronics and Computers Engineering, University
of Minho. Authors: Joao Mendes and Tiago Oliveira.

## Features

- Access control: RFID (125 kHz) for room entry and exit, capacitive fingerprint for the vault.
- Intrusion detection: PIR motion sensor and reed switches, interrupt-driven from the kernel.
- Environmental monitoring: SHT30 temperature and humidity with automatic fan control.
- Inventory: UHF RFID (YRM1001) scan of vault assets when the vault door closes.
- Web interface: dashboard, logs, and user/asset management (HTML, CSS, JavaScript, REST API).
- Security at rest: SQLite with SQLCipher (AES-256) and Argon2id password hashing.

## Architecture

A launcher process owns the IPC resources (POSIX message queues) and supervises three
user-space daemons. A kernel module turns GPIO interrupts into Real-Time Signals, so the core
never has to poll the sensors.

```
   sensors (GPIO IRQ) --> driver/ my_irq.ko --> Real-Time Signals 43-48
                                  |
   wrapper (launcher) ------------+--- creates queues, supervises lifecycle
     |-- SecureAssetCore   singleton and worker threads: sensors, actuators, logic
     |-- dWebServer        Mongoose HTTP/REST API: bridges browser and database
     |-- dDatabase         SQLite, SQLCipher, Argon2id: all persistence
```

Components communicate exclusively through message queues. The launcher starts the daemons in
order using a readiness handshake and stops them gracefully (SIGTERM, then ACK or EOF, with a
SIGKILL fallback). The full design is described in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Repository structure

```
.
├── CMakeLists.txt          user-space build (4 binaries)
├── src/
│   ├── launcher/           supervisor process (main.cpp)
│   ├── core/               SecureAssetCore daemon
│   │   ├── hal/            GPIO, I2C, PWM, UART
│   │   ├── devices/        sensor and actuator drivers
│   │   ├── ipc/            message queue and monitor wrappers
│   │   ├── threads/        worker threads
│   │   └── SharedTypes.h   IPC message and struct definitions
│   └── daemons/
│       ├── database/       dDatabase daemon
│       └── web/            dWebServer daemon
├── driver/                 Linux kernel module (GPIO IRQ to Real-Time Signals)
├── web/                    static front-end served by dWebServer
└── docs/                   report, presentation, and reference documentation
```

## Build and run

User-space binaries are built with CMake and the kernel module with kbuild. Quick reference:

```bash
# user-space (wrapper, SecureAssetCore, dDatabase, dWebServer)
cmake -B build
cmake --build build

# kernel module (my_irq.ko)
cd driver
make BUILDROOT=/path/to/buildroot/output
```

On the target the launcher loads the driver, creates the message queues, starts the daemons
and waits for a termination signal:

```bash
./wrapper
```

The web interface listens on port 8080. Full instructions, dependencies and deployment layout
are in [docs/BUILD.md](docs/BUILD.md).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): processes, threads, IPC, startup and shutdown.
- [docs/HARDWARE.md](docs/HARDWARE.md): GPIO pinout, components, power, IRQ signal map.
- [docs/BUILD.md](docs/BUILD.md): dependencies, build steps, and target deployment.
- [docs/API.md](docs/API.md): REST API endpoints and session model.
- [docs/DATABASE.md](docs/DATABASE.md): database schema and encryption.
- [docs/G6_SecureAssetGuard.pdf](docs/): full project report.
