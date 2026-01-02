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

    // ✅ Passamos 'this' como fn_data (4º parâmetro)
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

// ✅ Helper para comparar URIs (compatível com Mongoose 7.17)
bool dWebServer::matchUri(const struct mg_str* uri, const char* pattern) {
    size_t plen = strlen(pattern);
    return uri->len == plen && memcmp(uri->buf, pattern, plen) == 0;
}

// ✅ Event handler com 3 parâmetros
void dWebServer::eventHandler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;

        // ✅ Recuperar 'this' do fn_data que foi passado no mg_http_listen
        dWebServer* self = (dWebServer*)c->fn_data;

        // ✅ Comparação manual de URIs (sem mg_http_match_uri)
        if (matchUri(&hm->uri, "/api/login")) {
            self->handleLogin(c, hm);
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
        else {
            self->sendError(c, 404, "Endpoint not found");
        }
    }
}

void dWebServer::handleLogin(struct mg_connection* c, struct mg_http_message* hm) {
    nlohmann::json body;
    try {
        // ✅ body.ptr e body.len existem no Mongoose 7.17
        body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
    } catch (...) {
        sendError(c, 400, "Invalid JSON");
        return;
    }

    nlohmann::json params;
    params["user"] = body["user"];
    params["pass"] = body["pass"];

    nlohmann::json response = queryDatabase(DB_CMD_LOGIN, params);

    if (response.contains("error")) {
        sendError(c, 401, "Invalid credentials");
        return;
    }

    std::string token = generateToken();
    SessionData session;
    session.userId = response["userId"];
    session.username = response["username"];
    session.accessLevel = response["accessLevel"];
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

void dWebServer::handleDashboard(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    nlohmann::json response = queryDatabase(DB_CMD_GET_DASHBOARD);

    if (response.contains("error")) {
        sendError(c, 500, response["error"]);
        return;
    }

    sendJson(c, 200, response);
}

void dWebServer::handleSensors(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    nlohmann::json response = queryDatabase(DB_CMD_GET_SENSORS);

    if (response.contains("error")) {
        sendError(c, 500, response["error"]);
        return;
    }

    sendJson(c, 200, response);
}

void dWebServer::handleActuators(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    nlohmann::json response = queryDatabase(DB_CMD_GET_ACTUATORS);

    if (response.contains("error")) {
        sendError(c, 500, response["error"]);
        return;
    }

    sendJson(c, 200, response);
}

nlohmann::json dWebServer::queryDatabase(e_DbCommand cmd, const nlohmann::json& params) {
    DatabaseMsg msg = {};
    msg.command = cmd;

    if (cmd == DB_CMD_LOGIN) {
        std::string user = params["user"];
        std::string pass = params["pass"];
        strncpy(msg.payload.login.username, user.c_str(), sizeof(msg.payload.login.username) - 1);
        msg.payload.login.username[sizeof(msg.payload.login.username) - 1] = '\0';
        strncpy(msg.payload.login.password, pass.c_str(), sizeof(msg.payload.login.password) - 1);
        msg.payload.login.password[sizeof(msg.payload.login.password) - 1] = '\0';
    }

    if (!m_mqToDatabase.send(&msg, sizeof(msg))) {
        return {{"error", "Failed to send to database"}};
    }

    DbWebResponse resp = {};
    ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);

    if (bytes <= 0) {
        return {{"error", "Database timeout"}};
    }

    if (!resp.success) {
        return {{"error", std::string(resp.errorMsg)}};
    }

    return nlohmann::json::parse(resp.jsonData);
}

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