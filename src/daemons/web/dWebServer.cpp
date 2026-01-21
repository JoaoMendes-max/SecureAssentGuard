#include "dWebServer.h"
#include <iostream>
#include <cstring>
#include <ctime>
#include <random>
#include <unordered_map>
#include <cstdint>

namespace {
struct WakeupMsg {
    uint64_t connId;
    int status;
    std::string headers;
    std::string body;
};

void copyString(char* dest, size_t destSize, const std::string& src) {
    if (!dest || destSize == 0) {
        return;
    }
    std::strncpy(dest, src.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
}

bool sendWakeup(struct mg_mgr* mgr, uint64_t connId, WakeupMsg* msg) {
    const uintptr_t ptr = reinterpret_cast<uintptr_t>(msg);
    return mg_wakeup(mgr, static_cast<unsigned long>(connId), &ptr, sizeof(ptr));
}
}

// dWebServer::dWebServer(C_Mqueue& toDb, C_Mqueue& fromDb, int port) : m_mqToDatabase(toDb), m_mqFromDatabase(fromDb), m_port(port), m_running(false) {
dWebServer::dWebServer(C_Mqueue& toDb, C_Mqueue& fromDb, int port)
    : m_mqToDatabase(toDb),
      m_mqFromDatabase(fromDb),
      m_port(port),
      m_running(false),
      m_workerRunning(false),
      m_nextRequestId(1) {
    mg_mgr_init(&m_mgr);
}

dWebServer::~dWebServer() {
    stop();
    mg_mgr_free(&m_mgr);
}

bool dWebServer::start() {
    std::string addr = "http://10.42.0.163:" + std::to_string(m_port) + "/";

    if (!mg_wakeup_init(&m_mgr)) {
        std::cerr << "[WebServer] Failed to init wakeup" << std::endl;
        return false;
    }

    if (mg_http_listen(&m_mgr, addr.c_str(), eventHandler, this) == nullptr) {
        std::cerr << "[WebServer] Failed to start on port " << m_port << std::endl;
        return false;
    }

    m_running = true;
    m_workerRunning = true;
    m_dbWorker = std::thread(&dWebServer::processDbQueue, this);
    std::cout << "[WebServer] Listening on " << addr << std::endl;
    return true;
}

void dWebServer::stop() {
    m_running = false;
    m_workerRunning = false;
    m_dbCv.notify_all();
    if (m_dbWorker.joinable()) {
        m_dbWorker.join();
    }
}

void dWebServer::run() {
    while (m_running) {
        mg_mgr_poll(&m_mgr, 1000);
        cleanExpiredSessions();
    }
}

bool dWebServer::enqueueDbRequest(const DatabaseMsg& msg, uint64_t connId, int successStatus, int errorStatus, bool addIsAdmin, const SessionData& session) {
    if (!m_workerRunning) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_dbMutex);
    m_dbQueue.push_back(DbRequest{msg, connId, successStatus, errorStatus, addIsAdmin, session});
    m_dbCv.notify_one();
    return true;
}

