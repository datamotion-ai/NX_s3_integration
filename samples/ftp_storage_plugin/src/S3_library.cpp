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
#include <sstream>
#include <json/json.h>

#if defined(__linux__) || defined(__APPLE__)
#   include <sys/stat.h>
#endif

#include "S3_library.h"

#ifdef _MSC_VER
#   define NOEXCEPT
#elif defined __GNUC__
#   define NOEXCEPT noexcept
#endif

#define BUCKET_NAME "nxoptics"
#define S3_CONFIG_FILE "s3.config"
#define S3_DEFAULT_TOTAL_SPACE 100LL * 1024 * 1024 * 1024 //100GB

#define DEBUGLOG(...) ""
//#define DEBUGLOG(...) nx_spl::aux::DailyLogger::Log(nx_spl::aux::DailyLogger::LogPriority::DebugP, __FUNCTION__, __LINE__, __VA_ARGS__);
#define INFOLOG(...) nx_spl::aux::DailyLogger::Log(nx_spl::aux::DailyLogger::LogPriority::InfoP, __FUNCTION__, __LINE__, __VA_ARGS__);
#define ERRORLOG(...) nx_spl::aux::DailyLogger::Log(nx_spl::aux::DailyLogger::LogPriority::ErrorP, __FUNCTION__, __LINE__, __VA_ARGS__);

bool m_bucketSizeNeedUpdate = true;

nx_spl::aux::DailyLogger::LogPriority nx_spl::aux::DailyLogger::DailyLogger::verbosity = nx_spl::aux::DailyLogger::LogPriority::DebugP;
std::string nx_spl::aux::DailyLogger::logDirectory = "./logs";  // Default log directory
std::string nx_spl::aux::DailyLogger::currentLogFile;

namespace nx_spl
{
    namespace aux
    { 

        struct Url
        {
            std::string uaccessKey;
            std::string usecreatKey;
            std::string host;
            std::string path;

            static Url fromString(const std::string& s)
            {
                enum
                {   // parse states
                    scheme,
                    accessKey,
                    secreatKey,
                    host
                } ps = scheme;

                const int schemeSize = 6; // "S3://" size
                int start = 0, cur = 0;
                char c;
                Url u;

                if (s.size() <= schemeSize)
                    throw std::logic_error("Url parse failed. Too short.");

                for (;;)
                {
                    switch (ps)
                    {
                    case scheme:
                        if (s.substr(0, schemeSize) != "ftp://")
                            throw std::logic_error("Url parse failed. Wrong scheme.");
                        start = schemeSize;
                        cur = schemeSize;
                        // there can be no username substring in url. It's ok.
                        if (s.find('@', schemeSize) != std::string::npos)
                            ps = accessKey;
                        else
                            ps = host;
                        break;
                    case accessKey:
                        if (cur == (int) s.size()) // That's all? No, url can't end here
                            throw std::logic_error("Url parse failed. Url string ended at username parse stage");
                        c = s[cur];
                        if (c != ':' && c != '@')
                            ++cur;
                        else if (c == ':')
                        {
                            if (cur - start == 0) // if you wrote ':', you should have provided some non-empty name before.
                                throw std::logic_error("Url parse failed. Username is empty");
                            u.uaccessKey.assign(s.begin() + start, s.begin() + cur);
                            ++cur;
                            start = cur;
                            ps = secreatKey;
                        }
                        else if (c == '@')
                        {
                            if (cur - start == 0) // if you wrote '@', you should have provided some non-empty name before.
                                throw std::logic_error("Url parse failed. Username is empty");
                            u.uaccessKey.assign(s.begin() + start, s.begin() + cur);
                            ++cur;
                            start = cur;
                            ps = host;
                        }
                        break;
                    case secreatKey:
                        if (cur == (int) s.size()) // That's all? No, url can't end here
                            throw std::logic_error("Url parse failed. Url string ended at password parse stage");
                        c = s[cur];
                        if (c != '@')
                            ++cur;
                        else
                        {
                            u.usecreatKey.assign(s.begin() + start, s.begin() + cur);
                            ++cur;
                            start = cur;
                            ps = host;
                        }
                        break;
                    case host:
                        if (cur == (int) s.size())
                        {
                            u.host.assign(s.begin() + start, s.begin() + cur);
                            goto end;
                        }
                        c = s[cur];
                        if (c == '/') //path begins
                        {
                            u.host.assign(s.begin() + start, s.begin() + cur);
                            u.path.assign(s.begin() + cur + 1, s.end());
                            goto end;
                        }

                        if (c != ':')
                            ++cur;
                        else
                        {
                            if (cur - start == 0) // That's all? No, url can't end here
                                throw std::logic_error("Url parse failed. Host is empty");
                            u.host.assign(s.begin() + start, s.begin() + cur);
                            ++cur;
                            start = cur;
                        }
                        break;
                    default:
                        // something strange happened if we are here
                        throw std::logic_error("Url parse failed. Invalid parse state.");
                    }
                }

            end:
                return u;
            } // Url::fromString()
        }; // struct Url


