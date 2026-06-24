# Build and Deployment

The project has two independent builds: the user-space binaries (CMake) and the kernel module
(kbuild). Both target the Raspberry Pi 4 (aarch64) using a Buildroot toolchain.

## Dependencies

These libraries must be enabled in the Buildroot image and available to the toolchain:

| Library | Used by | Buildroot package |
|---------|---------|-------------------|
| SQLCipher | dDatabase | sqlcipher |
| libargon2 | dDatabase | libargon2 |
| Mongoose | dWebServer | mongoose |
| OpenSSL | dWebServer | openssl (ssl, crypto) |
| nlohmann/json | core, web | json-for-modern-cpp |
| NTP | system | ntp (accurate log timestamps) |

pthread and rt come from the C library.

## User-space build (CMake)

```bash
cmake -B build
cmake --build build
```

This produces four binaries: `wrapper`, `SecureAssetCore`, `dDatabase`, `dWebServer`.

To cross-compile, point CMake at the Buildroot toolchain file, for example:

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/buildroot/output/host/share/buildroot/toolchainfile.cmake
cmake --build build
```

## Kernel module build (kbuild)

```bash
cd driver
make BUILDROOT=/path/to/buildroot/output
```

This produces `my_irq.ko`. The Makefile derives the kernel build directory and the cross
compiler from BUILDROOT; both can also be overridden directly with KDIR and CROSS_COMPILE.

Other targets: `make clean`, `make help`.

## Deployment layout on the target

The launcher resolves the daemon paths relative to its own location, so the four binaries must
sit in the same directory. The expected layout on the Raspberry Pi is:

```
/root/
├── wrapper
├── SecureAssetCore
├── dDatabase
├── dWebServer
├── my_irq.ko
└── SecureAsset/
    └── web/            static files served by dWebServer
```

Runtime files created by the system:

- Database: `secure_asset.db` (encrypted, AES-256).
- PID files: `/var/run/{SecureAssetCore,dWebServer,dDatabase}.pid`.
- Logs: `/var/log/{SecureAssetCore,dWebServer,dDatabase}.log`.

## Run

```bash
./wrapper
```

The launcher loads the kernel module (`insmod /root/my_irq.ko`), creates the message queues,
starts the daemons and waits for Ctrl+C or a termination signal. The web interface is then
reachable on port 8080.
