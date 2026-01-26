/*
 * Mongoose-based web server implementation.
 * Parses JSON, manages sessions, and forwards to DB.
 */

#include "dWebServer.h"
#include <iostream>
#include <cstring>
#include <ctime>
#include <random>

dWebServer::dWebServer(C_Mqueue& toDb, C_Mqueue& fromDb, int port)
    : m_mqToDatabase(toDb), m_mqFromDatabase(fromDb), m_port(port), m_running(false) {
    mg_mgr_init(&m_mgr);
}

dWebServer::~dWebServer() {
    stop();
    mg_mgr_free(&m_mgr);
}

bool dWebServer::start() {
    std::string addr = "http://10.42.0.163:" + std::to_string(m_port) + "/";

    // Start HTTP listener on configured address.
    if (mg_http_listen(&m_mgr, addr.c_str(), eventHandler, this) == nullptr) {
        std::cerr << "[WebServer] Failed to start on port " << m_port << std::endl;
        return false;
    }

    m_running = true;
    std::cout << "[WebServer] Listening on " << addr << std::endl;
    return true;
}

void dWebServer::stop() {
    m_running = false;
}

void dWebServer::run() {
    while (m_running) {
        // Main Mongoose loop + expired session cleanup.
        mg_mgr_poll(&m_mgr, 1000);
        cleanExpiredSessions();
    }
}

bool dWebServer::matchUri(const struct mg_str* uri, const char* pattern) {
    size_t plen = strlen(pattern);
    return uri->len == plen && memcmp(uri->buf, pattern, plen) == 0;
}
bool dWebServer::matchPrefix(const struct mg_str* uri, const char* pattern) {
    size_t plen = strlen(pattern);
    return uri->len >= plen && memcmp(uri->buf, pattern, plen) == 0;
}