        // set error code to initial state (NoError generally if storage is available)
        error::code_t checkECode(
            int            *checked,                // ecode to set
            const int       avail,                  // pass result of getAvail here
            error::code_t   toSet = error::NoError  // default ecode
        )
        {
            if (checked)
                *checked = error::NoError;

            if (!avail)
            {
                if (checked)
                    *checked = error::StorageUnavailable;
                return error::StorageUnavailable;
            }
            else if (checked)
                *checked = toSet;

            return (error::code_t)*checked;
        }

        class NetworkException
            : public std::runtime_error
        {
        public:
            explicit NetworkException(const char* s)
                : runtime_error(s)
            {
            }

            virtual const char* what() const NOEXCEPT
            {
                return runtime_error::what();
            }
        };

        class BadUrlException
            : public std::runtime_error
        {
        public:
            explicit BadUrlException(const char* s)
                : runtime_error(s)
            {
            }

            virtual const char* what() const NOEXCEPT
            {
                return runtime_error::what();
            }
        };

        class ConnectException
            : public std::runtime_error
        {
        public:
            explicit ConnectException(const char* s)
                : runtime_error(s)
            {
            }

            virtual const char* what() const NOEXCEPT
            {
                return runtime_error::what();
            }
        };


        class InternalErrorException
            : public std::runtime_error
        {
        public:
            explicit InternalErrorException(const char* s)
                : runtime_error(s)
            {
            }

            virtual const char* what() const NOEXCEPT
            {
                return runtime_error::what();
            }
        };

        void dirFromUri(
        const std::string   &uri, // In
        std::string         *dir, // Out. directory name
        std::string         *file // Out. file name
        )
        {
            std::string::size_type pos;
            if ((pos = uri.rfind('/')) == std::string::npos)
            {   // Here we think that '/' is the only path separator.
                // Maybe generally it is not very reliable.
                *dir = ".";
                *file = uri;
            }
            else
            {
                dir->assign(uri.begin()+1, uri.begin() + pos);
                if (pos < uri.size() - 1) // if file name is not empty
                    file->assign(uri.begin() + pos + 1, uri.end());
            }
        }

        /**
         * Generates a pseudo-random file name and a file path to the OS TMP directory + the generated name.
         * \param suffix If set it is appended to the result file name.
         */
        static FileNameAndPath localUniqueFilePath(const std::string& suffix = std::string())
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << "suffix:" << suffix << std::endl;
            /* First, get a system tmp path*/
            std::string tmpFolder;
            #if defined (_WIN32)
                char buf[MAX_PATH + 1];
                DWORD result = GetTempPathA(sizeof(buf), buf);
                assert(result > 0);
                if (result == 0)
                    std::cerr << "Failed to get a temporary folder path" << std::endl;
                tmpFolder = buf;
            #elif defined (__unix__)
                for (const auto& v: {"TMP", "TEMP", "TMPDIR", "TEMPDIR"})
                {
                    const char* envVar = getenv(v);
                    if (envVar == nullptr)
                        continue;
                    tmpFolder = envVar;
                    break;
                }
                if (tmpFolder.empty())
                    tmpFolder = "/tmp";
            #else
                assert(false);
            #endif
            /* Now, when the base path is found, generate pseudo random bytes for a file name. */
            std::stringstream nameStream;
            for (int i = 0; i < 4; ++i)
                nameStream << std::hex << rand();
            /* Append the suffix, fill the result and we are done. */
            nameStream << suffix;
            FileNameAndPath nameAndPath;
            nameAndPath.name = nameStream.str();
            nameAndPath.fullPath = tmpFolder + "/" + nameAndPath.name;
            return nameAndPath;
        }