void dWebServer::processDbQueue() {
    std::unordered_map<uint32_t, DbWebResponse> pending;
    while (m_workerRunning) {
        DbRequest req{};
        {
            std::unique_lock<std::mutex> lock(m_dbMutex);
            m_dbCv.wait(lock, [this] { return !m_workerRunning || !m_dbQueue.empty(); });
            if (!m_workerRunning && m_dbQueue.empty()) {
                return;
            }
            req = m_dbQueue.front();
            m_dbQueue.pop_front();
        }

        if (!m_mqToDatabase.send(&req.msg, sizeof(req.msg))) {
            auto* wake = new WakeupMsg{req.connId, req.errorStatus, "Content-Type: application/json\r\n", R"({"error":"Database unavailable"})"};
            // mg_mgr_wakeup(&m_mgr, req.connId, wake);
            if (!sendWakeup(&m_mgr, req.connId, wake)) {
                delete wake;
            }
            continue;
        }

        DbWebResponse resp{};
        bool got = false;

        auto it = pending.find(req.msg.requestId);
        if (it != pending.end()) {
            resp = it->second;
            pending.erase(it);
            got = true;
        }

        for (int i = 0; !got && i < 4; ++i) {
            ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2);
            if (bytes <= 0) {
                break;
            }
            if (resp.requestId == req.msg.requestId) {
                got = true;
                break;
            }
            pending.emplace(resp.requestId, resp);
        }

        if (!got) {
            auto* wake = new WakeupMsg{req.connId, req.errorStatus, "Content-Type: application/json\r\n", R"({"error":"Database timeout"})"};
            // mg_mgr_wakeup(&m_mgr, req.connId, wake);
            if (!sendWakeup(&m_mgr, req.connId, wake)) {
                delete wake;
            }
            continue;
        }

        if (!resp.success) {
            std::string msg = resp.errorMsg[0] != '\0' ? resp.errorMsg : "Database error";
            std::string body = std::string("{\"error\":\"") + msg + "\"}";
            auto* wake = new WakeupMsg{req.connId, req.errorStatus, "Content-Type: application/json\r\n", body};
            // mg_mgr_wakeup(&m_mgr, req.connId, wake);
            if (!sendWakeup(&m_mgr, req.connId, wake)) {
                delete wake;
            }
            continue;
        }

        if (req.msg.command == DB_CMD_LOGIN) {
            nlohmann::json userData;
            try {
                userData = nlohmann::json::parse(resp.jsonData);
            } catch (...) {
                auto* wake = new WakeupMsg{req.connId, req.errorStatus, "Content-Type: application/json\r\n", R"({"error":"Invalid database response"})"};
                // mg_mgr_wakeup(&m_mgr, req.connId, wake);
                if (!sendWakeup(&m_mgr, req.connId, wake)) {
                    delete wake;
                }
                continue;
            }
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
            std::string headers = "Content-Type: application/json\r\nSet-Cookie: " + cookie + "\r\n";
            auto* wake = new WakeupMsg{req.connId, req.successStatus, headers, json};
            // mg_mgr_wakeup(&m_mgr, req.connId, wake);
            if (!sendWakeup(&m_mgr, req.connId, wake)) {
                delete wake;
            }
            continue;
        }

        std::string body = resp.jsonData;
        if (req.addIsAdmin) {
            try {
                nlohmann::json data = nlohmann::json::parse(resp.jsonData);
                data["isAdmin"] = (req.session.accessLevel >= 1);
                body = data.dump();
            } catch (...) {
                auto* wake = new WakeupMsg{req.connId, req.errorStatus, "Content-Type: application/json\r\n", R"({"error":"Invalid database response"})"};
                // mg_mgr_wakeup(&m_mgr, req.connId, wake);
                if (!sendWakeup(&m_mgr, req.connId, wake)) {
                    delete wake;
                }
                continue;
            }
        }

        auto* wake = new WakeupMsg{req.connId, req.successStatus, "Content-Type: application/json\r\n", body};
        // mg_mgr_wakeup(&m_mgr, req.connId, wake);
        if (!sendWakeup(&m_mgr, req.connId, wake)) {
            delete wake;
        }
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

// void dWebServer::eventHandler(struct mg_connection* c, int ev, void* ev_data) { if (ev == MG_EV_HTTP_MSG) { struct mg_http_message* hm = (struct mg_http_message*)ev_data; dWebServer* self = (dWebServer*)c->fn_data;
void dWebServer::eventHandler(struct mg_connection* c, int ev, void* ev_data) {
    if (ev == MG_EV_WAKEUP) {
        // auto* msg = static_cast<WakeupMsg*>(ev_data);
        // if (msg && c->id == msg->connId) {
        //     mg_http_reply(c, msg->status, msg->headers.c_str(), "%s", msg->body.c_str());
        //     delete msg;
        // }
        auto* data = static_cast<struct mg_str*>(ev_data);
        if (data && data->buf && data->len == sizeof(uintptr_t)) {
            uintptr_t ptr = 0;
            std::memcpy(&ptr, data->buf, sizeof(ptr));
            auto* msg = reinterpret_cast<WakeupMsg*>(ptr);
            if (msg && c->id == msg->connId) {
                mg_http_reply(c, msg->status, msg->headers.c_str(), "%s", msg->body.c_str());
            }
            delete msg;
        }
        return;
    }
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = static_cast<struct mg_http_message*>(ev_data);
        dWebServer* self = static_cast<dWebServer*>(c->fn_data);

        
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
            opts.root_dir = "./web";
            mg_http_serve_dir(c, hm, &opts);
        }
    }
}





