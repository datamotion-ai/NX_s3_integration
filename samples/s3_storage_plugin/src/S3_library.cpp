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
#include <filesystem>
#include <map>
#include "common.hpp"
#include "S3_library.h"
#include "daily_loger.hpp"
#include "ServerManager.h"
#include "ClearMemoryManager.h"

namespace nx_spl
{

    // S3StorageFactory

    std::mutex nx_spl::S3StorageFactory::m_mutex;
    // bool  g_licenseAvailable = false;

    nx_spl::S3StorageFactory::S3StorageFactory()
    {
        INFOLOG("S3StorageFactory::S3StorageFactory");
        m_options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Fatal;
        Aws::InitAPI(m_options);
        std::srand((unsigned int) time(0));
    }

    nx_spl::S3StorageFactory::~S3StorageFactory()
    {
        INFOLOG("S3StorageFactory::~S3StorageFactory");
        Aws::ShutdownAPI(m_options);
        ClearMemoryManager::deleteInstance();
        ServerManager::deleteInstance();
        nx_spl::aux::DailyLogger::Dinitialize();
    }

    const char** STORAGE_METHOD_CALL nx_spl::S3StorageFactory::findAvailable() const
    {
        DEBUGLOG("S3StorageFactory::findAvailable");
        assert(false);
        return nullptr;
    }

    Storage *STORAGE_METHOD_CALL nx_spl::S3StorageFactory::createStorage(const char *url, int *ecode)
    {
        INFOLOG("S3StorageFactory::createStorage",url);
        Storage* ret = nullptr;
        if(ecode != nullptr)
            *ecode = error::NoError;
        try
        {
            ret = new S3Storage(url);
        }
        catch (const std::bad_alloc& e)
        {
            ERRORLOG(e.what());
            if (ecode)
                *ecode = error::UnknownError;
            return nullptr;
        }
        catch (const aux::NetworkException& e)
        {
            ERRORLOG(e.what());
            if (ecode)
                *ecode = error::StorageUnavailable;
            return nullptr;
        }
        catch (const aux::BadUrlException& e)
        {
            ERRORLOG(e.what());
            if (ecode)
                *ecode = error::UrlNotExists;
            return nullptr;
        }
        catch (const aux::ConnectException& e)
        {
            ERRORLOG(e.what());
            if (ecode)
                *ecode = error::StorageUnavailable;
            return nullptr;
        }
        catch (const std::exception& e)
        {
            ERRORLOG("Exception Error:", e.what());
            if (ecode)
                *ecode = error::UnknownError;
            return nullptr;
        }
        return ret;
    }


    const char *STORAGE_METHOD_CALL nx_spl::S3StorageFactory::storageType() const
    {
        DEBUGLOG("S3StorageFactory::storageType");
        static bool pluginIntegrated = false;
        if(pluginIntegrated == false)
        {
            pluginIntegrated = true;
            return "s3";
        }
        else
        {
            if(ServerManager::getInstance()->isLicenseAvailable())
            {
                return "s3";
            }
            else
            {
                return "unknown";
            } 
            
        }
    }

    const char *nx_spl::S3StorageFactory::lastErrorMessage(int ecode) const
    {
        DEBUGLOG("S3StorageFactory::lastErrorMessage",ecode);
        switch(ecode)
        {
            ERROR_LIST(STR_ERROR);
            default: return "Unknown error";
        }
        return "";
    }

