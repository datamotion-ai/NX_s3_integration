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

    private:
    ServerManager();
    ~ServerManager();
    void updateLicenseDetail();
    bool loadServerCredential();
    bool createSession(const std::string &host, const std::string &usr, const std::string &pswd, std::string& token) const;
    bool isSessionExpired(const std::string &host,std::string& token) const;
    bool verifyLicense();

    private:
    static ServerManager* m_serverPtr;
    bool m_licenceAvailable;
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_token;
    Timer m_timer;
};

#endif //SERVERMANAGER_H