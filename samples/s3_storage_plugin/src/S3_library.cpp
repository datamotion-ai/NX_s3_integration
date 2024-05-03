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
#include "S3_library.h"
#include "daily_loger.hpp"
#include "ServerManager.h"
#include "ClearMemoryManager.h"

namespace nx_spl
{

    // S3StorageFactory

    std::mutex nx_spl::S3StorageFactory::m_mutex;
    bool  g_licenseAvailable = false;

    nx_spl::S3StorageFactory::S3StorageFactory()
    {
        INFOLOG("S3StorageFactory::S3StorageFactory");
        m_options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Error;
        Aws::InitAPI(m_options);
        std::srand((unsigned int) time(0));

    #if defined (_WIN32)

        NTSTATUS(WINAPI *RtlGetVersion)(LPOSVERSIONINFOEXW);
        OSVERSIONINFOEXW osInfo;

        *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

        if (NULL != RtlGetVersion)
        {
            osInfo.dwOSVersionInfoSize = sizeof(osInfo);
            RtlGetVersion(&osInfo);
        }
        g_userAgent = "Wasabi/1.0 NX Wasabi_storage_sdk/"  + std::string(VERSION) 
        + " Windows/" + std::to_string(osInfo.dwMajorVersion) + "." 
        + std::to_string( osInfo.dwMinorVersion) + "." + std::to_string( osInfo.dwBuildNumber);
        INFOLOG("OS Version",g_userAgent);

    #else
        struct utsname unameData;
        uname(&unameData);
        g_userAgent = "Wasabi/1.0 NX Wasabi_storage_sdk/"  +  std::string(VERSION) + " LINUX/" + unameData.release;
        INFOLOG("OS Version",g_userAgent);
    #endif

        ClearMemoryManager::getInstance();
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
        assert(false);
        return nullptr;
    }

    Storage *STORAGE_METHOD_CALL nx_spl::S3StorageFactory::createStorage(const char *url, int *ecode)
    {
        INFOLOG("S3StorageFactory::createStorage",url);
        Storage* ret = nullptr;
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
        switch(ecode)
        {
            ERROR_LIST(STR_ERROR);
        }
        return "";
    }