void dWebServer::eventHandler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        dWebServer* self = (dWebServer*)c->fn_data;

        // Simple URI-based router.
        if (matchUri(&hm->uri, "/api/login")) {
            self->handleLogin(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/register")) {
            self->handleRegister(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/logout")) {
            self->handleLogout(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/dashboard")) {
            self->handleDashboard(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/sensors")) {
            self->handleSensors(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/actuators")) {
            self->handleActuators(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/logs/filter")) {
            self->handleLogsFilter(c, hm);
        }
        else if (matchPrefix(&hm->uri, "/api/assets/")) {
            self->handleAssetsById(c, hm);
        }
        else if (matchPrefix(&hm->uri, "/api/users/")) {
            self->handleUsersById(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/users")) {
            self->handleUsers(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/assets")) {
            self->handleAssets(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/settings")) {
            self->handleSettings(c, hm);
        }
        else if (matchUri(&hm->uri, "/")) {
            mg_http_reply(c, 302, "Location: /login.html\r\n", "");
        }
        else {
            // Serve static files (with access control).
            SessionData session;
            bool logado = self->validateSession(hm, session);
            if (matchUri(&hm->uri, "/users.html") ||
                matchUri(&hm->uri, "/settings.html") ||
                matchUri(&hm->uri, "/assets.html")) {
                if (!logado || session.accessLevel < 1) {
                    mg_http_reply(c, 302, "Location: /login.html\r\n", "");
                    return;
                }
            }
            struct mg_http_serve_opts opts;
            memset(&opts, 0, sizeof(opts));
            opts.root_dir = "/root/SecureAsset/web";
            mg_http_serve_dir(c, hm, &opts);
        }
    }
}

void dWebServer::handleLogin(struct mg_connection* c, struct mg_http_message* hm) {
    // Parse JSON body with credentials.
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
    } catch (...) {
        sendError(c, 400, "Invalid JSON");
        return;
    }

    // Forward credentials to the DB daemon.
    DatabaseMsg msg = {};
    msg.command = DB_CMD_LOGIN;
    strncpy(msg.payload.login.username, body["user"].get<std::string>().c_str(), 63);
    strncpy(msg.payload.login.password, body["pass"].get<std::string>().c_str(), 63);

    if (!m_mqToDatabase.send(&msg, sizeof(msg))) {
        sendError(c, 500, "Database unavailable");
        return;
    }

    DbWebResponse resp = {};
    ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

    if (bytes <= 0 || !resp.success) {
        sendError(c, 401, "Invalid credentials");
        return;
    }

    // Create session and return cookie to client.
    nlohmann::json userData = nlohmann::json::parse(resp.jsonData);

    std::string token = generateToken();
    SessionData session;
    session.userId = userData["userId"];
    session.username = userData["username"];
    session.accessLevel = userData["accessLevel"];
    session.expires = time(nullptr) + 3600;

    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        m_sessions[token] = session;
    }

    std::string cookie = "session=" + token + "; HttpOnly; Path=/; Max-Age=3600";
    nlohmann::json responseBody = {{"status", "ok"}};
    std::string json = responseBody.dump();

    mg_http_reply(c, 200,
        ("Content-Type: application/json\r\nSet-Cookie: " + cookie + "\r\n").c_str(),
        "%s", json.c_str());
}

void dWebServer::handleRegister(struct mg_connection* c, struct mg_http_message* hm) {
    // Parse JSON body and forward to DB for registration.
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
    } catch (...) {
        sendError(c, 400, "Invalid JSON");
        return;
    }

    DatabaseMsg msg = {};
    msg.command = DB_CMD_REGISTER_USER;
    strncpy(msg.payload.user.name, body["user"].get<std::string>().c_str(), 63);
    strncpy(msg.payload.user.password, body["pass"].get<std::string>().c_str(), 63);

    if (!m_mqToDatabase.send(&msg, sizeof(msg))) {
        sendError(c, 500, "Database unavailable");
        return;
    }

    DbWebResponse resp = {};
    ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

    if (bytes > 0 && resp.success) {
        sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
    } else {
        sendError(c, 400, resp.errorMsg);
    }
}

void dWebServer::handleLogout(struct mg_connection* c, struct mg_http_message* hm) {
    // Extract session token from cookie and invalidate it.
    struct mg_str* cookie = mg_http_get_header(hm, "Cookie");
    if (cookie) {
        std::string cookieStr(cookie->buf, cookie->len);
        size_t pos = cookieStr.find("session=");
        if (pos != std::string::npos) {
            pos += 8;
            size_t end = cookieStr.find(';', pos);
            std::string token = cookieStr.substr(pos, end == std::string::npos ? std::string::npos : end - pos);

            std::lock_guard<std::mutex> lock(m_sessionMutex);
            m_sessions.erase(token);
        }
    }

    std::string clearCookie = "session=; HttpOnly; Path=/; Max-Age=0";
    nlohmann::json responseBody = {{"status", "ok"}};
    std::string json = responseBody.dump();

    mg_http_reply(c, 200,
        ("Content-Type: application/json\r\nSet-Cookie: " + clearCookie + "\r\n").c_str(),
        "%s", json.c_str());
}



void dWebServer::handleDashboard(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires a valid session.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    DatabaseMsg msg = {};
    msg.command = DB_CMD_GET_DASHBOARD;

    if (!m_mqToDatabase.send(&msg, sizeof(msg))) {
        sendError(c, 500, "Database unavailable");
        return;
    }

    DbWebResponse resp = {};
    ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

    if (bytes > 0 && resp.success) {
        nlohmann::json data = nlohmann::json::parse(resp.jsonData);

        data["isAdmin"] = (session.accessLevel >= 1);

        sendJson(c, 200, data);
    } else {
        sendError(c, 500, "Failed to get dashboard data");
    }
}

void dWebServer::handleSensors(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires a valid session.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    DatabaseMsg msg = {};
    msg.command = DB_CMD_GET_SENSORS;
    m_mqToDatabase.send(&msg, sizeof(msg));

    DbWebResponse resp = {};
    ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

    if (bytes > 0 && resp.success) {
        sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
    } else {
        sendError(c, 500, "Failed to get sensors");
    }
}

void dWebServer::handleActuators(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires a valid session.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    DatabaseMsg msg = {};
    msg.command = DB_CMD_GET_ACTUATORS;
    m_mqToDatabase.send(&msg, sizeof(msg));

    DbWebResponse resp = {};
    ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

    if (bytes > 0 && resp.success) {
        sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
    } else {
        sendError(c, 500, "Failed to get actuators");
    }
}

void dWebServer::handleLogsFilter(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires a valid session and a JSON filter body.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
    } catch (...) {
        sendError(c, 400, "Invalid JSON");
        return;
    }

    DatabaseMsg msg = {};
    msg.command = DB_CMD_FILTER_LOGS;
    strncpy(msg.payload.logFilter.timeRange, body["timeRange"].get<std::string>().c_str(), 15);
    strncpy(msg.payload.logFilter.logType, body["logType"].get<std::string>().c_str(), 31);

    m_mqToDatabase.send(&msg, sizeof(msg));

    DbWebResponse resp = {};
    ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

    if (bytes > 0 && resp.success) {
        sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
    } else {
        sendError(c, 500, "Failed to filter logs");
    }
}

void dWebServer::handleUsers(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires admin access.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
        // List users.
        DatabaseMsg msg = {};
        msg.command = DB_CMD_GET_USERS;
        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to get users");
        }
    }
    else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
        // Create user.
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_CREATE_USER;

        std::string name = body.value("name", "");
        std::string rfid = body.value("rfid", "");
        std::string access = body.value("access", "Viewer");

        strncpy(msg.payload.user.name, name.c_str(), 63);
        strncpy(msg.payload.user.rfid, rfid.c_str(), 10);
        msg.payload.user.fingerprintID = body.value("fingerprint", 0);

        if (access == "Room") {
            msg.payload.user.accessLevel = 1;
        } else if (access == "Room/Vault") {
            msg.payload.user.accessLevel = 2;
        } else {
            msg.payload.user.accessLevel = 0;  // Viewer
        }
        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to create user");
        }
    }
}