void dWebServer::handleLogin(struct mg_connection* c, struct mg_http_message* hm) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
    } catch (...) {
        sendError(c, 400, "Invalid JSON");
        return;
    }

    // DatabaseMsg msg = {}; msg.command = DB_CMD_LOGIN; strncpy(msg.payload.login.username, body["user"].get<std::string>().c_str(), 63); strncpy(msg.payload.login.password, body["pass"].get<std::string>().c_str(), 63); if (!m_mqToDatabase.send(&msg, sizeof(msg))) { sendError(c, 500, "Database unavailable"); return; } DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes <= 0 || !resp.success) { sendError(c, 401, "Invalid credentials"); return; } nlohmann::json userData = nlohmann::json::parse(resp.jsonData); std::string token = generateToken(); SessionData session; session.userId = userData["userId"]; session.username = userData["username"]; session.accessLevel = userData["accessLevel"]; session.expires = time(nullptr) + 3600; { std::lock_guard<std::mutex> lock(m_sessionMutex); m_sessions[token] = session; } std::string cookie = "session=" + token + "; HttpOnly; Path=/; Max-Age=3600"; nlohmann::json responseBody = {{"status", "ok"}}; std::string json = responseBody.dump(); mg_http_reply(c, 200, ("Content-Type: application/json\r\nSet-Cookie: " + cookie + "\r\n").c_str(), "%s", json.c_str());
    if (!body.contains("user") || !body.contains("pass")) {
        sendError(c, 400, "Missing credentials");
        return;
    }

    DatabaseMsg msg = {};
    msg.command = DB_CMD_LOGIN;
    msg.requestId = m_nextRequestId.fetch_add(1);
    copyString(msg.payload.login.username, sizeof(msg.payload.login.username), body["user"].get<std::string>());
    copyString(msg.payload.login.password, sizeof(msg.payload.login.password), body["pass"].get<std::string>());

    SessionData session{};
    if (!enqueueDbRequest(msg, c->id, 200, 401, false, session)) {
        sendError(c, 500, "Database unavailable");
        return;
    }
}

void dWebServer::handleRegister(struct mg_connection* c, struct mg_http_message* hm) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
    } catch (...) {
        sendError(c, 400, "Invalid JSON");
        return;
    }

    // DatabaseMsg msg = {}; msg.command = DB_CMD_REGISTER_USER; strncpy(msg.payload.user.name, body["user"].get<std::string>().c_str(), 63); strncpy(msg.payload.user.password, body["pass"].get<std::string>().c_str(), 63); if (!m_mqToDatabase.send(&msg, sizeof(msg))) { sendError(c, 500, "Database unavailable"); return; } DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 400, resp.errorMsg); }
    if (!body.contains("user") || !body.contains("pass")) {
        sendError(c, 400, "Missing credentials");
        return;
    }

    DatabaseMsg msg = {};
    msg.command = DB_CMD_REGISTER_USER;
    msg.requestId = m_nextRequestId.fetch_add(1);
    copyString(msg.payload.user.name, sizeof(msg.payload.user.name), body["user"].get<std::string>());
    copyString(msg.payload.user.password, sizeof(msg.payload.user.password), body["pass"].get<std::string>());

    SessionData session{};
    if (!enqueueDbRequest(msg, c->id, 200, 400, false, session)) {
        sendError(c, 500, "Database unavailable");
        return;
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





void dWebServer::handleDashboard(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    // DatabaseMsg msg = {}; msg.command = DB_CMD_GET_DASHBOARD; if (!m_mqToDatabase.send(&msg, sizeof(msg))) { sendError(c, 500, "Database unavailable"); return; } DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { nlohmann::json data = nlohmann::json::parse(resp.jsonData); data["isAdmin"] = (session.accessLevel >= 1); sendJson(c, 200, data); } else { sendError(c, 500, "Failed to get dashboard data"); }
    DatabaseMsg msg = {};
    msg.command = DB_CMD_GET_DASHBOARD;
    msg.requestId = m_nextRequestId.fetch_add(1);

    if (!enqueueDbRequest(msg, c->id, 200, 500, true, session)) {
        sendError(c, 500, "Database unavailable");
        return;
    }
}

void dWebServer::handleSensors(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    // DatabaseMsg msg = {}; msg.command = DB_CMD_GET_SENSORS; m_mqToDatabase.send(&msg, sizeof(msg)); DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 500, "Failed to get sensors"); }
    DatabaseMsg msg = {};
    msg.command = DB_CMD_GET_SENSORS;
    msg.requestId = m_nextRequestId.fetch_add(1);

    if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
        sendError(c, 500, "Database unavailable");
        return;
    }
}