    void *nx_spl::S3StorageFactory::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        DEBUGLOG("S3StorageFactory::queryInterface");
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
        return nullptr;
    }
    int nx_spl::S3StorageFactory::addRef() const
    {
        DEBUGLOG("S3StorageFactory::addRef");
        return p_addRef();
    }
    int nx_spl::S3StorageFactory::releaseRef() const
    {
        DEBUGLOG("S3StorageFactory::releaseRef");
        return p_releaseRef();
    }

    nx_spl::S3Storage::S3Storage(const std::string &url) : 
    m_available(false),
    m_freebucketSize(S3_DEFAULT_TOTAL_SPACE),
    m_totalSpace(S3_DEFAULT_TOTAL_SPACE)
    {

        aux::Url u;
        try
        {
            u = aux::Url::fromString(url);
        }
        catch (const std::logic_error& e)
        {
            ERRORLOG(e.what());
            throw aux::BadUrlException(e.what());
        }

        if(u.host.empty() || u.uaccessKey.empty() || u.usecreatKey.empty()||u.path.empty())
        {
            ERRORLOG("Invalid Url or credentials",url);
            throw aux::BadUrlException("Invalid Url or credentials!!");
        }

        m_impl.reset(new s3Client(u.host,u.uaccessKey,u.usecreatKey,u.path));

        if((m_impl.get() != nullptr) && m_impl.get()->establishS3Connection())
        {
            m_available = true;
            std::ifstream jsonFile(S3_CONFIG_FILE);
            if (!jsonFile.is_open()) 
            {
                ERRORLOG("Error opening config file:",S3_CONFIG_FILE);
                m_totalSpace = S3_DEFAULT_TOTAL_SPACE;
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
                            if((u.host == url) && (u.path == bucket))
                            {
                                m_totalSpace = s3storageObject["size"].asUInt64();
                                m_totalSpace *= DEFAULT_1_GB;
                                INFOLOG("total space:",m_totalSpace);
                                storageFound = true;
                                break;
                            }
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
        INFOLOG("S3Storage::isAvailable");
        std::lock_guard<std::mutex> lock(m_mutex);
        if(ServerManager::getInstance()->isLicenseAvailable() == false)
        {
            ERRORLOG("Invalid License!!");
            m_available = false;
            ServerManager::getInstance()->postEvent("License Expired!!,Update License Details!!","");
            return 0;
        }

        if(m_impl.get() != nullptr)
        {
            m_available = m_impl.get()->isAvailable();
            if(m_available == false)
            {
                INFOLOG("Connection lost");
                // m_impl.get()->stopThread();
                // m_available = m_impl.get()->establishS3Connection();
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr!");
            m_available = false;
        }
        INFOLOG("Storage Available",m_available);
        return 1;
    }

    IODevice *STORAGE_METHOD_CALL nx_spl::S3Storage::open(const char *uri, int flags, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        INFOLOG("S3Storage::open",uri,flags);
        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return nullptr;
        *ecode = error::NoError;
        IODevice *ret = nullptr;
        try
        {
            std::string filePath(uri);
            aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);

            if((file.fullPath.find(".mkv") == std::string::npos) && 
            !fs::exists(file.fullPath.c_str()) && 
            (m_impl.get() != nullptr))
            {
                m_impl.get()->downloadFile(uri,file.fullPath);
            }
            if((flags & io::ReadOnly) && (filePath.find(".mkv") != std::string::npos))
            {
                size_t last_underscore_pos = filePath.find_last_of('_');
                if (last_underscore_pos != std::string::npos) 
                {
                    filePath = filePath.substr(0, last_underscore_pos);
                    filePath.append(".mkv");
                }
                aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);
                if(!fs::exists(file.fullPath))
                {
                    *ecode = error::UrlNotExists;
                    return ret;
                }
                ClearMemoryManager::getInstance()->deleteFileFromRemoveList(file.fullPath);
            }

            ret = new S3IODevice( uri, flags,m_impl);
            return ret;
        }
        catch (...)
        {
            ERRORLOG("Unable to open file",uri,flags);
            *ecode = error::UrlNotExists;
            return nullptr;
        }
    }
    
    uint64_t STORAGE_METHOD_CALL nx_spl::S3Storage::getFreeSpace(int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::getFreeSpace");
        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return 0;

        if (ecode)
            *ecode = error::NoError;
        uint64_t totalSize = 0;
        
        if(g_bucketSizeNeedUpdate && (m_impl.get() != nullptr))
        {
            INFOLOG("Updating Free size");
            try
            {
                totalSize = m_impl.get()->remoteFolderSize("/");
                INFOLOG("totalSize:",totalSize);
            }
            catch(...)
            {
                if (ecode)
                *ecode = error::SpaceInfoNotAvailable;
                return 0;
            }
            m_freebucketSize = getTotalSpace(ecode) - totalSize;
            INFOLOG("Free size",m_freebucketSize);
            g_bucketSizeNeedUpdate = false;
        }
        
        return m_freebucketSize;  
    }

    uint64_t STORAGE_METHOD_CALL nx_spl::S3Storage::getTotalSpace(int *ecode) const
    {
        DEBUGLOG("S3Storage::getTotalSpace");
        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return 0;
        if (ecode)
            *ecode = error::NoError;
        return m_totalSpace;
    }
    int STORAGE_METHOD_CALL nx_spl::S3Storage::getCapabilities() const
    {
        DEBUGLOG("S3Storage::getCapabilities");
        int ret = 0;
        ret |= cap::ListFile;
        ret |= cap::WriteFile;
        ret |= cap::ReadFile;
        ret |= cap::RemoveFile;
        ret |= cap::DBReady;
        return ret;
    }

    void STORAGE_METHOD_CALL nx_spl::S3Storage::removeFile(const char *url, int *ecode)
    {
        DEBUGLOG("S3Storage::removeFile",url,m_bucket);
        std::lock_guard<std::mutex> lock(m_mutex);

        *ecode = error::NoError;

        std::string filePath(url);
        aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);

        if(fs::exists(file.fullPath.c_str()))
        {
            remove(file.fullPath.c_str());
        }
        else
        {
            if(m_impl.get() != nullptr)
            {
                if(m_impl.get()->remoteUriExists(url))
                {
                    uint64_t size = m_impl.get()->getRemoteFileSize(url);

                    if (!m_impl.get()->removeUrl(url)) 
                    {
                        ERRORLOG("Failed to delete file",url);
                        *ecode = error::UnknownError;
                    }
                    else 
                    {
                        *ecode = error::NoError;
                        m_freebucketSize = m_freebucketSize + size;
                        INFOLOG("file deleted",url,m_freebucketSize);
                    }
                }
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!!");
                *ecode = error::UnknownError;
            }
        }
        return ;
    }

    void STORAGE_METHOD_CALL nx_spl::S3Storage::removeDir(const char *url, int *ecode)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::removeDir",url,m_bucket);
        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return;
        
        if(m_impl.get() != nullptr)
        {
            if (!m_impl.get()->removeUrl(url)) 
            {
                ERRORLOG("Failed to delete directory",url);
                *ecode = error::UnknownError;
            }
            else 
            {
                *ecode = error::NoError;
                g_bucketSizeNeedUpdate = true;
                INFOLOG("deleted directory",url);
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr!");
            *ecode = error::UnknownError;
        }
        return ;
    }

    void STORAGE_METHOD_CALL nx_spl::S3Storage::renameFile(const char *oldUrl, const char *newUrl, int *ecode)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::renameFile",oldUrl,newUrl);
        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return;

        if(m_impl.get() != nullptr)
        {
            aux::FileNameAndPath oldFile = aux::localUniqueFilePath(std::string(oldUrl));

            if(fs::exists(oldFile.fullPath.c_str()))
            {
                if (!m_impl.get()->addFileToUploadInQueue(newUrl)) 
                {
                    ERRORLOG("Failed to upload object",oldUrl,newUrl);
                    ClearMemoryManager::getInstance()->addFileToRemoveList(oldFile.fullPath);
                    *ecode = error::UnknownError;
                }
                else
                {
                    uint64_t size = aux::getFileSize(oldFile.fullPath.c_str());
                    m_freebucketSize = m_freebucketSize + size;
                    INFOLOG("added file to uploaded",newUrl,m_freebucketSize);
                    *ecode = error::NoError;
                }
            }
            else
            {
                ERRORLOG("File not exist",oldFile.fullPath);
                *ecode = error::UrlNotExists;
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr!")
            *ecode = error::UnknownError;
        }
        return ;
    }

    FileInfoIterator *STORAGE_METHOD_CALL nx_spl::S3Storage::getFileIterator(const char *dirUrl, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::getFileIterator",dirUrl);
        if((aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError))
            return nullptr;
            
        if(m_impl.get() == nullptr)
        {
            ERRORLOG("implPtrType is nullptr");
            return nullptr;
        }

        std::vector<std::string> objects = m_impl.get()->getobjectKeys(dirUrl); 

        if(!objects.empty())
        {
            return new S3FileInfoIterator(std::move(objects),dirUrl);
        }

        return nullptr;
    }

    int STORAGE_METHOD_CALL nx_spl::S3Storage::fileExists(const char *url, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::fileExists",url);

        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return 0;

        if(m_impl.get() != nullptr)
        {
            std::string filePath(url);

            if(filePath.find(".nxdb") == std::string::npos)
            {
                size_t last_underscore_pos = filePath.find_last_of('_');
                if (last_underscore_pos != std::string::npos) 
                {
                    filePath = filePath.substr(0, last_underscore_pos);
                    filePath.append(".mkv");
                }
            }

            aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);
            if(fs::exists(file.fullPath))
            {
                INFOLOG("File already downloaded into local storage",file.fullPath);
                ClearMemoryManager::getInstance()->addFileToRemoveList(file.fullPath);
                return 1;
            }
            else
            {
                uintmax_t localFolderSize = aux::getFolderSize(aux::localUniqueFolder());
                if(localFolderSize <= DEFAULT_1_GB)
                {
                    if (!m_impl.get()->downloadFile(url,file.fullPath)) 
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
                else
                {
                    ERRORLOG("local folder full:",localFolderSize);
                    ServerManager::getInstance()->postEvent("No Space Avaialble in Local Folder!!, Recording stop!!","");
                    *ecode = error::UrlNotExists;
                    return 0;
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

    int STORAGE_METHOD_CALL nx_spl::S3Storage::dirExists(const char *url, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::dirExists",url);
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

    uint64_t STORAGE_METHOD_CALL nx_spl::S3Storage::fileSize(const char *url, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::fileSize");
        if(aux::checkECode(ecode, ServerManager::getInstance()->isLicenseAvailable()) != nx_spl::error::NoError)
            return 0;
        std::string filePath(url);
        aux::FileNameAndPath file = aux::localUniqueFilePath(filePath);

        uint64_t size = 0;
        if(fs::exists(file.fullPath.c_str()))
        {
            size = aux::getFileSize(file.fullPath.c_str());
        }
        return size;
    }

    void *nx_spl::S3Storage::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        DEBUGLOG("S3Storage::queryInterface");
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
        return nullptr;
    }
    int nx_spl::S3Storage::addRef() const
    {
        DEBUGLOG("S3Storage::addRef");
        return p_addRef();
    }
    int nx_spl::S3Storage::releaseRef() const
    {
        DEBUGLOG("S3Storage::releaseRef");
        return p_releaseRef();
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
        m_fileWriteCount(0),
        m_pos(0),
        m_altered(false),
        m_localsize(0),
        m_impl(impl),
        m_uri(uri),
        m_file(NULL)
    {
        try
        {
            if(mode & io::WriteOnly)
            {
                m_localfile = aux::localUniqueFilePath(m_uri);
                if(fs::exists(m_localfile.fullPath))
                {
                    if ((m_localsize = aux::getFileSize(m_localfile.fullPath.c_str())) <= 0)
                    {
                        ERRORLOG("Invalid local file size:",m_localfile.fullPath,m_localsize);
                        throw aux::InternalErrorException("local file calculate size failed");
                    }
                    m_file = fopen(m_localfile.fullPath.c_str(), "r+b");
                }
                else
                {
                    uintmax_t localFolderSize = aux::getFolderSize(aux::localUniqueFolder());
                    if(localFolderSize <= DEFAULT_1_GB)
                    {
                         m_file = fopen(m_localfile.fullPath.c_str(), "w+b");
                    }
                    else
                    {
                        ERRORLOG("local folder full:",localFolderSize);
                        ServerManager::getInstance()->postEvent("No Space Avaialble in Local Folder!!, Recording stop!!","");
                        throw aux::InternalErrorException("local folder is full");
                    }
                }
            }
            else if(mode & io::ReadOnly)
            {
                std::string file = m_uri;
                if((file.find(".nxdb") == std::string::npos) && (file.find("info.txt") == std::string::npos) )
                {
                    size_t last_underscore_pos = file.find_last_of('_');
                    if (last_underscore_pos != std::string::npos) 
                    {
                        file = file.substr(0, last_underscore_pos);
                        file.append(".mkv");
                    }
                }
                m_localfile = aux::localUniqueFilePath( file);
                if ((m_localsize = aux::getFileSize(m_localfile.fullPath.c_str())) <= 0)
                {
                    ERRORLOG("Invalid local file size:",m_localfile.fullPath,m_localsize);
                    throw aux::InternalErrorException("local file calculate size failed");
                }
                m_file = fopen(m_localfile.fullPath.c_str(), "rb");
            }

            
            INFOLOG("File size",m_localfile.fullPath,m_localsize);

            if(m_file == NULL)
            {
                ERRORLOG("Failed to open local file!!",m_localfile.fullPath);
                throw aux::InternalErrorException("Failed to open local file");
            }
        }
        catch(...)
        {
            ERRORLOG("Error while IO operation",uri,mode);
            ClearMemoryManager::getInstance()->addFileToRemoveList(m_localfile.fullPath);
            throw ;
        }
        DEBUGLOG("--------------------------done");
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::write(const void *src, const uint32_t size, int *ecode)
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

        int writeSize = fwrite(src, 1, size, m_file);
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
        m_fileWriteCount++;
        if((m_localfile.fullPath.find(".nxdb") != std::string::npos) && (m_fileWriteCount >= MAX_FILE_WRITE_COUNT))
        {
            fclose(m_file);
            flush();
            m_file = fopen(m_localfile.fullPath.c_str(), "r+b");
            if (fseek(m_file, (int)m_pos, SEEK_SET) != 0) 
            {
                ERRORLOG("Error while seek file:",m_localfile.fullPath);
                if (ecode)
                    *ecode = error::NotEnoughSpace;
                writeSize = 0;
            }
            m_fileWriteCount = 0;
        } 
        return writeSize;
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::read(void *dst, const uint32_t size, int *ecode) const
    {
        DEBUGLOG("S3IODevice::read",m_localfile.fullPath,size,m_localsize,m_pos);
        std::lock_guard<std::mutex> lock(m_mutex);
        uint32_t readSize = 0;
        
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

    int STORAGE_METHOD_CALL nx_spl::S3IODevice::seek(uint64_t pos, int *ecode)
    {
        DEBUGLOG("S3IODevice::seek",m_localfile.fullPath,m_pos,pos);
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
        return nullptr;
    }

    int nx_spl::S3IODevice::addRef() const
    {
        DEBUGLOG("S3IODevice::addRef");
        return p_addRef();
    }

    int nx_spl::S3IODevice::releaseRef() const
    {
        DEBUGLOG("S3IODevice::releaseRef");
        return p_releaseRef();
    }

    void nx_spl::S3IODevice::flush()
    {
        DEBUGLOG("S3IODevice::flush");
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

    S3IODevice::~S3IODevice()
    {
        INFOLOG("S3IODevice::~S3IODevice",m_localfile.fullPath);
        if (m_file != NULL)
            fclose(m_file);

        m_file = NULL;

        if(m_localfile.fullPath.find("info.txt") != std::string::npos)
        {
            flush();
            remove(m_localfile.fullPath.c_str());
        }

        if((m_mode & io::ReadOnly) && (m_localfile.fullPath.find(".nxdb") == std::string::npos) && !m_impl->isFileInUploadList(m_uri))
        {
            if (fs::exists(m_localfile.fullPath.c_str())) 
            {
                ClearMemoryManager::getInstance()->addFileToRemoveList(m_localfile.fullPath);
            }
        }
    }

    S3FileInfoIterator::S3FileInfoIterator(FileListType &&fileList, const std::string &baseDir):
    m_fileList(std::move(fileList)),
    m_curFile(m_fileList.cbegin())
    {
        DEBUGLOG("S3FileInfoIterator::S3FileInfoIterator");
    }

    FileInfo *STORAGE_METHOD_CALL S3FileInfoIterator::next(int *ecode) const
    {
        DEBUGLOG("S3FileInfoIterator::next:");
        if (ecode)
            *ecode = nx_spl::error::NoError;

        if (m_curFile != m_fileList.cend())
        {
            std::string line = m_curFile->c_str();
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
                m_fileInfo.url = substrings.at(0).c_str();
                if(std::stoi(substrings.at(1)) == 0)
                {
                    m_fileInfo.type = isFile;
                }
                else
                {
                    m_fileInfo.type = isDir;
                }
                m_fileInfo.size = std::stoi(substrings.at(2));
                DEBUGLOG("----------------->",m_fileInfo.url);
                substrings.clear();
                return &m_fileInfo;
            } 
            else
            {
                substrings.clear();
                INFOLOG("Invalid file url:" ,line);
            }
        }
        return nullptr;
    }
    
    void *nx_spl::S3FileInfoIterator::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        DEBUGLOG("S3FileInfoIterator::queryInterface");
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
        return nullptr;
    }

    int nx_spl::S3FileInfoIterator::addRef() const
    {
        DEBUGLOG("S3FileInfoIterator::addRef");
        return p_addRef();
    }

    int nx_spl::S3FileInfoIterator::releaseRef() const
    {
        DEBUGLOG("S3FileInfoIterator::releaseRef");
        return p_releaseRef();  
    }

    nx_spl::S3FileInfoIterator::~S3FileInfoIterator()
    {
        DEBUGLOG("S3FileInfoIterator::~S3FileInfoIterator");

    }
}

extern "C"
{
    NX_PLUGIN_API nxpl::PluginInterface* createNXPluginInstance()
    {
        nx_spl::aux::DailyLogger::Initialize();
        INFOLOG("======================================create  NXPlugin Instance================================");
        ServerManager::getInstance();
        return new nx_spl::S3StorageFactory();
    }
}