    void *nx_spl::S3StorageFactory::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        DEBUGLOG("S3StorageFactory::queryInterface");
        try
        {
            if (std::memcmp(&interfaceID, &IID_StorageFactory, sizeof(nxpl::NX_GUID)) == 0)
            {
                addRef();
                return static_cast<S3StorageFactory*>(this);
            }
            else if (std::memcmp(&interfaceID, &nxpl::IID_PluginInterface, sizeof(nxpl::IID_PluginInterface)) == 0)
            {
                addRef();
                return static_cast<nxpl::PluginInterface*>(this);
            }
            else
            {
                DEBUGLOG("Invalid GUID:" ,interfaceID.bytes);
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
        
        return nullptr;
    }
    int nx_spl::S3StorageFactory::addRef() const
    {
        DEBUGLOG("S3StorageFactory::addRef");
        try
        {
            return p_addRef();
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
    }
    int nx_spl::S3StorageFactory::releaseRef() const
    {
        DEBUGLOG("S3StorageFactory::releaseRef");
        try
        {
           return p_releaseRef();
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
    }

    nx_spl::S3Storage::S3Storage(const std::string &url) : 
    m_available(false),
    m_intialized(false),
    m_freebucketSize(S3_DEFAULT_TOTAL_SPACE),
    m_totalSpace(S3_DEFAULT_TOTAL_SPACE)
    {
        INFOLOG("S3Storage::S3Storage", url);
        aux::Url u;
        try
        {
            std::string tmp_url = url;
            std::string prefix = "@https//";
            size_t pos = tmp_url.find(prefix);
            if (pos != std::string::npos) {
                tmp_url.erase(pos+1, prefix.length()-1);
            }
            u = aux::Url::fromString(tmp_url);
        }
        catch (const std::logic_error& e)
        {
            ERRORLOG(e.what());
            throw aux::BadUrlException(e.what());
        }

        int schemeSize = 8; // "https://" size
        if(u.host.substr(0, schemeSize) == "https://")
        {
            u.host = u.host.substr(schemeSize,u.host.size());
        }

        if(u.host.empty() || u.uaccessKey.empty() || u.usecreatKey.empty()||u.path.empty())
        {
            ERRORLOG("Invalid Url or credentials",url);
            throw aux::BadUrlException("Invalid Url or credentials!!");
        }
        if(!u.port.empty())
        {
            u.host = u.host + ":" + u.port; 
        }

        INFOLOG("Host:",u.host);
        
        m_impl.reset(new s3Client(u.host,u.uaccessKey,u.usecreatKey,u.path));

        if((m_impl.get() != nullptr) && m_impl.get()->establishS3Connection())
        {
            INFOLOG("=====================>");
            // m_available = true;
            m_totalSpace = S3_DEFAULT_TOTAL_SPACE;
            std::ifstream jsonFile(S3_CONFIG_FILE);
            if (!jsonFile.is_open()) 
            {
                ERRORLOG("Error opening config file:",S3_CONFIG_FILE);
            }
            else
            {
                Json::Value jsonData;
                Json::CharReaderBuilder jsonReaderBuilder;
                bool validJson = Json::parseFromStream(jsonReaderBuilder, jsonFile, &jsonData, nullptr);

                if(validJson && jsonData.isMember("s3storage"))
                {
                    const Json::Value s3storageArray = jsonData["s3storage"];
                    if(s3storageArray.isArray())
                    {
                        bool storageFound = false;
                        for (const auto& s3storageObject : s3storageArray) 
                        {
                            std::string url = s3storageObject["url"].asString();
                            std::string bucket = s3storageObject["bucket"].asString();
                            INFOLOG("u.host",u.host, "url",url, "bucket",u.path, bucket);
                            if((u.host == url) && (u.path == bucket))
                            {
                                m_totalSpace = s3storageObject["size"].asUInt64();
                                m_totalSpace *= DEFAULT_1_GB;
                                INFOLOG("total space:",m_totalSpace);
                                storageFound = true;
                                break;
                            }
                        }
                        if(storageFound == false)
                        {
                            INFOLOG("Storage configuration not found",u.host,u.path);
                        }
                    }
                    else
                    {
                        ERRORLOG("Json file invalid having no array!!");
                    }
                }
                else
                {
                    ERRORLOG("Invalid json");
                }
            }
        }
        else
        {
            m_impl.reset();
            ERRORLOG("failed to create connection",url);
            throw aux::ConnectException("failed to create connection!!");
        }
    }

    int STORAGE_METHOD_CALL nx_spl::S3Storage::isAvailable() const
    {
        DEBUGLOG("S3Storage::isAvailable");
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(ServerManager::getInstance()->isLicenseAvailable() == false)
            {
                ERRORLOG("Invalid License!!");
                m_available = false;
                return 0;
            }

            if(m_impl.get() != nullptr)
            {
                if(m_intialized == false)
                {
                    m_impl.get()->initializeConnection();
                    m_intialized = true;
                }

                m_available = m_impl.get()->isAvailable();
                if(m_available == false)
                {
                    INFOLOG("Connection lost");
                }
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
                m_available = false;
            }
            DEBUGLOG("Storage Available",m_available);
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
        return m_available;
    }

    IODevice *STORAGE_METHOD_CALL nx_spl::S3Storage::open(const char *uri, int flags, int *ecode) const
    {
        INFOLOG("S3Storage::open",uri,flags);
        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return nullptr;
        IODevice *ret = nullptr;
        std::string filePath(uri);
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);

            if((file.fullPath.find(".mkv") == std::string::npos) 
                && !fs::exists(file.fullPath.c_str())
                && (m_impl.get() != nullptr))
            {
                if (!fs::exists(file.folderPath))
                {
                    INFOLOG("Creating folder:", file.folderPath);
                    fs::create_directories(file.folderPath); // creates all missing parent directories
                }
                m_impl.get()->downloadFile(filePath.c_str(),file.fullPath);
            }
            
            if(filePath.find(".mkv") != std::string::npos)
            {
                if(flags & io::WriteOnly)
                {
                    uintmax_t localFolderSize = nx_spl::aux::getFolderSize(nx_spl::aux::localUniqueFolder());
                    if(localFolderSize > ServerManager::getInstance()->getLocalBufferSize())
                    {
                        INFOLOG("Local Folder is full!! No space available.");
                        *ecode = error::WriteNotSupported;
                        return ret;
                    }
                    if (!fs::exists(file.folderPath))
                    {
                        INFOLOG("Creating folder:", file.folderPath);
                        fs::create_directories(file.folderPath); // creates all missing parent directories
                    }
                }
                else if(flags & io::ReadOnly)
                {
                    // size_t last_slase_pos = filePath.find_last_of('/');
                    // if (last_slase_pos != std::string::npos) 
                    // {
                    //     std::string tempFileName = filePath.substr(last_slase_pos+1, filePath.length());
                    //     size_t last_underscore_pos = tempFileName.find_last_of('_');
                    //     if (last_underscore_pos != std::string::npos) 
                    //     {
                    //         last_underscore_pos = filePath.find_last_of('_');
                    //         filePath = filePath.substr(0, last_underscore_pos);
                    //         filePath.append(".mkv");
                    //     }
                    // }

                    aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);
                    if(!fs::exists(file.fullPath))
                    {
                        *ecode = error::UrlNotExists;
                        return ret;
                    }
                    ClearMemoryManager::getInstance()->deleteFileFromRemoveList(file.fullPath);
                }
            }

            S3IODevice *temp_ptr = new S3IODevice( filePath.c_str(), flags,m_impl);
            if(temp_ptr != nullptr)
            {
                if(temp_ptr->intialise())
                {
                    ret = temp_ptr;
                }
                else
                {
                    ERRORLOG("Failed to initialize S3IODevice pointer!!");
                    delete temp_ptr;
                }
            }
            else
            {
                ERRORLOG("invalid S3IODevice pointer!!");
            }
            return ret;
        }
        catch(std::exception &e)
        {
            ERRORLOG("Exception Error:",filePath,flags,e.what());
            if(ecode)
                *ecode = error::UrlNotExists;
            return nullptr;
        }
        catch (...)
        {
            ERRORLOG("Unknown error",filePath,flags);
            if(ecode)
                *ecode = error::UrlNotExists;
            return nullptr;
        }
    }
    
