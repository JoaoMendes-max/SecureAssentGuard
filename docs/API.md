# REST API

The dWebServer daemon exposes a REST API on port 8080. The browser front-end calls these
endpoints; dWebServer forwards each request to dDatabase over message queues and returns the
response. All payloads are JSON.

## Endpoints

| Endpoint | Method | Description | Access |
|----------|--------|-------------|--------|
| /api/login | POST | Authenticate credentials and start a session. | Public |
| /api/register | POST | Register a new web account. | Public |
| /api/logout | POST | Invalidate the current session. | Authenticated |
| /api/dashboard | GET | General system status and security state. | Viewer |
| /api/sensors | GET | Real-time environmental and occupancy data. | Viewer |
| /api/actuators | GET | Current state of all actuators. | Viewer |
| /api/logs/filter | POST | History logs filtered by time range and type. | Viewer |
| /api/users | GET, POST | List all users or create a new one. | Admin |
| /api/users/{id} | PUT, DELETE | Modify or remove a user by id. | Admin |
| /api/assets | GET, POST | List all assets or create a new one. | Admin |
| /api/assets/{tag} | PUT, DELETE | Modify or remove an asset by RFID tag. | Admin |
| /api/settings | GET, POST | Read or update thresholds and sampling interval. | Admin |

Any other path serves the static front-end from /root/SecureAsset/web. The management pages
(users.html, settings.html, assets.html) require an authenticated session with admin access.

## Sessions

- On successful login the server generates a 32-character hexadecimal token and returns it as
  an HttpOnly cookie.
- Every subsequent request is validated against the active session map. Each session carries
  an access level: 0 for Viewer, 1 for Room/Admin.
- Sessions are invalidated after one hour of inactivity. dWebServer prunes expired sessions on
  every poll cycle.

## Notes

- Passwords are never stored in plaintext. They are hashed with Argon2id and a random salt by
  dDatabase before storage.
- The server binds to a fixed address in `dWebServer::start()`. Adjust it there if the target
  network differs.
