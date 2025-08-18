#include "ServerManager.h"
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>

ServerManager* ServerManager::m_serverPtr = nullptr;

ServerManager *ServerManager::getInstance()
{
    DEBUGLOG("ServerManager::getInstance");
    if(m_serverPtr == nullptr)
    {
        m_serverPtr = new ServerManager();
        m_serverPtr->m_timer.start(m_serverPtr, &ServerManager::updateLicenseDetail,ONE_MINUTE);
    }
    return m_serverPtr;
}

void ServerManager::deleteInstance()
{
    DEBUGLOG("ServerManager::deleteInstance");
    if(m_serverPtr != nullptr)
    {
        m_serverPtr->m_timer.stop();
        delete m_serverPtr;
        m_serverPtr = nullptr;
    }
}

bool ServerManager::isLicenseAvailable() const
{
    DEBUGLOG("ServerManager::isLicenseAvailable");
    return m_licenceAvailable;
}

bool ServerManager::isServerIntialize() const
{
    DEBUGLOG("ServerManager::isServerIntialize");
    return m_serverInitialize;
}

ServerManager::ServerManager():
m_licenceAvailable(false),
m_serverInitialize(false),
m_pluginRegistered(false),
m_local_buffer_size(1),
m_maxThread(10)
{
    DEBUGLOG("ServerManager::ServerManager");
}

ServerManager::~ServerManager()
{
    DEBUGLOG("ServerManager::~ServerManager");

}

bool ServerManager::loadServerCredential()
{
    DEBUGLOG("ServerManager::loadServerCredential");
    bool ret = false;
    Json::Reader reader;
    std::ifstream jsonFile(LICENSE_CONFIG_FILE);
    if (!jsonFile.is_open()) 
    {
        ERRORLOG("Error opening config file:",LICENSE_CONFIG_FILE);
        m_licenceAvailable = false;
        m_timer.setInterval(ONE_MINUTE);
        ret = ret;
    }
    else
    {
        Json::Value root;
        if (!reader.parse(jsonFile, root)) 
        {
            ERRORLOG("Error parsing JSON from file:",reader.getFormattedErrorMessages());
            m_licenceAvailable = false;
            m_timer.setInterval(ONE_MINUTE);
            ret = ret;
        }
        else
        {
            if(root.isMember("host"))
                m_host = root["host"].asString();
            if(root.isMember("pluginRegistered"))
                m_pluginRegistered = root["pluginRegistered"].asBool();
            if(root.isMember("local_buffer")){
                m_local_buffer_size = root["local_buffer"].asInt64();
                m_local_buffer_size *= DEFAULT_1_GB ;
            }
            INFOLOG("Local Buffer Size",m_local_buffer_size);
            if(root.isMember("max_parallel_upload")) {
                int log_level = root["log_level"].asInt64();
                nx_spl::aux::DailyLogger::SetVerbosity(nx_spl::aux::DailyLogger::LogPriority(log_level));
            }
            if(root.isMember("log_max")) {
                int log_max = root["log_max"].asInt64();
                nx_spl::aux::DailyLogger::SetMaxLogFileCount(log_max);
            }
            if(root.isMember("max_parallel_upload"))
                m_maxThread =  root["max_parallel_upload"].asUInt();
            ret = !m_host.empty();
        }
    }
    return ret;
}

