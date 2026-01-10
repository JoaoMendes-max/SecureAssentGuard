#ifndef DWEBSERVER_H
#define DWEBSERVER_H

#include <string>
#include <map>
#include <mutex>
#include "mongoose.h"
#include "nlohmann/json.hpp"
#include "C_Mqueue.h"
#include "SharedTypes.h"

struct SessionData {
    uint32_t userId;
    std::string username;
    uint32_t accessLevel;
    time_t expires;
};

class dWebServer {
private:
    struct mg_mgr m_mgr;
    C_Mqueue& m_mqToDatabase;
    C_Mqueue& m_mqFromDatabase;
    std::map<std::string, SessionData> m_sessions;
    std::mutex m_sessionMutex;
    int m_port;
    bool m_running;

public:
    dWebServer(C_Mqueue& toDb, C_Mqueue& fromDb, int port = 8080);
    ~dWebServer();

    bool start();
    void stop();
    void run();

private:
    // ✅ 3 PARÂMETROS (não 4!)
    static void eventHandler(struct mg_connection* c, int ev, void* ev_data);

    void handleLogin(struct mg_connection* c, struct mg_http_message* hm);
    void handleLogout(struct mg_connection* c, struct mg_http_message* hm);
    void handleDashboard(struct mg_connection* c, struct mg_http_message* hm);
    void handleSensors(struct mg_connection* c, struct mg_http_message* hm);
    void handleActuators(struct mg_connection* c, struct mg_http_message* hm);

    nlohmann::json queryDatabase(e_DbCommand cmd, const nlohmann::json& params = {});

    std::string generateToken();
    bool validateSession(struct mg_http_message* hm, SessionData& outSession);
    void cleanExpiredSessions();

    void sendJson(struct mg_connection* c, int statusCode, const nlohmann::json& data);
    void sendError(struct mg_connection* c, int statusCode, const std::string& message);

    // Helper para comparar URIs
    static bool matchUri(const struct mg_str* uri, const char* pattern);
};

#endif