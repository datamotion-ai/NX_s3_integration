#include "ServerManager.h"
#include <curl/curl.h>

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

void ServerManager::postEvent(std::string msg, std::string source)
{
    DEBUGLOG("ServerManager::ServerManager",msg);
    CURL* curl = curl_easy_init();
    if (!curl) 
    {
        ERRORLOG("Error initializing libcurl.");
    }
    else
    {
        std::string url = "https://" + m_host +  "/api/createEvent";
        DEBUGLOG("url",url);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L); 

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string runtimeGuidHeader = "x-runtime-guid: " + m_token;
        headers = curl_slist_append(headers, runtimeGuidHeader.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        std::string data = "{\"source\":\""+ source + "\",\"description\":\"" + msg + "\"}";
        DEBUGLOG("data",data);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nx_spl::aux::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res1 = curl_easy_perform(curl);
        if (res1 != CURLE_OK) 
        {
            ERRORLOG("curl_easy_perform() failed: ",curl_easy_strerror(res1));
        } 
        curl_slist_free_all(headers);
        curl_easy_reset(curl);
    }
}

ServerManager::ServerManager():
m_licenceAvailable(false),
m_eventActive(false),
m_serverInitialize(false)
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
            m_host = root["host"].asString();
            m_user = root["username"].asString();
            m_password = root["password"].asString();
            ret = !m_host.empty() && !m_user.empty() && !m_password.empty() ;
        }
    }
    return ret;
}

bool ServerManager::createSession(const std::string &host, const std::string &usr, const std::string &pswd, std::string &token) const
{
    DEBUGLOG("ServerManager::createSession",host,usr,pswd);
    bool ret = false;
    CURL* curl = curl_easy_init();
    if (!curl) 
    {
        ERRORLOG("Error initializing libcurl.");
    }
    else
    {
        std::string url = "https://" + host +  "/rest/v2/login/sessions";
        DEBUGLOG("url",url);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L); 

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        std::string data = "{\"username\":\""+ usr + "\",\"password\":\"" + pswd + "\",\"setCookie\":true}";
        DEBUGLOG("data",data);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nx_spl::aux::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res1 = curl_easy_perform(curl);
        if (res1 != CURLE_OK) 
        {
            ERRORLOG("curl_easy_perform() failed: ",curl_easy_strerror(res1));
        } 
        else 
        {
            INFOLOG("------->",response);
            Json::Value root;
            Json::Reader reader;
            if(reader.parse(response, root) && (root.isMember("token")))
            {
                token = root["token"].asString();
                ret = true;
            }
            else
            {
                ERRORLOG("Invalid json resopense",response);
            }
            
        }
        curl_slist_free_all(headers);
        curl_easy_reset(curl);
    }
    return ret;
}

bool ServerManager::isSessionExpired(const std::string &host, std::string &token) const
{
    DEBUGLOG("S3StorageFactory::isSessionExpired",host,token);
    bool ret = true;
    CURL* curl = curl_easy_init();
    if (!curl) 
    {
        ERRORLOG("Error initializing libcurl.");
    }
    else
    {
        std::string url = "https://" + host +  "/rest/v2/login/sessions/" + token;
        DEBUGLOG("url",url);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L); 

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "accept: application/json");
        std::string runtimeGuidHeader = "x-runtime-guid: " + token;
        headers = curl_slist_append(headers, runtimeGuidHeader.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nx_spl::aux::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res1 = curl_easy_perform(curl);
        if (res1 != CURLE_OK) {
            ERRORLOG("curl_easy_perform() failed: ",curl_easy_strerror(res1));
        } else {
            INFOLOG("------->",response);
            Json::Value root;
            Json::Reader reader;
            if(reader.parse(response, root))
            {
                std::string tokenExpiration = root["expiresInS"].asString();
                if(!tokenExpiration.empty())
                    ret = false;
            }
            else
            {
                ERRORLOG("Invalid json resopense",response);
            }
            
        }
        curl_slist_free_all(headers);
        curl_easy_reset(curl);
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
        std::string url = "https://" + m_host + "/rest/v2/licenses";
        std::string acceptHeader = "accept: application/json";
        std::string runtimeGuidHeader = "x-runtime-guid: " + m_token;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, acceptHeader.c_str());
        headers = curl_slist_append(headers, runtimeGuidHeader.c_str());
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
            if(headerResponse.find("Nx Witness") != std::string::npos)
            {
                Json::Value jsonData;
                Json::CharReaderBuilder jsonReaderBuilder;
                std::istringstream jsonStream(response);
                bool validJson = Json::parseFromStream(jsonReaderBuilder, jsonStream, &jsonData, nullptr);

                if(validJson && jsonData.isArray())
                {
                    for (const auto& jsonObject : jsonData) 
                    {
                        if(jsonObject.isMember("licenseBlock"))
                        {
                            std::istringstream iss(jsonObject["licenseBlock"].asString());
                            std::vector<std::string> lines;
                            std::string line;

                            while (std::getline(iss, line, '\n')) 
                            {
                                lines.push_back(line);
                            }

                            Json::Value licenseObject;
                            
                            for (const auto& line : lines) 
                            {
                                size_t equalPos = line.find('=');
                                if (equalPos != std::string::npos) 
                                {
                                    std::string key = line.substr(0, equalPos);
                                    std::string value = line.substr(equalPos + 1);
                                    licenseObject[key] = value;
                                }
                            }

                            if (licenseObject.isMember("EXPIRATION")) 
                            {
                                std::string expirationValue = licenseObject["EXPIRATION"].asString();
                                INFOLOG("---EXPIRATION---->",expirationValue);

                                std::time_t rawTime;
                                std::tm* timeInfo;
                                char buffer[80];

                                std::time(&rawTime);
                                timeInfo = std::localtime(&rawTime);

                                std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeInfo);
                                std::string timestamp(buffer);

                                if(timestamp <= expirationValue)
                                {
                                    INFOLOG("***Valid license***");
                                    licenseAvailable = true;
                                    break;
                                }
                                else
                                {
                                    INFOLOG("Invalid license");
                                    licenseAvailable = false;
                                }
                            } 
                            else 
                            {
                                ERRORLOG("Key 'EXPIRATION' not found in the JSON object");
                                licenseAvailable = false;
                            }
                        }
                    }
                }
                else
                {
                    ERRORLOG("Invalid Json response",response);
                }
            }
            else
            {
                ERRORLOG("Oops!!, Invalid Server!!",headerResponse);
            }
        }
        curl_slist_free_all(headers);
        curl_easy_reset(curl);
    }
    return licenseAvailable;
}

void ServerManager::updateLicenseDetail()
{
    DEBUGLOG("ServerManager::updateLicenseDetail");
    m_serverInitialize = true;
    if(loadServerCredential())
    {
        if(m_token.empty() || isSessionExpired(m_host, m_token))
        {
            m_token.clear();
            if(!createSession(m_host,m_user,m_password,m_token))
            {
                ERRORLOG("Failed to create session!!");
                m_licenceAvailable = false;
                m_timer.setInterval(ONE_MINUTE);
                return;
            }
        }
        if(!m_token.empty())
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
                ServerManager::getInstance()->postEvent("License Expired!!,Update License Details!!","");
            }
        }
        else
        {
            ERRORLOG("Token is Empty!!");
        }
    }
}
