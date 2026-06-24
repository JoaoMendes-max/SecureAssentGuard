# Database

The dDatabase daemon is the single access point to the data store. It uses SQLite through
SQLCipher, so the file on disk is fully encrypted. All other components reach the database only
by sending DatabaseMsg requests over the /mq_to_db queue, which keeps access serialized.

## Encryption

- The database file is encrypted with AES-256 (SQLCipher 4 defaults: 4096-byte pages,
  PBKDF2-HMAC-SHA512 key derivation).
- The 32-byte key is reconstructed at runtime by XORing two constant byte arrays, so it is not
  stored as a readable string in the binary. The temporary key buffer is wiped from memory
  right after it is applied.
- If the key cannot be applied, the daemon closes the handle and aborts startup rather than
  running unencrypted.

## Schema

Six tables, created on first run if missing.

### Users

| Column | Type | Notes |
|--------|------|-------|
| UserID | INTEGER | Primary key, autoincrement |
| Name | TEXT | |
| RFID_Card | TEXT | Unique |
| FingerprintID | INTEGER | Unique |
| Password | TEXT | Argon2id hash |
| AccessLevel | INTEGER | 0 Viewer, 1 Room/Admin |
| IsInside | INTEGER | Presence flag, default 0 |

### Logs

| Column | Type | Notes |
|--------|------|-------|
| LogsID | INTEGER | Primary key, autoincrement |
| EntityID | INTEGER | Related entity |
| Timestamp | INTEGER | Unix time |
| LogType | INTEGER | actuator, sensor, access, system, alert, inventory |
| Description | TEXT | |
| Value | REAL | |
| Value2 | REAL | Default 0 |

### Assets

| Column | Type | Notes |
|--------|------|-------|
| AssetID | INTEGER | Primary key, autoincrement |
| Name | TEXT | |
| RFID_Tag | TEXT | Unique |
| LastRead | INTEGER | Unix time of last detection |

### Sensors

| Column | Type | Notes |
|--------|------|-------|
| SensorID | INTEGER | Primary key, autoincrement |
| Type | TEXT | Unique |
| Value | REAL | Last reading |
| LastUpdate | INTEGER | Default 0 |

### Actuators

| Column | Type | Notes |
|--------|------|-------|
| ActuatorID | INTEGER | Primary key, autoincrement |
| Type | TEXT | Unique |
| State | INTEGER | Current state |
| LastUpdate | INTEGER | Default 0 |

### SystemSettings

| Column | Type | Notes |
|--------|------|-------|
| ID | INTEGER | Primary key, fixed at 1 |
| TempThreshold | INTEGER | Fan trigger, default 30 |
| SamplingTime | INTEGER | Sensor sampling interval in seconds, default 600 |

## Request dispatch

Requests arrive as DatabaseMsg with an e_DbCommand selector. The daemon dispatches each
command to a dedicated handler in `processDbMessage()` (authentication, logging, dashboard and
sensor/actuator queries, user and asset CRUD, settings, and log filtering). Responses use
AuthResponse for core threads and DbWebResponse (a success flag plus JSON and error buffers)
for the web daemon.