        // get local file size by it's name
        long long getFileSize(const char *fname)
        {
            // std::cout << __LINE__ << ":" << __func__ << ": fname:" << fname << std::endl;
        #ifdef _WIN32

            HANDLE hFile = CreateFileA(
                fname,
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            if (hFile == INVALID_HANDLE_VALUE)
                return -1;

            LARGE_INTEGER s;
            if (!GetFileSizeEx(hFile, &s))
            {
                CloseHandle(hFile);
                return -1;
            }
            CloseHandle(hFile);
            return s.QuadPart;

        #elif defined(__linux__) || defined(__APPLE__)

            struct stat st;
            if (stat(fname, &st) == -1)
                return -1;
            return st.st_size;
        #endif
        }

        // Checks if remote dir exists. Bases on MLSD command response parsing.
        bool remoteUriExists(const std::string& uri,const std::string& bucket,const implPtrType& impl) 
        {
            DEBUGLOG(uri,bucket);
            bool found = false;
            if(impl.get() != nullptr)
            {
                Aws::S3::Model::HeadObjectRequest request;
                request.WithBucket(bucket).WithKey(uri);
                const auto response = impl->HeadObject(request);
                found = response.IsSuccess();
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
            return found;
        }

        bool remoteFileExist(const std::string& uri,const std::string& bucket,const implPtrType& impl)
        {
            DEBUGLOG(uri,bucket);
            if(impl.get() == nullptr)
            {
                ERRORLOG("implPtrType is nullptr!");
                throw std::runtime_error("implPtrType is nullptr!!");
            }
            
            std::string dir;
            std::string file;
            dirFromUri(uri, &dir, &file);

            if (file.empty()) // Uri shouldn't end with '/'
            {
                INFOLOG("empty file name",uri);
                throw std::runtime_error("empty file name");
            }

            bool found = false;
            Aws::S3::Model::ListObjectsRequest request;
            request.SetBucket(bucket);
            
            auto outcome = impl->ListObjects(request);
            if (outcome.IsSuccess()) 
            {
                auto objects = outcome.GetResult().GetContents();
                for (const auto& object : objects) {
                    if(file == object.GetKey())
                    {
                        found = true;
                        break;
                    }
                }
            }
            else 
            {
                INFOLOG("Remote dir not exists",bucket);
                throw aux::BadUrlException("Remote dir not exists");
            }
            return found;
        }

        uint64_t remoteFolderSize(const std::string& uri,const std::string& bucket,const implPtrType& impl)
        {
            DEBUGLOG("uri:",uri,"bucket:",bucket);
            if(impl.get() == nullptr)
            {
                ERRORLOG("implPtrType is nullptr!");
                throw std::runtime_error("implPtrType is nullptr!!");
            }

            if (uri.empty()) 
            {
                INFOLOG("empty file name");
                throw std::runtime_error("empty file name");
            }
                

            uint64_t totalSize = 0;
            Aws::S3::Model::ListObjectsRequest request;
            request.SetBucket(bucket);
            request.WithPrefix(uri);
            
            
            auto outcome = impl->ListObjects(request);
            if (outcome.IsSuccess()) 
            {
                for (const auto& object : outcome.GetResult().GetContents())
                {
                    // Get metadata for each object to get the size
                    Aws::S3::Model::HeadObjectRequest headObjectRequest;
                    headObjectRequest.WithBucket(bucket)
                        .WithKey(object.GetKey());

                    Aws::S3::Model::HeadObjectOutcome headObjectOutcome = impl->HeadObject(headObjectRequest);

                    if (headObjectOutcome.IsSuccess())
                    {
                        totalSize += headObjectOutcome.GetResult().GetContentLength();
                    }
                    else
                    {
                        ERRORLOG("Failed to get metadata",object.GetKey());
                    }
                }
            }
            else 
            {
                ERRORLOG("Remote dir not exists",uri,bucket);
                throw aux::BadUrlException("Remote dir not exists");
            }
            return totalSize;
        }

        bool createDir( const std::string& dirPath,const std::string& bucket, const implPtrType& impl) 
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << "dirPath:" << dirPath << std::endl;
            bool ret = false;
            std::string dir = dirPath;
            dir.append("/");
            if(impl.get() != nullptr)
            {
                if((remoteUriExists(dir,bucket,impl) == false))
                {
                    Aws::S3::Model::PutObjectRequest request;
                    request.SetBucket(bucket);
                    request.SetKey(dir);
                    const auto response = impl->PutObject(request);
                    ret = response.IsSuccess();
                    if (!response.IsSuccess()) 
                    {
                        ERRORLOG("Failed to create directory",dir,bucket,response.GetError().GetMessage());
                    }
                    else 
                    {
                        INFOLOG("Directory created successfully",dir,bucket);
                        ret = true;
                    }
                }
                else
                {
                    INFOLOG("Directory already exist",dir,bucket);
                    ret = true;
                }
            }
            else
            {
                ERRORLOG("implPtrType is nullptr");
            }
            return ret;
        }

        bool createBucket(const std::string  &bucket,implPtrType &impl)
        {
            bool ret = false;
            if(impl.get() != nullptr)
            {
                Aws::S3::Model::CreateBucketRequest request;
                request.SetBucket(bucket);
                Aws::S3::Model::CreateBucketOutcome outcome = impl->CreateBucket(request);
                if (!outcome.IsSuccess()) 
                {
                    auto err = outcome.GetError();
                    ERRORLOG("Failed to create bucket",bucket,err.GetMessage());
                }
                else 
                {
                    INFOLOG("bucket created",bucket);
                    ret =true;
                }
            }
            else
            {
                ERRORLOG("implPtrType is nullptr");
            }
            return ret;
        }

        bool establishS3Connection(
        const std::string  &url,        
        const std::string  &uaccessKey,      
        const std::string  &usecreatKey, 
        const std::string  &bucket,
        implPtrType        &impl        
        )
        {
            DEBUGLOG("establishS3Connection",url,uaccessKey,usecreatKey,bucket);
            try
            {
                Aws::Client::ClientConfiguration clientConfig;
                clientConfig.scheme = Aws::Http::Scheme::HTTPS;
                clientConfig.endpointOverride = Aws::String(url);

                Aws::Auth::AWSCredentials credentials;
                credentials.SetAWSAccessKeyId(uaccessKey);
                credentials.SetAWSSecretKey(usecreatKey);
                
                impl.reset(new Aws::S3::S3Client(credentials, Aws::MakeShared<Aws::S3::S3EndpointProvider>(Aws::S3::S3Client::ALLOCATION_TAG), clientConfig));
                if(impl.get() != nullptr)
                {
                    auto outcome = impl->ListBuckets();
                    if (outcome.IsSuccess()) {
                        bool bucketFound = false;
                        auto objects = outcome.GetResult().GetBuckets();
                        for (const auto& object : objects) 
                        {
                            if(object.GetName() == bucket)
                            {
                                bucketFound = true;
                                break;
                            } 
                        }
                        if((bucketFound == false) && (createBucket(bucket,impl) == false))
                        {
                            ERRORLOG("Failed to create bucket S3",bucket);
                            return false;
                        }
                        else
                        {
                            INFOLOG("SuccessFully establish s3 connection with host: ",url);
                            return true;
                        }
                    }
                }
                else
                {
                    ERRORLOG("implPtrType is nullptr");
                }
                return false;
            }
            catch (const std::exception& e)
            {
                ERRORLOG(e.what());
                throw aux::NetworkException(e.what());
            }
        }

        
    }

