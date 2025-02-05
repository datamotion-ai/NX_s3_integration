#ifndef SERVERMANAGER_H
#define SERVERMANAGER_H

#include "common.hpp"
#include "timer.h"

class ServerManager
{
    public:
    static ServerManager* getInstance();
    static void deleteInstance();
    bool isLicenseAvailable() const;
    bool isServerIntialize() const;
    void postEvent(std::string msg, std::string source);
    int64_t getLocalBufferSize();
    int getMaxThread();

    private:
    ServerManager();
    ~ServerManager();
    void updateLicenseDetail();
    bool loadServerCredential();
    bool createSession(const std::string &host, const std::string &usr, const std::string &pswd, std::string& token) const;
    bool isSessionExpired(const std::string &host,std::string& token) const;
    bool verifyLicense();
    std::string hex_to_string(const std::string hex_input);
    std::string decrypt_string(const std::string input);
    bool verifyOEM(const std::string headerResponse);
    bool registerSDK();
    std::string getLicenseKey();
    void registerPlugin();

    private:
    static ServerManager* m_serverPtr;
    bool m_licenceAvailable;
    bool m_serverInitialize;
    bool m_eventActive;
    bool m_pluginRegistered;
    int  m_maxThread;
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_serverOEM;
    int64_t m_local_buffer_size;
    std::vector<std::string> m_OEM;
    std::string m_token;
    Timer m_timer;
};

#endif //SERVERMANAGER_H