bool ServerManager::verifyLicense()
{
    DEBUGLOG("ServerManager::verifyLicense");
    bool licenseAvailable = false;
    CURL* curl = curl_easy_init();
    if (!curl) 
    {
        ERRORLOG("Error initializing libcurl.");
        licenseAvailable = false;
    }
    else
    {
        std::string url = "https://" + m_host + "/rest/v3/system/info";
        std::string acceptHeader = "accept: application/json";
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, acceptHeader.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nx_spl::aux::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        std::string headerResponse;
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, nx_spl::aux::headerCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerResponse);

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L); 

        CURLcode res1 = curl_easy_perform(curl);
        if (res1 != CURLE_OK) 
        {
            ERRORLOG("curl_easy_perform() failed: ",curl_easy_strerror(res1));
            licenseAvailable = false;
        } 
        else 
        {
            INFOLOG("------->",response);
            INFOLOG("------->",headerResponse);
            m_serverOEM = headerResponse;
            m_serverOEM.erase(std::remove(m_serverOEM.begin(), m_serverOEM.end(), '\n'), m_serverOEM.end());
            Json::Value jsonData;
            Json::CharReaderBuilder jsonReaderBuilder;
            std::istringstream jsonStream(response);
            bool validJson = Json::parseFromStream(jsonReaderBuilder, jsonStream, &jsonData, nullptr);

            if(validJson)
            {
                if (jsonData.isMember("organizationId")) 
                {
                    INFOLOG("***Valid license***");
                    licenseAvailable = true;
                } 
                else 
                {
                    ERRORLOG("Key 'organizationId' not found in the JSON object");
                    licenseAvailable = false;
                }
            }
            else
            {
                ERRORLOG("Invalid Json response",response);
            }
        }
        curl_slist_free_all(headers);
        curl_easy_reset(curl);
    }
    return licenseAvailable;
}

std::string ServerManager::getLicenseKey()
{
    DEBUGLOG("ServerManager::getLicenseKey");
    std::string key;
    CURL* curl = curl_easy_init();
    if (!curl) 
    {
        ERRORLOG("Error initializing libcurl.");
        return key;
    }
    else
    {
        curl_easy_setopt(curl, CURLOPT_URL, "https://xs29e9rn48.execute-api.us-east-1.amazonaws.com/prod/key");
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "accept: application/json");
        headers = curl_slist_append(headers, "x-api-key:1VBLNAafWU9oGO7RnibXaqyHqOjEvDdaQ6Uukfw0");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nx_spl::aux::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L); 

        CURLcode res1 = curl_easy_perform(curl);
        if (res1 != CURLE_OK) 
        {
            ERRORLOG("curl_easy_perform() failed: ",curl_easy_strerror(res1));
        } 
        else 
        {
            INFOLOG("------->",response);
            Json::Value jsonData;
            Json::CharReaderBuilder jsonReaderBuilder;
            std::istringstream jsonStream(response);
            bool validJson = Json::parseFromStream(jsonReaderBuilder, jsonStream, &jsonData, nullptr);

            if(validJson && jsonData.isMember("key"))
            {
                key = jsonData["key"].asString();
                INFOLOG("--key----->",key);
            }
        }
    }

    return key;
}

