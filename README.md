# Secure Asset Guard

Embedded security system for vaults and protected rooms, running on a **Raspberry Pi 4**
with a custom **Buildroot** Linux image. Written in **C++17**.

Integrates multi-layer access control, intrusion detection, environmental monitoring and
asset inventory, all managed through a local web interface.

> Academic project — MSc in Industrial Electronics and Computers Engineering, University of Minho.
> João Mendes · Tiago Oliveira.

## Features

- **Access control** — RFID (125 kHz) for room entry/exit, capacitive fingerprint for the vault.
- **Intrusion detection** — PIR motion sensor + reed switches, kernel-interrupt driven.
- **Environmental monitoring** — SHT30 temperature/humidity, automatic fan control.
- **Inventory** — UHF RFID (YRM1001) scan of vault assets.
- **Web interface** — dashboard, logs, user/asset management (HTML/CSS/JS + REST API).
- **Security at rest** — SQLite + SQLCipher (AES-256), Argon2id password hashing.

## Architecture

A **launcher** process owns the IPC resources (POSIX message queues) and supervises three
daemons. They communicate exclusively through message queues.

```
wrapper (launcher)
 ├── dDatabase        SQLite + SQLCipher, Argon2id  ── handles all persistence
 ├── dWebServer       Mongoose HTTP/REST API        ── bridges browser ⇄ database
 └── SecureAssetCore  singleton + 8 worker threads  ── sensors, actuators, logic
```

The launcher starts daemons in order with a readiness handshake and shuts them down
gracefully (SIGTERM → ACK/EOF → SIGKILL fallback). See the report in [`docs/`](docs/).

## Repository structure

```
.
├── CMakeLists.txt
├── src/
│   ├── launcher/        # supervisor process (main.cpp)
│   ├── core/            # SecureAssetCore daemon
│   │   ├── hal/         #   GPIO, I2C, PWM, UART
│   │   ├── devices/     #   sensor & actuator drivers
│   │   ├── ipc/         #   message queue + monitor wrappers
│   │   ├── threads/     #   worker threads
│   │   └── SharedTypes.h#   IPC message/struct definitions
│   └── daemons/
│       ├── database/    # dDatabase daemon
│       └── web/         # dWebServer daemon
├── web/                 # static front-end served by dWebServer
└── docs/                # full report + presentation + hardware reference
```

## Build

Targets the Buildroot toolchain for the Raspberry Pi 4 (aarch64). Required libraries on the
target image: `sqlcipher`, `argon2`, `mongoose`, `openssl`, plus `pthread` and `rt`.

```bash
cmake -B build
cmake --build build
```

Produces four binaries: `wrapper`, `SecureAssetCore`, `dDatabase`, `dWebServer`.

## Run

The launcher expects the daemon binaries in its own directory and the kernel IRQ module
available at `/root/my_irq.ko`. The web front-end is served from `/root/SecureAsset/web`.

```bash
./wrapper            # creates IPC queues, starts daemons, waits for Ctrl+C
```

The web interface listens on port **8080**. See [`docs/HARDWARE.md`](docs/HARDWARE.md) for
the GPIO pinout and component list.
