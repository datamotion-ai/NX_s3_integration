#ifndef SERVERMANAGER_H
#define SERVERMANAGER_H

#include <cstring>
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <cassert>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <cstdio>
#include <json/json.h>
#include <vector>
#include <memory>
#include <stdint.h>
#include <mutex>

#if defined(__linux__) || defined(__APPLE__)
#   include <sys/stat.h>
#   include <sys/utsname.h>
#elif defined (_WIN32)
#   include <Windows.h>
#   include <winternl.h>
#endif

#if __has_include(<filesystem>)
  #include <filesystem>
  namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
  #include <experimental/filesystem> 
  namespace fs = std::experimental::filesystem;
#else
  error "Missing the <filesystem> header."
#endif

#include "storage/third_party_storage.h"
#include "daily_loger.hpp"
#include "timer.h"

#define S3_CONFIG_FILE "s3.config"
#define DEFAULT_1_GB  1024 * 1024 * 1024 //1 GB
#define S3_DEFAULT_TOTAL_SPACE 1024LL * DEFAULT_1_GB //100GB

#define LICENSE_CONFIG_FILE "license.config"
#define ONE_MINUTE 60 * 1000
#define FIVE_MINUTE 5 * ONE_MINUTE
#define TEN_MINUTE 10 * ONE_MINUTE
#define MAX_FILE_WRITE_COUNT 1
#define VERSION "beta-1.0.3.1"

namespace nx_spl
{
    namespace aux
    { 
        size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output); 

        size_t headerCallback(char* buffer, size_t size, size_t nitems, std::string* output);

    }
}

class ServerManager
{
    public:
    static ServerManager* getInstance();
    static void deleteInstance();
    bool isLicenseAvailable() const;
    bool isServerIntialize() const;
    void postEvent(std::string msg, std::string source);
    int64_t getLocalBufferSize();

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
    std::string getLicenseKey();
    void registerPlugin();
    std::string getServerID();

    private:
    static ServerManager* m_serverPtr;
    bool m_licenceAvailable;
    bool m_serverInitialize;
    bool m_eventActive;
    bool m_pluginRegistered;
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_serverOEM;
    int64_t m_local_buffer_size;
    std::vector<std::string> m_OEM;
    std::string m_token;
    std::string m_dmLicenceKey;
    Timer m_timer;
};

#endif //SERVERMANAGER_H