void ServerManager::registerPlugin()
{
    DEBUGLOG("ServerManager::registerPlugin");
    std::string licenseKey = getLicenseKey();
    if(licenseKey.empty() == false)
    {

        std::string os_name;
        std::string os_version;
        std::string architecture = "x64";
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* localTime = std::localtime(&now_c);
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::stringstream ss;
        ss << std::put_time(localTime, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << milliseconds.count();
        std::string local_time = ss.str();
        ss.clear();

    #if defined (_WIN32)

        os_name = "Windows";
        NTSTATUS(WINAPI *RtlGetVersion)(LPOSVERSIONINFOEXW);
        OSVERSIONINFOEXW osInfo;

        *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

        if (NULL != RtlGetVersion)
        {
            osInfo.dwOSVersionInfoSize = sizeof(osInfo);
            RtlGetVersion(&osInfo);
        }
        os_version = std::to_string(osInfo.dwMajorVersion) + "."  + std::to_string( osInfo.dwMinorVersion) + "." + std::to_string( osInfo.dwBuildNumber);
        
    #else
        os_name = "LINUX";
        struct utsname unameData;
        uname(&unameData);
        os_version = unameData.release;
    #endif

        CURL* curl = curl_easy_init();
        if (!curl) 
        {
            ERRORLOG("Error initializing libcurl.");
        }
        else
        {
            std::string url = "https://xs29e9rn48.execute-api.us-east-1.amazonaws.com/prod/register";
            DEBUGLOG("url",url);
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);

            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L); 

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "accept: application/json");
            headers = curl_slist_append(headers, "x-api-key:1VBLNAafWU9oGO7RnibXaqyHqOjEvDdaQ6Uukfw0");
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            size_t startPos = m_serverOEM.find('/');
            size_t endPos = m_serverOEM.find(' ', startPos);
            m_serverOEM = m_serverOEM.substr(0, endPos);

            std::string data = "{"
            "\"license_key\":\"" + licenseKey + "\","
            "\"software_name\":\"" + "Wasabi_storage_sdk" + "\","
            "\"software_version\":\"" + VERSION + "\","
            "\"os_name\":\"" + os_name + "\","
            "\"os_version\":\"" + os_version + "\","
            "\"architecture\":\"" + architecture + "\","
            "\"license_type\":\"" + "Trial" + "\","
            "\"company\":\"" + "NX" + "\","
            "\"software_oem\":\"" + m_serverOEM + "\","
            "\"software_localtime\":\"" + local_time + "\""
            "}";

            INFOLOG("data",data);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

            std::string response;
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nx_spl::aux::WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

            CURLcode res1 = curl_easy_perform(curl);
            if (res1 != CURLE_OK) 
            {
                ERRORLOG("Failed to register plugin: ",curl_easy_strerror(res1));
            } 
            else 
            {
                INFOLOG("------->",response);
                Json::Value jsonData;
                Json::CharReaderBuilder jsonReaderBuilder;
                std::istringstream jsonStream(response);
                bool validJson = Json::parseFromStream(jsonReaderBuilder, jsonStream, &jsonData, nullptr);

                if(validJson && jsonData.isMember("success") && (jsonData["success"].asBool() == true))
                {
                    INFOLOG("successfully registered plugin!!");
                    m_pluginRegistered = true;
                    Json::Reader reader;
                    std::ifstream jsonFile(LICENSE_CONFIG_FILE);
                    if (!jsonFile.is_open()) 
                    {
                        ERRORLOG("Error opening config file:",LICENSE_CONFIG_FILE);
                    }
                    else
                    {
                        Json::Value root;
                        if (!reader.parse(jsonFile, root)) 
                        {
                            ERRORLOG("Error parsing JSON from file:",reader.getFormattedErrorMessages());
                        }
                        else
                        {
                            std::ofstream outputFile(LICENSE_CONFIG_FILE);
                            if (!outputFile.is_open()) {
                                ERRORLOG("Error opening JSON file:",LICENSE_CONFIG_FILE);
                            }
                            else
                            {
                                root["pluginRegistered"] = m_pluginRegistered;
                                Json::StyledStreamWriter writer;
                                writer.write(outputFile, root);
                                outputFile.close();
                            }
                        }
                    }
                }
                else
                {
                    INFOLOG("Failed to register plugin, invalid json response!!");
                }
            }
            curl_slist_free_all(headers);
            curl_easy_reset(curl);
        }
    }
}

int64_t ServerManager::getLocalBufferSize()
{
    DEBUGLOG("ServerManager::getLocalBufferSize");
    return m_local_buffer_size;
}

int ServerManager::getMaxThread()
{
    DEBUGLOG("ServerManager::getMaxThread");
    return m_maxThread;
}

void ServerManager::updateLicenseDetail()
{
    DEBUGLOG("ServerManager::updateLicenseDetail");
    try
    {
        m_serverInitialize = true;
        if(loadServerCredential())
        {
            if(verifyLicense())
            {
                m_licenceAvailable = true;
                m_timer.setInterval(TEN_MINUTE);
            }
            else
            {
                m_licenceAvailable = false;
                m_timer.setInterval(ONE_MINUTE);
            }
            if(m_pluginRegistered == false)
                registerPlugin();
        }
    }
    catch (...)
    {
        ERRORLOG("exception Error");
    }
    
}