    // S3StorageFactory
    S3StorageFactory::S3StorageFactory()
    {
        // options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Debug;
        INFOLOG("S3StorageFactory::S3StorageFactory");
        Aws::InitAPI(options);
        std::srand((unsigned int) time(0));
    }

    S3StorageFactory::~S3StorageFactory()
    {
        INFOLOG("S3StorageFactory::~S3StorageFactory");
        Aws::ShutdownAPI(options);
    }

    const char** STORAGE_METHOD_CALL S3StorageFactory::findAvailable() const
    {
        assert(false);
        return nullptr;
    }

    Storage *STORAGE_METHOD_CALL S3StorageFactory::createStorage(const char *url, int *ecode)
    {
        DEBUGLOG("S3StorageFactory::createStorage",url);
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
                *ecode = error::UnknownError;
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


    const char *STORAGE_METHOD_CALL S3StorageFactory::storageType() const
    {
        return "ftp";
    }

    #define ERROR_LIST(APPLY) \
    APPLY(nx_spl::error::EndOfFile) \
    APPLY(nx_spl::error::NoError) \
    APPLY(nx_spl::error::NotEnoughSpace) \
    APPLY(nx_spl::error::ReadNotSupported) \
    APPLY(nx_spl::error::SpaceInfoNotAvailable) \
    APPLY(nx_spl::error::StorageUnavailable) \
    APPLY(nx_spl::error::UnknownError) \
    APPLY(nx_spl::error::UrlNotExists) \
    APPLY(nx_spl::error::WriteNotSupported)

    #define STR_ERROR(ecode) case ecode: return #ecode;

    const char *S3StorageFactory::lastErrorMessage(int ecode) const
    {
        switch(ecode)
        {
            ERROR_LIST(STR_ERROR);
        }
        return "";
    }

    #undef PRINT_ERROR
    #undef ERROR_LIST

    void *S3StorageFactory::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        DEBUGLOG("S3StorageFactory::queryInterface");
        if (std::memcmp(&interfaceID,
                        &IID_StorageFactory,
                        sizeof(nxpl::NX_GUID)) == 0)
        {
            addRef();
            return static_cast<S3StorageFactory*>(this);
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
    int S3StorageFactory::addRef() const
    {
        DEBUGLOG("S3StorageFactory::addRef");
        return p_addRef();
    }
    int S3StorageFactory::releaseRef() const
    {
        DEBUGLOG("S3StorageFactory::releaseRef");
        return p_releaseRef();
    }
    S3Storage::S3Storage(const std::string &url) : 
    m_available(false),
    m_freebucketSize(S3_DEFAULT_TOTAL_SPACE),
    m_totalSpace(S3_DEFAULT_TOTAL_SPACE)
    {
        try
        {
            aux::Url u;
            try
            {
                u = aux::Url::fromString(url);
            }
            catch (const std::exception& e)
            {
                ERRORLOG(e.what());
                throw aux::BadUrlException(e.what());
            }
            if(u.host.empty() || u.uaccessKey.empty() || u.usecreatKey.empty()||u.path.empty())
            {
                ERRORLOG("Invalid Url or credentials",url);
                throw aux::BadUrlException("Invalid Url or credentials!!");
            }

            m_url = u.host;
            m_accessKey = u.uaccessKey.c_str();
            m_secretKey = u.usecreatKey.c_str();
            m_bucket = u.path.c_str();

            m_available = aux::establishS3Connection(m_url,m_accessKey,m_secretKey,m_bucket, m_impl);

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
                Json::parseFromStream(jsonReaderBuilder, jsonFile, &jsonData, nullptr);

                const Json::Value s3storageArray = jsonData["s3storage"];
                bool storageFound = false;
                for (const auto& s3storageObject : s3storageArray) 
                {
                    std::string url = s3storageObject["url"].asString();
                    std::string bucket = s3storageObject["bucket"].asString();
                    if((m_url == url) && (m_bucket == bucket))
                    {
                        m_totalSpace = s3storageObject["size"].asUInt64();
                        m_totalSpace *= 1024 * 1024 * 1024;
                        INFOLOG("total space:",m_totalSpace);
                        storageFound = true;
                        break;
                    }
                }

                if(storageFound == false)
                {
                    INFOLOG("storage not found in config file:",S3_CONFIG_FILE,m_bucket,url);
                }
            }
        }
        catch(const aux::NetworkException& e)
        {
            m_available = false;
            return;
        }
        catch(...)
        {
            m_available = false;
            throw;
        }
    }
    int STORAGE_METHOD_CALL S3Storage::isAvailable() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::isAvailable");
        m_available = false;
        if(m_impl != nullptr)
        {
            auto outcome = m_impl->ListBuckets();
            if (outcome.IsSuccess()) 
            {
                auto objects = outcome.GetResult().GetBuckets();
                for (const auto& object : objects) 
                {
                    if(object.GetName() == m_bucket)
                    {
                        m_available = true;
                        break;
                    }
                }
            }
            else 
            {
                INFOLOG("No connection!!");
            }
            if(m_available == false)
            {
                INFOLOG("Connection lost");
                m_available = aux::establishS3Connection(m_url,m_accessKey,m_secretKey,m_bucket,m_impl);
            }
        }
        return m_available;
    }
    IODevice *STORAGE_METHOD_CALL S3Storage::open(const char *uri, int flags, int *ecode) const
    {
        // std::lock_guard<std::mutex> lock(m_mutex);
        INFOLOG("S3Storage::open",uri,flags);
        *ecode = error::NoError;
        IODevice *ret = nullptr;
        if (!isAvailable())
        {
            *ecode = error::StorageUnavailable;
            INFOLOG("S3 not connected");
            return ret;
        }

        try
        {
            ret = new S3IODevice(
                uri,
                m_bucket.c_str(),
                flags,
                m_impl
            );
            return ret;
        }
        catch (const aux::BadUrlException& e)
        {
            *ecode = error::UrlNotExists;
            ERRORLOG(e.what());
            return nullptr;
        }
        catch (...)
        {
            ERRORLOG("Unable to open file",uri,m_bucket,flags);
            *ecode = error::UnknownError;
            return nullptr;
        }
    }
    
