# Secure Asset Guard

Embedded security system for vaults and protected rooms, running on a **Raspberry Pi 4**
with a custom **Buildroot** Linux image. The user-space application is written in **C++17**
and is backed by a custom **Linux kernel module** for interrupt-driven sensing.

It integrates multi-layer access control, intrusion detection, environmental monitoring and
asset inventory, all managed through a local web interface.

> Academic project — MSc in Industrial Electronics and Computers Engineering, University of Minho.
> João Mendes · Tiago Oliveira.

## Features

- **Access control** — RFID (125 kHz) for room entry/exit, capacitive fingerprint for the vault.
- **Intrusion detection** — PIR motion sensor + reed switches, kernel-interrupt driven.
- **Environmental monitoring** — SHT30 temperature/humidity with automatic fan control.
- **Inventory** — UHF RFID (YRM1001) scan of vault assets on door close.
- **Web interface** — dashboard, logs, user/asset management (HTML/CSS/JS + REST API).
- **Security at rest** — SQLite + SQLCipher (AES-256) and Argon2id password hashing.

## Architecture

A **launcher** owns the IPC resources (POSIX message queues) and supervises three user-space
daemons. A kernel module turns GPIO interrupts into Real-Time Signals, so the core never polls.

```
                         ┌─────────────────────────────────────────┐
   sensors (GPIO IRQ) ──▶│  driver/  my_irq.ko  ──▶ RT signals 43-48 │
                         └─────────────────────────────────────────┘
                                            │
   wrapper (launcher) ──────────────────────┼─── creates queues, supervises lifecycle
     ├── SecureAssetCore   singleton + worker threads ── sensors, actuators, logic
     ├── dWebServer        Mongoose HTTP/REST API       ── bridges browser ⇄ database
     └── dDatabase         SQLite + SQLCipher + Argon2id ── all persistence
```

The launcher starts daemons in order with a readiness handshake and shuts them down
gracefully (SIGTERM → ACK/EOF → SIGKILL fallback). Full design in [`docs/`](docs/).

## Repository structure

```
.
├── CMakeLists.txt          # user-space build (4 binaries)
├── src/
│   ├── launcher/           # supervisor process (main.cpp)
│   ├── core/               # SecureAssetCore daemon
│   │   ├── hal/            #   GPIO, I2C, PWM, UART
│   │   ├── devices/        #   sensor & actuator drivers
│   │   ├── ipc/            #   message queue + monitor wrappers
│   │   ├── threads/        #   worker threads
│   │   └── SharedTypes.h   #   IPC message/struct definitions
│   └── daemons/
│       ├── database/       # dDatabase daemon
│       └── web/            # dWebServer daemon
├── driver/                 # Linux kernel module (GPIO IRQ → Real-Time Signals)
├── web/                    # static front-end served by dWebServer
└── docs/                   # report, presentation, hardware reference
```

## Build

### User-space (CMake)

Targets the Buildroot toolchain for the Raspberry Pi 4 (aarch64). Required libraries on the
target image: `sqlcipher`, `argon2`, `mongoose`, `openssl`, plus `pthread` and `rt`.

```bash
cmake -B build
cmake --build build
```

Produces `wrapper`, `SecureAssetCore`, `dDatabase`, `dWebServer`.

### Kernel module (kbuild)

```bash
cd driver
make BUILDROOT=/path/to/buildroot/output
```

Produces `my_irq.ko`. See [`driver/`](driver/) and [`docs/HARDWARE.md`](docs/HARDWARE.md).

## Run

On the target, the launcher expects the daemon binaries in its own directory, the kernel
module at `/root/my_irq.ko`, and the web front-end at `/root/SecureAsset/web`.

```bash
./wrapper            # loads the driver, creates IPC queues, starts daemons, waits for Ctrl+C
```

The web interface listens on port **8080**.

## Documentation

- [`docs/HARDWARE.md`](docs/HARDWARE.md) — GPIO pinout, components, power, IRQ signal map.
- [`docs/G6_SecureAssetGuard.pdf`](docs/) — full project report.
