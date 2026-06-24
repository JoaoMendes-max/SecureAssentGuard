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

## How it works

### Layered design

The software is organized in three layers:

1. Custom Linux and drivers. The Buildroot image plus the `my_irq.ko` kernel module sit at the
   bottom. The module watches the sensor GPIO pins and, on each edge, sends a Real-Time Signal
   to the application instead of letting it poll.
2. Middleware. The core process (SecureAssetCore) wraps the hardware in C++ classes: a HAL for
   GPIO, I2C, PWM and UART; device drivers for each sensor and actuator; and IPC wrappers for
   message queues and condition variables. Worker threads run the logic on top of these.
3. Application. The database daemon owns all persistence and the web daemon serves the user
   interface. Neither touches hardware directly.

The whole system runs as four cooperating processes (see the diagram above). They share no
memory: every interaction is a fixed-size message on a POSIX queue, which keeps the boundaries
clean and the message sizes deterministic.

### Event-driven model

The core does not spin polling the sensors. Each input pin is mapped, in the kernel module, to
a Real-Time Signal (43 to 48). A dedicated thread, `tSighandler`, receives these signals and
signals the matching condition variable. The corresponding worker thread, which was blocked
waiting, wakes up, does its work, and goes back to sleep. This keeps CPU usage near zero while
idle and gives a near-instant response when something happens.

A request that needs data always follows the same pattern: the worker sends a `DatabaseMsg` to
dDatabase on `/mq_to_db`, then blocks waiting for the reply on its own response queue. The
database is the only writer of persistent state, so there are no concurrent-access races.

### Typical flows

- Room access. A card at the entry reader triggers an interrupt; `tVerifyRoomAccess` wakes,
  reads the tag over UART and asks the database to authenticate it. On success it commands the
  room servo (through `tAct`) to unlock; on repeated failures it triggers the alarm. The
  attempt is logged either way. Leaving the room follows the same path through
  `tLeaveRoomAccess`, which updates the presence state.
- Vault access. The fingerprint module authenticates internally and pulses its WAKE pin.
  `tVerifyVaultAccess` wakes, and if the print is authorized it commands the vault servo. The
  access attempt is logged.
- Intrusion. Motion on the PIR triggers `tCheckMovement`, which asks the database whether an
  authorized person is registered inside the room. If nobody is, it commands the buzzer and
  LED and logs an invalid access.
- Environmental control. `tReadEnvSensor` runs on a timer, reads the SHT30 over I2C, and
  compares the temperature against the configured threshold. Above it, it commands the fan.
  The reading is stored in the database.
- Inventory. Closing the vault door triggers the reed switch interrupt; `tInventoryScan` reads
  the UHF reader, and the database updates each asset as inside or outside the vault.
- Web request. The browser calls a REST endpoint; dWebServer translates it into a `DatabaseMsg`,
  forwards it to dDatabase, waits for the reply, and returns it as JSON. Sessions are tracked
  with an HttpOnly token and an access level.

A single header, `src/core/SharedTypes.h`, defines every message and enum, so all four
binaries agree on the exact layout of what travels through the queues.

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