    uint64_t STORAGE_METHOD_CALL S3Storage::getFreeSpace(int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::getFreeSpace");
        if (ecode)
        *ecode = error::NoError;
        uint64_t totalSize = 0;
        
        if(m_bucketSizeNeedUpdate)
        {
            INFOLOG("Updating Free size");
            try
            {
                totalSize = aux::remoteFolderSize(m_bucket + "/",m_bucket,m_impl);
                INFOLOG("totalSize:",totalSize);
            }
            catch(const std::runtime_error& e)
            {
                ERRORLOG(e.what());
            }
            m_freebucketSize = getTotalSpace(ecode) - totalSize;
            INFOLOG("Free size",m_freebucketSize);
            m_bucketSizeNeedUpdate = false;
        }
        
        return m_freebucketSize;  
    }

    uint64_t STORAGE_METHOD_CALL S3Storage::getTotalSpace(int *ecode) const
    {
        DEBUGLOG("S3Storage::getTotalSpace");
        if (ecode)
        *ecode = error::NoError;
        return m_totalSpace;
    }
    int STORAGE_METHOD_CALL S3Storage::getCapabilities() const
    {
        DEBUGLOG("S3Storage::getCapabilities");
        int ret = 0;
        ret |= cap::ListFile;
        ret |= cap::WriteFile;
        ret |= cap::ReadFile;
        ret |= cap::RemoveFile;
        return ret;
    }

    void STORAGE_METHOD_CALL S3Storage::removeFile(const char *url, int *ecode)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return;
        
        *ecode = error::NoError;
        
        if(m_impl.get() != nullptr)
        {
            uint64_t size = fileSize(url,ecode);
            Aws::S3::Model::DeleteObjectRequest request;
            request.WithBucket(m_bucket)
                    .WithKey(url);

            const auto response = m_impl->DeleteObject(request);
            if (!response.IsSuccess()) 
            {
                ERRORLOG("Failed to delete file",url,m_bucket);
                *ecode = error::UnknownError;
            }
            else 
            {
                *ecode = error::NoError;
                m_freebucketSize = m_freebucketSize + size;
                INFOLOG("file deleted",url,m_bucket,m_freebucketSize);
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr!!");
            *ecode = error::UnknownError;
        }

        return ;
    }

    void STORAGE_METHOD_CALL S3Storage::removeDir(const char *url, int *ecode)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::removeDir",url);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return;
        
        if(m_impl.get() != nullptr)
        {
            Aws::S3::Model::DeleteObjectRequest request;
            request.WithBucket(m_bucket)
                    .WithKey(url);

            const auto response = m_impl->DeleteObject(request);
            if (!response.IsSuccess()) 
            {
                ERRORLOG("Failed to delete directory",url,m_bucket);
                *ecode = error::UnknownError;
            }
            else 
            {
                *ecode = error::NoError;
                m_bucketSizeNeedUpdate = true;
                INFOLOG("deleted directory",url,m_bucket);
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr!");
            *ecode = error::UnknownError;
        }
        return ;
    }

    void STORAGE_METHOD_CALL S3Storage::renameFile(const char *oldUrl, const char *newUrl, int *ecode)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::renameFile",oldUrl,newUrl);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return;
        
