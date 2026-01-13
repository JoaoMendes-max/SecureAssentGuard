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
    std::string addr = "http://0.0.0.0:" + std::to_string(m_port);

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
        mg_mgr_poll(&m_mgr, 1000);
        cleanExpiredSessions();
    }
}

// ============================================
// EVENT HANDLER (Router Principal)
// ============================================

bool dWebServer::matchUri(const struct mg_str* uri, const char* pattern) {
    size_t plen = strlen(pattern);
    return uri->len == plen && memcmp(uri->buf, pattern, plen) == 0;
}
bool dWebServer::matchPrefix(const struct mg_str* uri, const char* pattern) {
    size_t plen = strlen(pattern);
    // Verifica se o URI é maior ou igual ao pattern e se o início coincide
    return uri->len >= plen && memcmp(uri->buf, pattern, plen) == 0;
}

void dWebServer::eventHandler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        dWebServer* self = (dWebServer*)c->fn_data;

        // ========== ROTAS PÚBLICAS (sem autenticação) ==========
        if (matchUri(&hm->uri, "/api/login")) {
            self->handleLogin(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/register")) {
            self->handleRegister(c, hm);
        }
        else if (matchUri(&hm->uri, "/api/logout")) {
            self->handleLogout(c, hm);
        }

        // ========== ROTAS PROTEGIDAS (requerem autenticação) ==========
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

        // ========== ROTAS ADMIN (requerem AccessLevel >= 1) ==========
        else if (matchPrefix(&hm->uri, "/api/assets/")) {
            self->handleAssetsById(c, hm);
        }
        else if (matchPrefix(&hm->uri, "/api/users/")) { // <--- MUDANÇA AQUI
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

        // ========== 404 ==========
        else {
            self->sendError(c, 404, "Endpoint not found");
        }
    }
}

// ============================================
// AUTENTICAÇÃO
// ============================================

void dWebServer::handleLogin(struct mg_connection* c, struct mg_http_message* hm) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
    } catch (...) {
        sendError(c, 400, "Invalid JSON");
        return;
    }

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

// ============================================
// DASHBOARD & VISUALIZAÇÃO (todos os users)
// ============================================

void dWebServer::handleDashboard(struct mg_connection* c, struct mg_http_message* hm) {
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

        // ✅ ADICIONAR isAdmin baseado no AccessLevel
        data["isAdmin"] = (session.accessLevel >= 1);

        sendJson(c, 200, data);
    } else {
        sendError(c, 500, "Failed to get dashboard data");
    }
}

void dWebServer::handleSensors(struct mg_connection* c, struct mg_http_message* hm) {
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

// ============================================
// USERS (ADMIN apenas)
// ============================================

void dWebServer::handleUsers(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    // GET - Listar
    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
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
    // POST - Criar
    else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_CREATE_USER;
        strncpy(msg.payload.user.name, body["name"].get<std::string>().c_str(), 63);
        strncpy(msg.payload.user.rfid, body["rfid"].get<std::string>().c_str(), 10); // nao tenho a certeza desta shit por causa dos \0
        msg.payload.user.fingerprintID = body.value("fingerprint", 0);
        std::string access = body["access"].get<std::string>();
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
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    // Extrair ID do URL: /api/users/5 -> "5"
    std::string uri(hm->uri.buf, hm->uri.len);
    std::string idStr = uri.substr(11);  // "/api/users/" = 11 chars
    int userId = std::stoi(idStr);

    // PUT - Editar
    if (mg_strcmp(hm->method, mg_str("PUT")) == 0) {
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

        strncpy(msg.payload.user.name, body["name"].get<std::string>().c_str(), 63);
        strncpy(msg.payload.user.rfid, body["rfid"].get<std::string>().c_str(), 10);
        msg.payload.user.fingerprintID = body.value("fingerprint", 0);  // ← CORRIGIDO
        // 3. CORRECÇÃO DO ACESSO (Onde antes usavas a password)
        std::string accessStr = body["access"].get<std::string>();
        msg.payload.user.accessLevel = (accessStr == "Room/Vault") ? 2 : 1; // Converte string para número

        // A password fica vazia (terminador nulo no início), pois não se edita a pass aqui
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
    // DELETE - Apagar
    else if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
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

// ============================================
// ASSETS (ADMIN apenas)
// ============================================

void dWebServer::handleAssets(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    // GET - Listar
    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
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
    // POST - Criar
    else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
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
        strncpy(msg.payload.asset.state, body["state"].get<std::string>().c_str(), 15);

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
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    // Extrair TAG do URL
    std::string uri(hm->uri.buf, hm->uri.len);
    std::string tag = uri.substr(12);  // "/api/assets/" = 12 chars

    // PUT - Editar
    if (mg_strcmp(hm->method, mg_str("PUT")) == 0) {
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
        strncpy(msg.payload.asset.state, body["state"].get<std::string>().c_str(), 15);

        m_mqToDatabase.send(&msg, sizeof(msg));

        DbWebResponse resp = {};
        ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

        if (bytes > 0 && resp.success) {
            sendJson(c, 200, nlohmann::json::parse(resp.jsonData));
        } else {
            sendError(c, 500, "Failed to modify asset");
        }
    }
    // DELETE - Apagar
    else if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
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

// ============================================
// SETTINGS (ADMIN apenas)
// ============================================

void dWebServer::handleSettings(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    if (session.accessLevel < 1) {
        sendError(c, 403, "Admin access required");
        return;
    }

    // GET - Ler
    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
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
    // POST - Atualizar
    else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
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
        msg.payload.settings.samplingInterval = body["sampleTime"].get<int>() * 60;  // Min -> Seg

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

// ============================================
// UTILITÁRIOS
// ============================================

std::string dWebServer::generateToken() {
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
    std::string json = data.dump();
    mg_http_reply(c, statusCode, "Content-Type: application/json\r\n", "%s", json.c_str());
}

void dWebServer::sendError(struct mg_connection* c, int statusCode, const std::string& message) {
    nlohmann::json error = {{"error", message}};
    sendJson(c, statusCode, error);
}