void dWebServer::handleActuators(struct mg_connection* c, struct mg_http_message* hm) {
    SessionData session;
    if (!validateSession(hm, session)) {
        sendError(c, 401, "Not authenticated");
        return;
    }

    // DatabaseMsg msg = {}; msg.command = DB_CMD_GET_ACTUATORS; m_mqToDatabase.send(&msg, sizeof(msg)); DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 500, "Failed to get actuators"); }
    DatabaseMsg msg = {};
    msg.command = DB_CMD_GET_ACTUATORS;
    msg.requestId = m_nextRequestId.fetch_add(1);

    if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
        sendError(c, 500, "Database unavailable");
        return;
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

    // DatabaseMsg msg = {}; msg.command = DB_CMD_FILTER_LOGS; strncpy(msg.payload.logFilter.timeRange, body["timeRange"].get<std::string>().c_str(), 15); strncpy(msg.payload.logFilter.logType, body["logType"].get<std::string>().c_str(), 31); m_mqToDatabase.send(&msg, sizeof(msg)); DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 500, "Failed to filter logs"); }
    if (!body.contains("timeRange") || !body.contains("logType")) {
        sendError(c, 400, "Missing filter fields");
        return;
    }

    DatabaseMsg msg = {};
    msg.command = DB_CMD_FILTER_LOGS;
    msg.requestId = m_nextRequestId.fetch_add(1);
    copyString(msg.payload.logFilter.timeRange, sizeof(msg.payload.logFilter.timeRange), body["timeRange"].get<std::string>());
    copyString(msg.payload.logFilter.logType, sizeof(msg.payload.logFilter.logType), body["logType"].get<std::string>());

    if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
        sendError(c, 500, "Database unavailable");
        return;
    }
}





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

    
    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
        // DatabaseMsg msg = {}; msg.command = DB_CMD_GET_USERS; m_mqToDatabase.send(&msg, sizeof(msg)); DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 500, "Failed to get users"); }
        DatabaseMsg msg = {};
        msg.command = DB_CMD_GET_USERS;
        msg.requestId = m_nextRequestId.fetch_add(1);
        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
        }
    } else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        if (!body.contains("name")) {
            sendError(c, 400, "Missing name");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_CREATE_USER;
        msg.requestId = m_nextRequestId.fetch_add(1);
        copyString(msg.payload.user.name, sizeof(msg.payload.user.name), body["name"].get<std::string>());
        if (body.contains("rfid")) {
            copyString(msg.payload.user.rfid, sizeof(msg.payload.user.rfid), body["rfid"].get<std::string>());
        } else {
            msg.payload.user.rfid[0] = '\0';
        }
        msg.payload.user.fingerprintID = body.value("fingerprint", 0);
        msg.payload.user.accessLevel = 0;

        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
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

    
    std::string uri(hm->uri.buf, hm->uri.len);
    std::string idStr = uri.substr(11);
    for (char ch : idStr) {
        if (ch < '0' || ch > '9') {
            sendError(c, 400, "Invalid user id");
            return;
        }
    }
    int userId = std::stoi(idStr);

    
    if (mg_strcmp(hm->method, mg_str("PUT")) == 0) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        // DatabaseMsg msg = {}; msg.command = DB_CMD_MODIFY_USER; msg.payload.user.userID = userId; strncpy(msg.payload.user.name, body["name"].get<std::string>().c_str(), 63); strncpy(msg.payload.user.rfid, body["rfid"].get<std::string>().c_str(), 10); msg.payload.user.fingerprintID = body.value("fingerprint", 0); std::string accessStr = body["access"].get<std::string>(); msg.payload.user.accessLevel = (accessStr == "Room/Vault") ? 2 : 1; msg.payload.user.password[0] = '\0'; m_mqToDatabase.send(&msg, sizeof(msg)); DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 500, "Failed to modify user"); }
        if (!body.contains("name")) {
            sendError(c, 400, "Missing name");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_MODIFY_USER;
        msg.requestId = m_nextRequestId.fetch_add(1);
        msg.payload.user.userID = userId;
        copyString(msg.payload.user.name, sizeof(msg.payload.user.name), body["name"].get<std::string>());
        if (body.contains("rfid")) {
            copyString(msg.payload.user.rfid, sizeof(msg.payload.user.rfid), body["rfid"].get<std::string>());
        } else {
            msg.payload.user.rfid[0] = '\0';
        }
        msg.payload.user.fingerprintID = body.value("fingerprint", 0);
        msg.payload.user.accessLevel = 0;
        msg.payload.user.password[0] = '\0';

        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
        }
    }
    
    else if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
        DatabaseMsg msg = {};
        msg.command = DB_CMD_REMOVE_USER;
        msg.requestId = m_nextRequestId.fetch_add(1);
        msg.payload.userId = static_cast<uint32_t>(userId);

        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
        }
    }
}





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

    
    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
        // DatabaseMsg msg = {}; msg.command = DB_CMD_GET_ASSETS; m_mqToDatabase.send(&msg, sizeof(msg)); DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 500, "Failed to get assets"); }
        DatabaseMsg msg = {};
        msg.command = DB_CMD_GET_ASSETS;
        msg.requestId = m_nextRequestId.fetch_add(1);
        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
        }
    } else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        if (!body.contains("name") || !body.contains("tag")) {
            sendError(c, 400, "Missing fields");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_CREATE_ASSET;
        msg.requestId = m_nextRequestId.fetch_add(1);
        copyString(msg.payload.asset.name, sizeof(msg.payload.asset.name), body["name"].get<std::string>());
        copyString(msg.payload.asset.tag, sizeof(msg.payload.asset.tag), body["tag"].get<std::string>());

        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
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

    
    std::string uri(hm->uri.buf, hm->uri.len);
    std::string tag = uri.substr(12);  

    
    if (mg_strcmp(hm->method, mg_str("PUT")) == 0) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        // DatabaseMsg msg = {}; msg.command = DB_CMD_MODIFY_ASSET; strncpy(msg.payload.asset.name, body["name"].get<std::string>().c_str(), 63); strncpy(msg.payload.asset.tag, tag.c_str(), 31); strncpy(msg.payload.asset.state, body["state"].get<std::string>().c_str(), 15); m_mqToDatabase.send(&msg, sizeof(msg)); DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 500, "Failed to modify asset"); }
        if (!body.contains("name")) {
            sendError(c, 400, "Missing name");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_MODIFY_ASSET;
        msg.requestId = m_nextRequestId.fetch_add(1);
        copyString(msg.payload.asset.name, sizeof(msg.payload.asset.name), body["name"].get<std::string>());
        copyString(msg.payload.asset.tag, sizeof(msg.payload.asset.tag), tag);

        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
        }
    }
    
    else if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
        DatabaseMsg msg = {};
        msg.command = DB_CMD_REMOVE_ASSET;
        msg.requestId = m_nextRequestId.fetch_add(1);
        copyString(msg.payload.asset.tag, sizeof(msg.payload.asset.tag), tag);

        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
        }
    }
}





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

    
    if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
        // DatabaseMsg msg = {}; msg.command = DB_CMD_GET_SETTINGS; m_mqToDatabase.send(&msg, sizeof(msg)); DbWebResponse resp = {}; ssize_t bytes = m_mqFromDatabase.timedReceive(&resp, sizeof(resp), 2); if (bytes > 0 && resp.success) { sendJson(c, 200, nlohmann::json::parse(resp.jsonData)); } else { sendError(c, 500, "Failed to get settings"); }
        DatabaseMsg msg = {};
        msg.command = DB_CMD_GET_SETTINGS;
        msg.requestId = m_nextRequestId.fetch_add(1);
        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
        }
    } else if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(std::string(hm->body.buf, hm->body.len));
        } catch (...) {
            sendError(c, 400, "Invalid JSON");
            return;
        }

        if (!body.contains("tempLimit") || !body.contains("sampleTime")) {
            sendError(c, 400, "Missing settings");
            return;
        }

        DatabaseMsg msg = {};
        msg.command = DB_CMD_UPDATE_SETTINGS;
        msg.requestId = m_nextRequestId.fetch_add(1);
        msg.payload.settings.tempThreshold = body["tempLimit"];
        msg.payload.settings.samplingInterval = body["sampleTime"].get<int>() * 60;

        if (!enqueueDbRequest(msg, c->id, 200, 500, false, session)) {
            sendError(c, 500, "Database unavailable");
            return;
        }
    }
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