        if(m_impl.get() != nullptr)
        {
            Aws::S3::Model::CopyObjectRequest copyRequest;
            copyRequest.WithBucket(m_bucket)
                        .WithCopySource(m_bucket + "/" + oldUrl)
                        .WithKey(newUrl);

            const auto response = m_impl->CopyObject(copyRequest);
            if (!response.IsSuccess()) 
            {
                ERRORLOG("Failed to copy object",oldUrl,newUrl,m_bucket);
                *ecode = error::UnknownError;
            }
            else 
            {
                Aws::S3::Model::DeleteObjectRequest request;
                request.WithBucket(m_bucket).WithKey(oldUrl);

                const auto response = m_impl->DeleteObject(request);
                if (!response.IsSuccess()) 
                {
                    ERRORLOG("Failed to delete object!!",oldUrl,m_bucket);
                        *ecode = error::UnknownError;
                }
                else 
                {
                    *ecode = error::NoError;
                }
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr!")
            *ecode = error::UnknownError;
        }
        return ;
    }

    FileInfoIterator *STORAGE_METHOD_CALL S3Storage::getFileIterator(const char *dirUrl, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::getFileIterator",dirUrl);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return nullptr;

        if(m_impl.get() == nullptr)
        {
            ERRORLOG("implPtrType is nullptr");
            return nullptr;
        }
            
            
        std::string uri = dirUrl;
        std::string dir;
        if(uri[0] == '/')
        {
            dir.assign(uri.begin()+1, uri.end());
        }
        else
        {
            dir.assign(uri.begin(), uri.end());
        }
        dir.append("/");
        

        Aws::S3::Model::ListObjectsV2Request request;
        request.SetBucket(m_bucket);
        request.WithPrefix(dir);
        request.WithDelimiter("/");
        
        auto outcome = m_impl->ListObjectsV2(request);
        if (outcome.IsSuccess()) 
        {
            auto folderObjects = outcome.GetResult().GetCommonPrefixes();
            std::vector<std::string> urls;
            for (const auto& object : folderObjects) 
            {
                std::string line;
                std::string folderName;
                folderName.assign(object.GetPrefix().begin()+dir.size(),object.GetPrefix().end()-1);
                line.append(folderName.c_str());
                line.append(",");
                line.append(std::to_string(isDir));
                line.append(",");
                line.append("0");
                DEBUGLOG(line);
                urls.push_back(line);
            }
            
            auto fileObjects = outcome.GetResult().GetContents();
            for (const auto& object : fileObjects) 
            {
                std::string line;
                std::string fileName;
                fileName.assign(object.GetKey().begin()+dir.size(),object.GetKey().end());
                line.append(fileName);
                line.append(",");
                line.append(std::to_string(isFile));
                line.append(",");
                Aws::S3::Model::HeadObjectRequest headObjectRequest;
                headObjectRequest.SetBucket(m_bucket);
                headObjectRequest.SetKey(object.GetKey());
                const auto response = m_impl->HeadObject(headObjectRequest);
                if (!response.IsSuccess()) 
                {
                    line.append("0");
                }
                else 
                {
                    line.append(std::to_string(response.GetResult().GetContentLength()));
                }
                DEBUGLOG(line);
                urls.push_back(line);
            }
            if(!urls.empty())
            {
                return new S3FileInfoIterator(
                    std::move(urls),
                    dirUrl
                );
            }
        }
        else 
        {
            return nullptr;
        }
        return nullptr;
    }

    int STORAGE_METHOD_CALL S3Storage::fileExists(const char *url, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::fileExists",url);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return 0;

        if (!aux::remoteUriExists(url,m_bucket, m_impl))
        {
            INFOLOG("file not found:",url,m_bucket);
            return 0;
        }
        else
        {
            INFOLOG("file found:",url,m_bucket);
            return 1;
        }
    }

    int STORAGE_METHOD_CALL S3Storage::dirExists(const char *url, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::dirExists",url);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return 0;
        
        if(aux::createDir(url,m_bucket,m_impl))
        {
            INFOLOG("Directory found:",url,m_bucket);
            return 1;
        }
        else
        {
            ERRORLOG("Failed to find directory:",url,m_bucket);
            return 0;
        }
    }

