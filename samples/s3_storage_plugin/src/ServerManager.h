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
    uint64_t getLocalBufferSize();
    int getMaxThread();

    private:
    ServerManager();
    ~ServerManager();
    void updateLicenseDetail();
    bool loadServerCredential();
    bool verifyLicense();
    std::string getLicenseKey();
    void registerPlugin();

    private:
    static ServerManager* m_serverPtr;
    bool m_licenceAvailable;
    bool m_serverInitialize;
    bool m_pluginRegistered;
    std::string m_host;
    std::string m_serverOEM;
    uint64_t m_local_buffer_size;
    int  m_maxThread;
    Timer m_timer;
};

#endif //SERVERMANAGER_H