    uint64_t STORAGE_METHOD_CALL nx_spl::S3Storage::getFreeSpace(int *ecode) const
    {
        DEBUGLOG("S3Storage::getFreeSpace");
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
                return 0;

            if (ecode)
                *ecode = error::NoError;
            
            static bool spaceFullSet = false;
            uintmax_t localFolderSize = nx_spl::aux::getFolderSize(nx_spl::aux::localUniqueFolder());
            if(localFolderSize > ServerManager::getInstance()->getLocalBufferSize())
            {
                DEBUGLOG("local folder full:",localFolderSize);
                if(spaceFullSet == false)
                {
                    INFOLOG("local folder full:",localFolderSize);
                    spaceFullSet = true;
                }
                if (ecode)
                    *ecode = error::SpaceInfoNotAvailable;
                return 0;
            }

            if(spaceFullSet)
            {
                INFOLOG("Server is Back Online:",localFolderSize);
            }

            spaceFullSet = false;
            
            if(m_impl.get() != nullptr)
            {
                uint64_t totalSize = m_impl.get()->remoteFolderSize();
                m_freebucketSize = getTotalSpace(ecode) - totalSize - localFolderSize;
                DEBUGLOG("TotalSpace:",getTotalSpace(ecode));
                DEBUGLOG("remoteFolderSize:",totalSize);
                DEBUGLOG("m_freebucketSize:",m_freebucketSize);
            }
            
            return m_freebucketSize;
        }
        catch(std::exception &e)
        {
            ERRORLOG("Exception Error:",e.what());
            if(ecode)
                *ecode = error::UnknownError;
            return 0;
        }
    }

    uint64_t STORAGE_METHOD_CALL nx_spl::S3Storage::getTotalSpace(int *ecode) const
    {
        DEBUGLOG("S3Storage::getTotalSpace");
        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return 0;
        return m_totalSpace;
    }
    int STORAGE_METHOD_CALL nx_spl::S3Storage::getCapabilities() const
    {
        DEBUGLOG("S3Storage::getCapabilities");
        int ret = 0;
        uintmax_t localFolderSize = nx_spl::aux::getFolderSize(nx_spl::aux::localUniqueFolder());
        if(localFolderSize < ServerManager::getInstance()->getLocalBufferSize())
        {
            ret |= cap::WriteFile;
        }
        ret |= cap::ReadFile;
        ret |= cap::ListFile;
        ret |= cap::RemoveFile;
        return ret;
    }

    void STORAGE_METHOD_CALL nx_spl::S3Storage::removeFile(const char *url, int *ecode)
    {
        INFOLOG("S3Storage::removeFile",url);
        std::string filePath(url);
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(ecode)
                *ecode = error::NoError;


            // if(filePath.find(".mkv") != std::string::npos)
            // {
            //     size_t last_slase_pos = filePath.find_last_of('/');
            //     if (last_slase_pos != std::string::npos) 
            //     {
            //         std::string tempFileName = filePath.substr(last_slase_pos+1, filePath.length());
            //         size_t last_underscore_pos = tempFileName.find_last_of('_');
            //         if (last_underscore_pos != std::string::npos) 
            //         {
            //             last_underscore_pos = filePath.find_last_of('_');
            //             filePath = filePath.substr(0, last_underscore_pos);
            //             filePath.append(".mkv");
            //         }
            //     }
            // }

            aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);

            if(fs::exists(file.fullPath.c_str()))
            {
                ClearMemoryManager::getInstance()->deleteFileFromWriteList(file.fullPath);
                INFOLOG("Delete File:", file.fullPath);
                if(remove(file.fullPath.c_str()) != 0)
                {
                    ERRORLOG("Failed to remove file:", file.fullPath.c_str());
                    ClearMemoryManager::getInstance()->addFileToRemoveList(file.fullPath);
                }
            }
            if(m_impl.get() != nullptr)
            {
                if(m_impl.get()->remoteUriExists(filePath))
                {
                    // uint64_t size = m_impl.get()->getRemoteFileSize(filePath);

                    if (!m_impl.get()->removeUrl(filePath.c_str())) 
                    {
                        ERRORLOG("Failed to delete file",filePath);
                        if(ecode)
                            *ecode = error::UnknownError;
                    }
                    else 
                    {
                        if(ecode)
                            *ecode = error::NoError;
                        INFOLOG("file deleted",filePath);
                    }
                }
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!!");
                if(ecode)
                    *ecode = error::UnknownError;
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            if(ecode)
                *ecode = error::UnknownError;
        }
    }

    void STORAGE_METHOD_CALL nx_spl::S3Storage::removeDir(const char *url, int *ecode)
    {
        DEBUGLOG("S3Storage::removeDir",url);
        if(ecode)
            *ecode = error::UnknownError;
    }

    void STORAGE_METHOD_CALL nx_spl::S3Storage::renameFile(const char *oldUrl, const char *newUrl, int *ecode)
    {
        INFOLOG("S3Storage::renameFile",oldUrl,newUrl);
        std::string oldFileUrl = oldUrl;
        std::string newFileUrl = newUrl;
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
                return;
            
            if(m_impl.get() != nullptr)
            {
                aux::FileNameAndPath oldFile = aux::localUniqueFilePath(std::string(oldFileUrl));
                aux::FileNameAndPath newFile = aux::localUniqueFilePath(std::string(newUrl));

                if(fs::exists(oldFile.fullPath.c_str()))
                {
                    fs::rename(oldFile.fullPath, newFile.fullPath);
                    ClearMemoryManager::getInstance()->deleteFileFromWriteList(oldFile.fullPath);
                    if (!m_impl.get()->addFileToUploadInQueue(newFileUrl.c_str())) 
                    {
                        ERRORLOG("Failed to upload object, deleting the object",oldFileUrl,newFileUrl);
                        INFOLOG("Delete File:", newFile.fullPath);
                        if(remove(newFile.fullPath.c_str()) != 0)
                        {
                            ERRORLOG("Failed to remove file:", newFile.fullPath.c_str());
                            ClearMemoryManager::getInstance()->addFileToRemoveList(newFile.fullPath);
                        }
                        if(ecode)
                            *ecode = error::UrlNotExists;
                    }
                    else
                    {
                        INFOLOG("added file to uploaded",newUrl);
                        if(ecode)
                            *ecode = error::NoError;
                    }
                }
                else
                {
                    ERRORLOG("File not exist",oldFile.fullPath);
                    if(ecode)
                        *ecode = error::UrlNotExists;
                }
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!")
                if(ecode)
                    *ecode = error::UnknownError;
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what(),oldFileUrl,newFileUrl);
            if(ecode)
                *ecode = error::UnknownError;
        }
    }

    FileInfoIterator *STORAGE_METHOD_CALL nx_spl::S3Storage::getFileIterator(const char *dirUrl, int *ecode) const
    {
        INFOLOG("----------------->",dirUrl);
        std::string dirPath = dirUrl;
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if((aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError))
                return nullptr;
                
            if(m_impl.get() == nullptr)
            {
                ERRORLOG("implPtrType is nullptr");
                if(ecode)
                    *ecode = error::UnknownError;
                return nullptr;
            }

            if(!dirPath.empty() && dirPath.back() == '/')
            {
                dirPath.pop_back();
            }

            if(dirPath.empty())
            {
                ERRORLOG("dirPath is empty!!");
                if(ecode)
                    *ecode = error::UnknownError;
                return nullptr;
            }

            std::vector<std::string> localObjects;
            std::string dir = nx_spl::aux::localUniqueFolder() + dirPath;
            std::vector<std::string> objects;
            if (fs::exists(dir) && fs::is_directory(dir))
            {
                for (const auto& entry : fs::directory_iterator(dir))
                {
                    std::string line;
                    if (entry.is_regular_file())
                    {
                        std::string fileName = entry.path().filename().string();
                        line.append(fileName);
                        objects.push_back(fileName);
                        line.append(",");
                        line.append(std::to_string(nx_spl::isFile));
                        line.append(",");
                        line.append(std::to_string(entry.file_size()));
                        INFOLOG("----------->Local:",line);
                        localObjects.push_back(line);
                    }
                    else if (entry.is_directory())
                    {
                        std::string dirName = entry.path().filename().string();
                        line.append(dirName);
                        objects.push_back(dirName);
                        line.append(",");
                        line.append(std::to_string(nx_spl::isDir));
                        line.append(",");
                        line.append("0");
                        INFOLOG("------------>Local:",line);
                        localObjects.push_back(line);
                    }
                }
            }

            std::vector<std::string> s3Objects = m_impl.get()->getobjectKeys(dirPath.c_str(), objects);
            localObjects.insert(
                localObjects.end(),
                std::make_move_iterator(s3Objects.begin()),
                std::make_move_iterator(s3Objects.end())
            );

            if(!localObjects.empty())
            {
                return new S3FileInfoIterator(std::move(localObjects),dirPath);
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            if(ecode)
                *ecode = error::UnknownError;
            return nullptr;
        }
        return nullptr;
    }

    int STORAGE_METHOD_CALL nx_spl::S3Storage::fileExists(const char *url, int *ecode) const
    {
        DEBUGLOG("S3Storage::fileExists",url);
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
                return 0;

            if(m_impl.get() != nullptr)
            {
                std::string filePath(url);

                // if(filePath.find(".mkv") != std::string::npos)
                // {
                //     size_t last_slase_pos = filePath.find_last_of('/');
                //     if (last_slase_pos != std::string::npos) 
                //     {
                //         std::string tempFileName = filePath.substr(last_slase_pos+1, filePath.length());
                //         size_t last_underscore_pos = tempFileName.find_last_of('_');
                //         if (last_underscore_pos != std::string::npos) 
                //         {
                //             last_underscore_pos = filePath.find_last_of('_');
                //             filePath = filePath.substr(0, last_underscore_pos);
                //             filePath.append(".mkv");
                //         }
                //     }
                // }

                aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);
                if(fs::exists(file.fullPath))
                {
                    INFOLOG("File already into local storage",file.fullPath);
                    if((m_impl.get() != nullptr) && (!m_impl.get()->isFileInUploadList(filePath))) {
                        ClearMemoryManager::getInstance()->addFileToRemoveList(file.fullPath);
                    }
                    return 1;
                }
                else
                {
                    if (!fs::exists(file.folderPath))
                    {
                        INFOLOG("Creating folder:", file.folderPath);
                        fs::create_directories(file.folderPath); // creates all missing parent directories
                    }
                    if (!m_impl.get()->downloadFile(filePath.c_str(),file.fullPath)) 
                    {
                        ERRORLOG("file not found:",file.fullPath);
                        *ecode = error::UrlNotExists;
                        return 0;
                    }
                    else
                    {
                        INFOLOG("File found!!",file.fullPath);
                        ClearMemoryManager::getInstance()->addFileToRemoveList(file.fullPath);
                        return 1;
                    }
                }
            }
            else
            {
                ERRORLOG("implPtrType is null ptr!!");
                *ecode = error::UrlNotExists;
                return 0;
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            if(ecode)
                *ecode = error::UnknownError;
            return 0;
        }
    }

    int STORAGE_METHOD_CALL nx_spl::S3Storage::dirExists(const char *url, int *ecode) const
    {
        DEBUGLOG("S3Storage::dirExists",url);
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if((aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError) )
                return 0;

            if(m_impl.get() != nullptr)
            {
                if (m_impl.get()->remoteDirExists(url)) 
                {
                    DEBUGLOG("Directory found:",url);
                    return 1;
                }
                else
                {
                    ERRORLOG("Failed to find directory:",url);
                    *ecode = error::UrlNotExists;
                    return 0;
                }
            }
            else
            {
                ERRORLOG("implPtrType is null ptr!!");
                *ecode = error::UrlNotExists;
                return 0;
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            if(ecode)
                *ecode = error::UnknownError;
            return 0;
        }
    }

    uint64_t STORAGE_METHOD_CALL nx_spl::S3Storage::fileSize(const char *url, int *ecode) const
    {
        DEBUGLOG("S3Storage::fileSize",url);
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
                return 0;
            std::string filePath(url);
            aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);

            uint64_t size = 0;
            if(fs::exists(file.fullPath.c_str()))
            {
                size = aux::getFileSize(file.fullPath.c_str());
                DEBUGLOG("fileSize:",filePath,size);
            }
            else if((m_impl.get() != nullptr) && m_impl.get()->isAvailable())
            {
                size = m_impl.get()->getRemoteFileSize(filePath);
                DEBUGLOG("fileSize:",filePath,size);
            }
            else
            {
                INFOLOG("File do not exist",filePath);
                if(ecode)
                    *ecode = error::UnknownError;
            }
            return size;
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            if(ecode)
                *ecode = error::UnknownError;
            return 0;
        }
    }

    void *nx_spl::S3Storage::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        DEBUGLOG("S3Storage::queryInterface");
        try
        {
            if (std::memcmp(&interfaceID,
                            &IID_Storage,
                            sizeof(nxpl::NX_GUID)) == 0)
            {
                addRef();
                return static_cast<nx_spl::Storage*>(this);
            }
            else if (std::memcmp(&interfaceID,
                                    &nxpl::IID_PluginInterface,
                                    sizeof(nxpl::IID_PluginInterface)) == 0)
            {
                addRef();
                return static_cast<nxpl::PluginInterface*>(this);
            }
            else
            {
                DEBUGLOG("Invalid GUID:" ,interfaceID.bytes);
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
        return nullptr;
    }
    int nx_spl::S3Storage::addRef() const
    {
        DEBUGLOG("S3Storage::addRef");
        try
        {
            return p_addRef();
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            return 0;
        }
    }

    int nx_spl::S3Storage::releaseRef() const
    {
        DEBUGLOG("S3Storage::releaseRef");
        try
        {
            return p_releaseRef();
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            return 0;
        }
    }

    nx_spl::S3Storage::~S3Storage()
    {
        INFOLOG("S3Storage::~S3Storage");
        m_impl.reset();
    }

    nx_spl::S3IODevice::S3IODevice(const char *uri,
                                    int mode, 
                                    const  implPtrType &impl
        ): m_mode(mode),
        m_updateDate(""),
        m_pos(0),
        m_altered(false),
        m_localsize(0),
        m_impl(impl),
        m_uri(uri),
        m_file(NULL)
    {
        
        DEBUGLOG("S3IODevice::S3IODevice");
    }

    bool S3IODevice::intialise()
    {
        DEBUGLOG("S3IODevice::intialise");
        bool ret = false;
        try
        {
            if(m_mode & io::WriteOnly)
            {
                m_localfile = aux::localUniqueFilePath(m_uri);
                if(fs::exists(m_localfile.fullPath))
                {
                    if ((m_localsize = aux::getFileSize(m_localfile.fullPath.c_str())) <= 0)
                    {
                        ERRORLOG("Invalid local file size:",m_localfile.fullPath,m_localsize);
                    }
                    else
                    {
                        m_file = fopen(m_localfile.fullPath.c_str(), "r+b");
                    }
                }
                else
                {
                    m_file = fopen(m_localfile.fullPath.c_str(), "w+b");
                }
            }
            else if(m_mode & io::ReadOnly)
            {
                std::string file = m_uri;
                // if((file.find(".nxdb") == std::string::npos) && (file.find("info.txt") == std::string::npos) )
                // {
                //     size_t last_slase_pos = file.find_last_of('/');
                //     if (last_slase_pos != std::string::npos) 
                //     {
                //         std::string tempFileName = file.substr(last_slase_pos+1, file.length());
                //         size_t last_underscore_pos = tempFileName.find_last_of('_');
                //         if (last_underscore_pos != std::string::npos) 
                //         {
                //             last_underscore_pos = file.find_last_of('_');
                //             file = file.substr(0, last_underscore_pos);
                //             file.append(".mkv");
                //         }
                //     }
                // }
                m_localfile = aux::localUniqueFilePath( file);
                if ((m_localsize = aux::getFileSize(m_localfile.fullPath.c_str())) <= 0)
                {
                    ERRORLOG("Invalid local file size:",m_localfile.fullPath,m_localsize);
                }
                else
                {
                    m_file = fopen(m_localfile.fullPath.c_str(), "rb");
                }
            }

            
            DEBUGLOG("File size",m_localfile.fullPath,m_localsize);

            if(m_file == NULL)
            {
                ERRORLOG("Failed to open local file!!",m_localfile.fullPath);
                if((m_impl.get() != nullptr) && (!m_impl.get()->isFileInUploadList(m_uri))) {
                    ClearMemoryManager::getInstance()->addFileToRemoveList(m_localfile.fullPath);
                }
            }
            else 
            {
                ret = true;
                if(m_mode & io::WriteOnly) {
                    ClearMemoryManager::getInstance()->addFileToWriteList(m_localfile.fullPath);
                }
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error while IO operation",e.what());
        }
        catch(...)
        {
            ERRORLOG("Exception Error while IO operation",m_uri,m_mode);
        }
        return ret;
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::write(const void *src, const uint32_t size, int *ecode)
    {
        DEBUGLOG("S3IODevice::write");
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (!(m_mode & io::WriteOnly))
            {
                if (ecode)
                    *ecode = error::WriteNotSupported;
                return 0;
            }
            if(m_file == NULL)
            {
                ERRORLOG("m_file is null",m_localfile.fullPath);
                if (ecode)
                    *ecode = error::UrlNotExists;
                return 0;
            } 
            DEBUGLOG("S3IODevice::write:",m_localfile.fullPath,ftell(m_file),m_pos,size);

            if (ecode)
                *ecode = error::NoError;

            size_t writeSize = fwrite(src, 1, size, m_file);
            if(writeSize < size)
            {
                ERRORLOG("Failed to write into file",writeSize,size,m_localfile.fullPath);
                if (ecode)
                    *ecode = error::NotEnoughSpace;
                return writeSize;
            }
            m_pos += writeSize;
            m_localsize += writeSize;
            m_altered = true;
            if(m_localfile.fullPath.find(".nxdb") != std::string::npos)
            {
                fclose(m_file);
                if (ServerManager::getInstance()->isNxdbSyncEnabled()
                    && (m_updateDate.empty() || m_updateDate != nx_spl::aux::getCurrentDate()))
                {
                    flush();
                    m_updateDate = nx_spl::aux::getCurrentDate();
                }
                m_file = fopen(m_localfile.fullPath.c_str(), "r+b");
                if (fseek(m_file, (int)m_pos, SEEK_SET) != 0) 
                {
                    ERRORLOG("Error while seek file:",m_localfile.fullPath);
                    if (ecode)
                        *ecode = error::NotEnoughSpace;
                    writeSize = 0;
                }
            } 
            return writeSize;
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            if (ecode)
                *ecode = error::UnknownError;
            return 0;
        }
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::read(void *dst, const uint32_t size, int *ecode) const
    {
        DEBUGLOG("S3IODevice::read",m_localfile.fullPath,size,m_localsize,m_pos);
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            size_t readSize = 0;
            
            if (!(m_mode & io::ReadOnly))
            {
                if (ecode)
                    *ecode = error::ReadNotSupported;
                return 0;
            }

            if (m_file == NULL)
            {
                ERRORLOG("Error while reading file:",m_localfile.fullPath);
                if (ecode)
                    *ecode = error::UrlNotExists;
                return 0;
            }

            if (ecode)
                *ecode = error::NoError;

            readSize = fread(dst, 1, size, m_file);
            if(readSize < size)
            {
                if (feof(m_file)) 
                {
                    INFOLOG("EOF",m_localfile.fullPath,readSize);
                    m_pos += readSize;
                    if((readSize <= 0) && ecode)
                        *ecode = error::EndOfFile;
                    return readSize;
                } 
                else if (ferror(m_file)) 
                {
                    ERRORLOG("Error reading from file",m_localfile.fullPath);
                    if (ecode)
                        *ecode = error::UnknownError;
                    return 0;
                }
            }
            m_pos += readSize;
            DEBUGLOG("S3IODevice::read",readSize);
            return readSize;
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            if (ecode)
                *ecode = error::UnknownError;
            return 0;
        }
    }

    int STORAGE_METHOD_CALL nx_spl::S3IODevice::seek(uint64_t pos, int *ecode)
    {
        DEBUGLOG("S3IODevice::seek",m_localfile.fullPath,m_pos,pos);
        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_file == NULL)
            {
                ERRORLOG("m_file nullptr",m_localfile.fullPath);
                if (ecode)
                    *ecode = error::UrlNotExists;
                return 0;
            }

            if (ecode)
                *ecode = error::NoError;

            if ((fseek(m_file, (int)pos, SEEK_SET) != 0))
            {
                ERRORLOG("Error while seek file:",m_localfile.fullPath);
                *ecode = error::UnknownError;
                return 0;
            }
            else
            {
                m_pos = pos;
                return 1;
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            if (ecode)
                *ecode = error::UnknownError;
            return 0;
        }
    }

    int STORAGE_METHOD_CALL nx_spl::S3IODevice::getMode() const
    {
        DEBUGLOG("S3IODevice::getMode");
        return m_mode;
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::size(int *ecode) const
    {
        DEBUGLOG("S3IODevice::size");
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ecode)
            *ecode = error::NoError;
        DEBUGLOG("local file size:",m_localfile.fullPath, m_localsize);
        return static_cast<uint32_t>(m_localsize);
    }

    void *nx_spl::S3IODevice::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        DEBUGLOG("S3IODevice::queryInterface");
        try
        {
            if (std::memcmp(&interfaceID,
                            &IID_IODevice,
                            sizeof(nxpl::NX_GUID)) == 0)
            {
                addRef();
                return static_cast<nx_spl::IODevice*>(this);
            }
            else if (std::memcmp(&interfaceID,
                                    &nxpl::IID_PluginInterface,
                                    sizeof(nxpl::IID_PluginInterface)) == 0)
            {
                addRef();
                return static_cast<nxpl::PluginInterface*>(this);
            }
            else
            {
                DEBUGLOG("Invalid GUID:" ,interfaceID.bytes);
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
        return nullptr;
    }

    int nx_spl::S3IODevice::addRef() const
    {
        DEBUGLOG("S3IODevice::addRef");
        try
        {
            return p_addRef();
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            return 0;
        }
    }

    int nx_spl::S3IODevice::releaseRef() const
    {
        DEBUGLOG("S3IODevice::releaseRef");
        try
        {
            return p_releaseRef();
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            return 0;
        }
    }

    void nx_spl::S3IODevice::flush()
    {
        DEBUGLOG("S3IODevice::flush");
        try
        {
            if(m_altered)
            {
                m_altered = false;
                if(m_impl.get() != nullptr)
                {
                    m_impl.get()->uploadFile(m_uri.c_str(),m_localfile.fullPath);
                }
                else
                {
                    ERRORLOG("implPtrType is nullptr!")
                }
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
        
        
    }

    S3IODevice::~S3IODevice()
    {
        INFOLOG("S3IODevice::~S3IODevice",m_localfile.fullPath);
        try
        {
            if (m_file != NULL)
                fclose(m_file);

            m_file = NULL;

            if(m_localfile.fullPath.find(".mkv") == std::string::npos)
            {
                flush();
                if(m_localfile.fullPath.find(".nxdb") == std::string::npos)
                {
                    INFOLOG("Delete File:", m_localfile.fullPath);
                    if (remove(m_localfile.fullPath.c_str()) != 0)
                    {
                        ERRORLOG("Failed to remove file:", m_localfile.fullPath);
                    }
                }
            }

            if((m_mode & io::ReadOnly) 
                && (m_localfile.fullPath.find(".nxdb") == std::string::npos) 
                && !m_impl->isFileInUploadList(m_uri))
            {
                if (fs::exists(m_localfile.fullPath.c_str())) 
                {
                    ClearMemoryManager::getInstance()->addFileToRemoveList(m_localfile.fullPath);
                }
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
    }

    S3FileInfoIterator::S3FileInfoIterator(FileListType &&fileList, const std::string &baseDir):
    m_fileList(std::move(fileList)),
    m_curFile(m_fileList.cbegin()),
    m_baseDir(baseDir)
    {
        INFOLOG("------>S3FileInfoIterator::S3FileInfoIterator",m_baseDir);
    }

    FileInfo *STORAGE_METHOD_CALL S3FileInfoIterator::next(int *ecode) const
    {
        DEBUGLOG("S3FileInfoIterator::next:");
        try
        {
            if (ecode)
                *ecode = nx_spl::error::NoError;

            if (m_curFile != m_fileList.cend())
            {
                const std::string line = m_curFile->c_str();
                DEBUGLOG("S3FileInfoIterator::next:",line);
                ++m_curFile;
                std::stringstream ss(line);
                std::vector<std::string> substrings;
                std::string substring;
                while (std::getline(ss, substring, ',')) 
                {
                    substrings.push_back(substring);
                }  
                if(substrings.size() == 3)
                {
                    m_fileInfo.url = std::string(m_baseDir + "/" + substrings.at(0)).c_str();
                    if(std::stoi(substrings.at(1)) == 0)
                    {
                        m_fileInfo.type = isFile;
                    }
                    else
                    {
                        m_fileInfo.type = isDir;
                    }
                    m_fileInfo.size = std::stoi(substrings.at(2));
                    INFOLOG("----------------->",m_fileInfo.url, m_fileInfo.type, m_fileInfo.size);
                    substrings.clear();
                    return &m_fileInfo;
                } 
                else
                {
                    substrings.clear();
                    INFOLOG("Invalid file url:" ,line);
                }
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
        return nullptr;
    }
    
    void *nx_spl::S3FileInfoIterator::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        DEBUGLOG("S3FileInfoIterator::queryInterface");
        try
        {
            if (std::memcmp(&interfaceID,
                            &IID_FileInfoIterator,
                            sizeof(nxpl::NX_GUID)) == 0)
            {
                addRef();
                return static_cast<nx_spl::FileInfoIterator*>(this);
            }
            else if (std::memcmp(&interfaceID,
                                    &nxpl::IID_PluginInterface,
                                    sizeof(nxpl::IID_PluginInterface)) == 0)
            {
                addRef();
                return static_cast<nxpl::PluginInterface*>(this);
            }
            else
            {
                DEBUGLOG("Invalid GUID:" ,interfaceID.bytes);
            }
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
        }
        return nullptr;
    }

    int nx_spl::S3FileInfoIterator::addRef() const
    {
        DEBUGLOG("S3FileInfoIterator::addRef");
        try
        {
            return p_addRef();
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            return 0;
        }
    }

    int nx_spl::S3FileInfoIterator::releaseRef() const
    {
        DEBUGLOG("S3FileInfoIterator::releaseRef");
        try
        {
             return p_releaseRef(); 
        }
        catch(const std::exception& e)
        {
            ERRORLOG("Exception Error:",e.what());
            return 0;
        }
    }

    nx_spl::S3FileInfoIterator::~S3FileInfoIterator()
    {
        INFOLOG("--------->S3FileInfoIterator::~S3FileInfoIterator", m_baseDir);
    }
}

extern "C"
{
    NX_PLUGIN_API nxpl::PluginInterface* createNXPluginInstance()
    {
        nx_spl::aux::DailyLogger::Initialize();
        INFOLOG("======================================create  NXPlugin Instance================================");
        ServerManager::getInstance();
        ClearMemoryManager::getInstance();
        return new nx_spl::S3StorageFactory();
    }
}