    uint64_t STORAGE_METHOD_CALL S3Storage::fileSize(const char *url, int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        DEBUGLOG("S3Storage::fileSize");
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return 0;

        uint64_t size = 0;
        if(m_impl.get() != nullptr)
        {
            Aws::S3::Model::HeadObjectRequest request;
            request.SetBucket(m_bucket);
            request.SetKey(url);
            const auto response = m_impl->HeadObject(request);
            if (!response.IsSuccess()) 
            {
                ERRORLOG("File not found",url,response.GetError().GetMessage());
            }
            else 
            {
                size = response.GetResult().GetContentLength();
                INFOLOG("File size:",size);
                m_freebucketSize = m_freebucketSize - size;
                INFOLOG("Free bucket size:",m_freebucketSize);
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr");
        }
        return size;
    }

    void *S3Storage::queryInterface(const nxpl::NX_GUID &interfaceID)
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
    int S3Storage::addRef() const
    {
        DEBUGLOG("S3Storage::addRef");
        return p_addRef();
    }
    int S3Storage::releaseRef() const
    {
        DEBUGLOG("S3Storage::releaseRef");
        return p_releaseRef();
    }
    S3Storage::~S3Storage()
    {
        DEBUGLOG("S3Storage::~S3Storage");
    }

    nx_spl::S3IODevice::S3IODevice(const char *uri, 
                                    const char *bucket, 
                                    int mode, 
                                    const  implPtrType &impl
        ): m_mode(mode),
        m_bucket(bucket),
        m_fileWriteCount(0),
        m_pos(0),
        m_altered(false),
        m_localsize(0),
        m_impl(impl)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        try
        {
            std::string remoteDir, remoteFile;
            aux::dirFromUri(uri, &remoteDir, &remoteFile);
            m_localfile = aux::localUniqueFilePath("--" + remoteFile);
            m_uri = remoteDir + "/" + remoteFile;
            INFOLOG("S3IODevice::S3IODevice",uri,m_localfile.fullPath,mode);

            bool fileExists = false;

            try
            {
                fileExists = aux::remoteUriExists(uri,m_bucket,m_impl);
            }
            catch(const aux::BadUrlException& e)
            {
                INFOLOG("Directory not exist:",uri,m_bucket,e.what());

                if(false == aux::createDir(remoteDir,m_bucket,m_impl))
                {
                    INFOLOG("Failed to create Directory :",remoteDir,m_bucket);
                    throw aux::InternalErrorException(e.what());
                }   
                else fileExists = false;
            }
            catch (const std::exception& e)
            {
                ERRORLOG(e.what());
                throw aux::InternalErrorException(e.what());
            }

            if(mode & io::WriteOnly)
            {
                if (!fileExists)
                {
                    remove(m_localfile.fullPath.c_str());
                    FILE *f = fopen(m_localfile.fullPath.c_str(), "wb");
                    if (f == NULL)
                    {
                        ERRORLOG("couldn't create local temporary file",m_localfile.fullPath);
                        throw aux::InternalErrorException("couldn't create local temporary file");
                    }
                    fclose(f);

                    std::shared_ptr<Aws::IOStream> inputData =
                    Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
                        m_localfile.fullPath.c_str(),
                        std::ios_base::in | std::ios_base::binary);

                    if (!*inputData) 
                    {
                        ERRORLOG("Unable to read file",m_localfile.fullPath);
                        throw aux::InternalErrorException("Error unable to read file ");
                    }

                    Aws::S3::Model::PutObjectRequest request;
                    request.SetBucket(m_bucket);
                    request.SetKey(m_uri);
                    request.SetBody(inputData);

                    Aws::S3::Model::PutObjectOutcome outcome = m_impl->PutObject(request);

                    if (!outcome.IsSuccess()) 
                    {
                        ERRORLOG("Failed to upload file:",uri,m_bucket);
                        throw aux::InternalErrorException(outcome.GetError().GetMessage().c_str());
                    }
                }
                else
                {
                    remove(m_localfile.fullPath.c_str());
                    Aws::S3::Model::GetObjectRequest request;
                    request.SetBucket(m_bucket);
                    request.SetKey(m_uri);

                    Aws::S3::Model::GetObjectOutcome outcome = m_impl->GetObject(request);

                    if (!outcome.IsSuccess()) 
                    {
                        ERRORLOG("Download failed:",m_uri,m_bucket);
                        throw aux::InternalErrorException(outcome.GetError().GetMessage().c_str());
                    }
                    else 
                    {
                        auto& objectStream = outcome.GetResultWithOwnership().GetBody();

                        std::ofstream fileStream(m_localfile.fullPath.c_str(), std::ios::out | std::ios::binary);

                        if (fileStream) 
                        {
                            fileStream << objectStream.rdbuf();
                            fileStream.close();
                            INFOLOG("File downaloded and stored in file",m_localfile.fullPath);
                        } 
                        else 
                        {
                            ERRORLOG("Local file write failed",m_localfile.fullPath);
                            throw aux::InternalErrorException("s3 get failed");
                        }
                    }
                }
            }
            else if(mode & io::ReadOnly)
            {
                remove(m_localfile.fullPath.c_str());
                FILE *f = fopen(m_localfile.fullPath.c_str(), "wb");
                if (f == NULL)
                {
                    ERRORLOG("Local file create failed:",m_localfile.fullPath)
                    throw aux::BadUrlException("couldn't create local temporary file");
                }

                Aws::S3::Model::GetObjectRequest request;
                request.SetBucket(m_bucket);
                request.SetKey(m_uri);

                Aws::S3::Model::GetObjectOutcome outcome = m_impl->GetObject(request);

                if (!outcome.IsSuccess()) 
                {
                    ERRORLOG("Download failed:",m_uri,m_bucket);
                    throw aux::BadUrlException(outcome.GetError().GetMessage().c_str());
                }
                else 
                {
                    auto& objectStream = outcome.GetResultWithOwnership().GetBody();

                    std::ofstream fileStream(m_localfile.fullPath.c_str(), std::ios::out | std::ios::binary);

                    if (fileStream) 
                    {
                        fileStream << objectStream.rdbuf();
                        fileStream.close();
                        INFOLOG("File downloaded successfully:",m_localfile.fullPath);
                    } 
                    else 
                    {
                        ERRORLOG("Local file write failed:",m_localfile.fullPath);
                        throw aux::InternalErrorException("s3 get failed");
                    }
                }
            }

            if ((m_localsize = aux::getFileSize(m_localfile.fullPath.c_str())) == -1)
            {
                ERRORLOG("Invalid local file size:",m_localfile.fullPath)
                throw aux::InternalErrorException("local file calculate size failed");
            }
        }
        catch(...)
        {
            ERRORLOG("Error while IO operation",uri,bucket,mode);
            throw ;
        }
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::write(const void *src, const uint32_t size, int *ecode)
    {
        INFOLOG("S3IODevice::write:",m_localfile.fullPath);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ecode)
            *ecode = error::NoError;