void dWebServer::handleUsersById(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires admin access and a numeric ID in the URI.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    std::string uri(hm->uri.buf, hm->uri.len);
    std::string idStr = uri.substr(11);
    int userId = std::stoi(idStr);

    if (mg_strcmp(hm->method, mg_str("PUT")) == 0) {
        // Update user by ID.
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_MODIFY_USER;
        msg.payload.user.userID = userId;

        std::string name = body.value("name", "");
        std::string rfid = body.value("rfid", "");
        std::string accessStr = body.value("access", "Viewer");

        strncpy(msg.payload.user.name, name.c_str(), 63);
        strncpy(msg.payload.user.rfid, rfid.c_str(), 10);
        msg.payload.user.fingerprintID = body.value("fingerprint", 0);
        msg.payload.user.accessLevel = (accessStr == "Room/Vault") ? 2 : ((accessStr == "Room") ? 1 : 0);

        msg.payload.user.password[0] = '\0';

        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to modify user");
        }
    }
    else if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
        // Delete user by ID.
        DatabaseMsg msg = {};
        msg.command = DB_CMD_REMOVE_USER;
        msg.payload.userId = userId;

        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to delete user");
        }
    }
}

void dWebServer::handleAssets(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires admin access.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }
    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
        // List assets.
        DatabaseMsg msg = {};
        msg.command = DB_CMD_GET_ASSETS;
        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to get assets");
        }
    }
    else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
        // Create asset.
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_CREATE_ASSET;
        strncpy(msg.payload.asset.name, body["name"].get<std::string>().c_str(), 63);
        strncpy(msg.payload.asset.tag, body["tag"].get<std::string>().c_str(), 31);

        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to create asset");
        }
    }
}

void dWebServer::handleAssetsById(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires admin access and a tag in the URI.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    std::string uri(hm->uri.buf, hm->uri.len);
    std::string tag = uri.substr(12);

    if (mg_strcmp(hm->method, mg_str("PUT")) == 0) {
        // Update asset by tag.
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_MODIFY_ASSET;
        strncpy(msg.payload.asset.name, body["name"].get<std::string>().c_str(), 63);
        strncpy(msg.payload.asset.tag, tag.c_str(), 31);

        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to modify asset");
        }
    }
    else if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
        // Delete asset by tag.
        DatabaseMsg msg = {};
        msg.command = DB_CMD_REMOVE_ASSET;
        strncpy(msg.payload.asset.tag, tag.c_str(), 31);

        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to delete asset");
        }
    }
}

void dWebServer::handleSettings(struct mg_connection* c, struct mg_http_message* hm) {
    // Requires admin access.
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
        // Read system settings.
        DatabaseMsg msg = {};
        msg.command = DB_CMD_GET_SETTINGS;
        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to get settings");
        }
    }
    else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
        // Update system settings.
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_UPDATE_SETTINGS;
        msg.payload.settings.tempThreshold = body["tempLimit"];
        msg.payload.settings.samplingInterval = body["sampleTime"].get<int>() * 60;  // Min -> Sec

        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to update settings");
        }
    }
}

std::string dWebServer::generateToken() {
    // Generate a 32-hex-character session token.
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    const char* hex = "0123456789abcdef";
    std::string token;
    for (int i = 0; i < 32; i++) {
        token += hex[dis(gen)];
    }
    return token;
}

bool dWebServer::validateSession(struct mg_http_message* hm, SessionData& outSession) {
    // Parse cookie header and validate token in the session map.
    struct mg_str* cookie = mg_http_get_header(hm, "Cookie");
    if (!cookie) return false;

    std::string cookieStr(cookie->buf, cookie->len);
    size_t pos = cookieStr.find("session=");
    if (pos == std::string::npos) return false;

    pos += 8;
    size_t end = cookieStr.find(';', pos);
    std::string token = cookieStr.substr(pos, end == std::string::npos ? std::string::npos : end - pos);

    std::lock_guard<std::mutex> lock(m_sessionMutex);

    auto it = m_sessions.find(token);
    if (it == m_sessions.end()) return false;

    if (it->second.expires < time(nullptr)) {
        m_sessions.erase(it);
        return false;
    }

    outSession = it->second;
    return true;
}

void dWebServer::cleanExpiredSessions() {
    // Remove expired sessions from the map.
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    time_t now = time(nullptr);
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (it->second.expires < now) {
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }
}

void dWebServer::sendJson(struct mg_connection* c, int statusCode, const nlohmann::json& data) {
    // Serialize JSON and send response.
    std::string json = data.dump();
    mg_http_reply(c, statusCode, "Content-Type: application/json\r\n", "%s", json.c_str());
}

void dWebServer::sendError(struct mg_connection* c, int statusCode, const std::string& message) {
    // Send standardized error payload.
    nlohmann::json error = {{"error", message}};
    sendJson(c, statusCode, error);
}
