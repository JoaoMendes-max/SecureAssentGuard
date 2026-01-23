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
    uint32_t accessLevel;  // 0=Viewer, 1=Room, 2=Room+Vault
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
    static void eventHandler(struct mg_connection* c, int ev, void* ev_data);

    // Autenticação
    void handleLogin(struct mg_connection* c, struct mg_http_message* hm);
    void handleRegister(struct mg_connection* c, struct mg_http_message* hm);
    void handleLogout(struct mg_connection* c, struct mg_http_message* hm);

    // Visualização (todos os users)
    void handleDashboard(struct mg_connection* c, struct mg_http_message* hm);
    void handleSensors(struct mg_connection* c, struct mg_http_message* hm);
    void handleActuators(struct mg_connection* c, struct mg_http_message* hm);
    void handleLogsFilter(struct mg_connection* c, struct mg_http_message* hm);

    // Admin apenas
    void handleUsers(struct mg_connection* c, struct mg_http_message* hm);
    void handleUsersById(struct mg_connection* c, struct mg_http_message* hm);
    void handleAssets(struct mg_connection* c, struct mg_http_message* hm);
    void handleAssetsById(struct mg_connection* c, struct mg_http_message* hm);
    void handleSettings(struct mg_connection* c, struct mg_http_message* hm);

    // Utilitários
    std::string generateToken();
    bool validateSession(struct mg_http_message* hm, SessionData& outSession);
    void cleanExpiredSessions();

    void sendJson(struct mg_connection* c, int statusCode, const nlohmann::json& data);
    void sendError(struct mg_connection* c, int statusCode, const std::string& message);

    static bool matchUri(const struct mg_str* uri, const char* pattern);
    static bool matchPrefix(const struct mg_str* uri, const char* pattern);
};

#endif