        if (!(m_mode & io::WriteOnly))
        {
            *ecode = error::WriteNotSupported;
            return 0;
        }
        FILE * f = fopen(m_localfile.fullPath.c_str(), "r+b");
        if (f == NULL)
            goto bad_end;

        if (fseek(f, (int)m_pos, SEEK_SET) != 0)
            goto bad_end;

        fwrite(src, 1, size, f);
        m_pos += size;
        m_localsize += size;
        m_altered = true;
        fclose(f);
        m_fileWriteCount++;
        if(m_fileWriteCount > 10)
        {
            flush();
            m_fileWriteCount = 0;
        } 
        return size;

    bad_end:
        if (f != NULL)
            fclose(f);
        ERRORLOG("Error while writing file:",m_localfile.fullPath);
        *ecode = error::UnknownError;
        return 0;
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::read(void *dst, const uint32_t size, int *ecode) const
    {
        INFOLOG("S3IODevice::read",m_localfile.fullPath);
        std::lock_guard<std::mutex> lock(m_mutex);
        uint32_t readSize = 0;
        if (ecode)
            *ecode = error::NoError;

        if (!(m_mode & io::ReadOnly))
        {
            *ecode = error::ReadNotSupported;
            return 0;
        }

        FILE * f = fopen(m_localfile.fullPath.c_str(), "rb");
        if (f == NULL)
            goto bad_end;

        readSize = (uint32_t)(m_pos + size > m_localsize ? m_localsize - m_pos : size);

        if (fseek(f, (int)m_pos, SEEK_SET) != 0)
            goto bad_end;

        fread(dst, 1, readSize, f);
        m_pos += readSize;
        fclose(f);
        return readSize;

    bad_end:
        if (f != NULL)
            fclose(f);
        ERRORLOG("Error while reading file:",m_localfile.fullPath);
        *ecode = error::UnknownError;
        return 0;
    }

    int STORAGE_METHOD_CALL nx_spl::S3IODevice::seek(uint64_t pos, int *ecode)
    {
        DEBUGLOG("S3IODevice::seek");
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ecode)
            *ecode = error::NoError;

        if ((long long)pos > m_localsize)
        {
            *ecode = error::UnknownError;
            return 0;
        }
        m_pos = pos;
        return 1;
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

        long long filesize;
        if ((filesize = aux::getFileSize(m_localfile.fullPath.c_str())) == -1)
        {
            if (ecode)
                *ecode = error::UnknownError;
            ERRORLOG("Unable to get file size:",m_localfile.fullPath);
            return 0;
        }
        INFOLOG("local file size:",m_localfile.fullPath, filesize);
        return static_cast<uint32_t>(filesize);
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
            try
            {
                std::shared_ptr<Aws::IOStream> inputData =
                Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
                    m_localfile.fullPath.c_str(),
                    std::ios_base::in | std::ios_base::binary);

                if (!*inputData) {
                    ERRORLOG("Unable to read local file:",m_localfile.fullPath);
                    throw aux::InternalErrorException("Error unable to read local file ");
                }

                Aws::S3::Model::PutObjectRequest request;
                request.SetBucket(m_bucket);
                request.SetKey(m_uri);
                request.SetBody(inputData);

                Aws::S3::Model::PutObjectOutcome outcome = m_impl->PutObject(request);

                if (!outcome.IsSuccess()) 
                {
                    ERRORLOG("Unable to upload file:",m_uri,outcome.GetError().GetMessage().c_str());
                    throw aux::InternalErrorException(outcome.GetError().GetMessage().c_str());
                }
                else 
                {
                    INFOLOG("Successfully uploaded file:",m_localfile.fullPath,m_uri,m_bucket);
                }
            }
            catch(aux::InternalErrorException &e)
            {
                ERRORLOG(e.what());
            }
        }
    }

    S3IODevice::~S3IODevice()
    {
        DEBUGLOG("S3IODevice::~S3IODevice");
        flush();
        remove(m_localfile.fullPath.c_str());
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
        DEBUGLOG("create  NXPlugin Instance");
        return new nx_spl::S3StorageFactory();
    }
}