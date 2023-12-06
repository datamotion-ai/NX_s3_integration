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

bool m_bucketSizeNeedUpdate = true;

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
            // std::cout << __LINE__ << ":" << __func__ << std::endl;
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
                // std::cout << __LINE__ << ":" << __func__ << " : " << "NetworkException" << ",msg:" << s << std::endl;
            }

            virtual const char* what() const NOEXCEPT
            {
                // std::cout << __LINE__ << ":" << __func__ << " : " << "what" << std::endl;
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
                // std::cout << __LINE__ << ":" << __func__ << " : " << "BadUrlException" << ",msg:" << s << std::endl;
            }

            virtual const char* what() const NOEXCEPT
            {
                // std::cout << __LINE__ << ":" << __func__ << " : " << "what" << std::endl;
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
                // std::cout << __LINE__ << ":" << __func__ << " : " << "ConnectException" << ",msg:" << s << std::endl;
            }

            virtual const char* what() const NOEXCEPT
            {
                // std::cout << __LINE__ << ":" << __func__ << " : " << "what" << std::endl;
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
                // std::cout << __LINE__ << ":" << __func__ << " : " << "InternalErrorException" << ",msg:" << s << std::endl;
            }

            virtual const char* what() const NOEXCEPT
            {
                // std::cout << __LINE__ << ":" << __func__ << " : " << "what" << std::endl;
                return runtime_error::what();
            }
        };

        void dirFromUri(
        const std::string   &uri, // In
        std::string         *dir, // Out. directory name
        std::string         *file // Out. file name
        )
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << "uri:" << uri << std::endl;
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
            // std::cout << __LINE__ << ":" << __func__ << " : " << "nameAndPath.fullPath:" << nameAndPath.fullPath << std::endl;
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
            // std::cout << __LINE__ << ":" << __func__ << " : " << "uri:" << uri << std::endl;
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
                std::cout << __LINE__ << ":" << __func__ << " : " << "implPtrType is nullptr!!" << std::endl;
            }
            // std::cout << __LINE__ << ":" << __func__ << " : "  <<"found:" << found<< std::endl;
            return found;
        }

        bool remoteFileExist(const std::string& uri,const std::string& bucket,const implPtrType& impl)
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << "uri:" << uri << std::endl;

            if(impl.get() == nullptr)
                throw std::runtime_error("implPtrType is nullptr!!");
            
            std::string dir;
            std::string file;
            dirFromUri(uri, &dir, &file);

            if (file.empty()) // Uri shouldn't end with '/'
                throw std::runtime_error("empty file name");

            bool found = false;
            Aws::S3::Model::ListObjectsRequest request;
            request.SetBucket(bucket);
            
            auto outcome = impl->ListObjects(request);
            if (outcome.IsSuccess()) {
                auto objects = outcome.GetResult().GetContents();
                for (const auto& object : objects) {
                    if(file == object.GetKey())
                    {
                        found = true;
                        break;
                    }
                }
            }
            else {
                throw aux::BadUrlException("Remote dir not exists");
            }
            return found;
        }

        uint64_t remoteFolderSize(const std::string& uri,const std::string& bucket,const implPtrType& impl)
        {
            std::cout << __LINE__ << ":" << __func__ << " : " << "uri:" << uri << std::endl;

            if(impl.get() == nullptr)
                throw std::runtime_error("implPtrType is nullptr!!");

            if (uri.empty()) 
                throw std::runtime_error("empty file name");

            uint64_t totalSize = 0;
            Aws::S3::Model::ListObjectsRequest request;
            request.SetBucket(bucket);
            request.WithPrefix(uri);
            
            
            auto outcome = impl->ListObjects(request);
            if (outcome.IsSuccess()) 
            {
                for (const auto& object : outcome.GetResult().GetContents())
                {
                    // std::cout << __LINE__ << ":" << __func__ << " : " << "object.GetKey():" << object.GetKey() << std::endl;
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
                        std::cerr << "Failed to get metadata for object " << object.GetKey() << ": "
                                << headObjectOutcome.GetError().GetMessage() << std::endl;
                    }
                }
            }
            else 
            {
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
                    // std::cout << __LINE__ << ":" << __func__ << " : " << "dir:" << dir << std::endl;
                    Aws::S3::Model::PutObjectRequest request;
                    request.SetBucket(bucket);
                    request.SetKey(dir);
                    const auto response = impl->PutObject(request);
                    ret = response.IsSuccess();
                    if (!response.IsSuccess()) {
                        std::cout << __LINE__ << ":" << __func__ << "Error: PutObject: " <<
                            response.GetError().GetMessage() << std::endl;
                    }
                    else 
                    {
                        std::cout << "Directory created successfully: " << dir << " to bucket '"
                            << bucket << "." << std::endl;
                        ret = true;
                    }
                }
                else
                {
                    // std::cout << "Directory already exist: " << dir << " to bucket '" << bucket << "." << std::endl;
                    ret = true;
                }
            }
            else
            {
               std::cout << __LINE__ << ":" << __func__ << " : " << "implPtrType is nullptr!!" << std::endl; 
            }
            return ret;
        }

        bool createBucket(const std::string  &bucket,implPtrType &impl)
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << " bucket:" << bucket << std::endl;
            bool ret = false;
            if(impl.get() != nullptr)
            {
                Aws::S3::Model::CreateBucketRequest request;
                request.SetBucket(bucket);
                Aws::S3::Model::CreateBucketOutcome outcome = impl->CreateBucket(request);
                if (!outcome.IsSuccess()) {
                    auto err = outcome.GetError();
                    std::cout << "Error: CreateBucket: " << err.GetExceptionName() << ": " << err.GetMessage() << std::endl;
                }
                else 
                {
                    std::cout << "Created bucket " << bucket << " in the specified AWS Region." << std::endl;
                    ret =true;
                }
            }
            else
            {
                std::cout << "Error: implPtrType is nullptr!! " << std::endl;
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
            // std::cout << __LINE__ << ":" << __func__ << " : " << " url:" << url << " uaccessKey:" <<  uaccessKey << " usecreatKey:"<< usecreatKey << std::endl;
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
                        for (const auto& object : objects) {
                            // std::cout << __LINE__ << ":" << __func__ << "object.GetName():" << object.GetName() << std::endl;
                            if(object.GetName() == bucket)
                            {
                                std::cout << __LINE__ << ":" << __func__ << "Bucket found in s3 storage:" << bucket << std::endl;
                                bucketFound = true;
                                break;
                            } 
                        }
                        if((bucketFound == false) && (createBucket(bucket,impl) == false))
                        {
                            return false;
                        }
                        else
                        {
                            std::cout << "SuccessFully establish s3 connection with host: " << url << std::endl;
                            return true;
                        }
                    }
                    else {
                        std::cout << "S3Client::ListBuckets: Failed with error: " << outcome.GetError() << std::endl;
                    }
                }
                else
                {
                    std::cout << "Error: implPtrType is nullptr!! " << std::endl;
                }
                return false;
            }
            catch (const std::exception& e)
            {
                throw aux::NetworkException(e.what());
            }
        }
    }

    // S3StorageFactory
    S3StorageFactory::S3StorageFactory()
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3StorageFactory::S3StorageFactory" << std::endl;
        Aws::SDKOptions options;
        // options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Debug;
        std::cout << "Creating S3 connection...!!" << std::endl;
        Aws::InitAPI(options);
        std::srand((unsigned int) time(0));
    }

    const char** STORAGE_METHOD_CALL S3StorageFactory::findAvailable() const
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3StorageFactory::findAvailable" << std::endl;
        assert(false);
        return nullptr;
    }

    Storage *STORAGE_METHOD_CALL S3StorageFactory::createStorage(const char *url, int *ecode)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3StorageFactory::createStorage"
        Storage* ret = nullptr;
        *ecode = error::NoError;
        try
        {
            ret = new S3Storage(url);
        }
        catch (const std::bad_alloc&)
        {
            if (ecode)
                *ecode = error::UnknownError;
            return nullptr;
        }
        catch (const aux::NetworkException&)
        {
            if (ecode)
                *ecode = error::UnknownError;
            return nullptr;
        }
        catch (const aux::BadUrlException&)
        {
            if (ecode)
                *ecode = error::UrlNotExists;
            return nullptr;
        }
        return ret;
        return nullptr;//Storage * STORAGE_METHOD_CALL();
    }
    const char *STORAGE_METHOD_CALL S3StorageFactory::storageType() const
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3StorageFactory::storageType" << std::endl;
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
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3StorageFactory::lastErrorMessage"
        // << ",ecode:" << ecode << std::endl;
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
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3StorageFactory::queryInterface:" << interfaceID.bytes << std::endl;
        if (std::memcmp(&interfaceID,
                        &IID_StorageFactory,
                        sizeof(nxpl::NX_GUID)) == 0)
        {
            addRef();
            // std::cout << __LINE__ << ":--->1" << __func__ << " : " << "S3StorageFactory::queryInterface" << std::endl;
            return static_cast<S3StorageFactory*>(this);
        }
        else if (std::memcmp(&interfaceID,
                                &nxpl::IID_PluginInterface,
                                sizeof(nxpl::IID_PluginInterface)) == 0)
        {
            addRef();
            // std::cout << __LINE__ << "------------>2:" << __func__ << " : " << "S3StorageFactory::queryInterface" << std::endl;
            return static_cast<nxpl::PluginInterface*>(this);
        }
        // std::cout << __LINE__ << "------->3:" << __func__ << " : " << "S3StorageFactory::queryInterface" << std::endl;
        return nullptr;
    }
    int S3StorageFactory::addRef() const
    {
        return p_addRef();
    }
    int S3StorageFactory::releaseRef() const
    {
        return p_releaseRef();
    }
    S3Storage::S3Storage(const std::string &url) : 
    m_available(false),
    m_freebucketSize(0)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::S3Storage: url :" << url << std::endl;
        try
        {
            aux::Url u;
            try
            {
                u = aux::Url::fromString(url);
            }
            catch (const std::exception& e)
            {
                throw aux::BadUrlException(e.what());
            }
            if(u.host.empty() || u.uaccessKey.empty() || u.usecreatKey.empty())
            {
                throw aux::BadUrlException("Invalid Url or credentials!!");
            }

            m_url = u.host;
            m_accessKey = u.uaccessKey.c_str();
            m_secretKey = u.usecreatKey.c_str();
            m_bucket = u.path.empty() ?  BUCKET_NAME : u.path.c_str();

            m_available = aux::establishS3Connection(m_url,m_accessKey,m_secretKey,m_bucket, m_impl);
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
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::isAvailable" << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_available = false;
        if(m_impl != nullptr)
        {
            auto outcome = m_impl->ListBuckets();
            if (outcome.IsSuccess()) {
                auto objects = outcome.GetResult().GetBuckets();
                for (const auto& object : objects) {
                    if(object.GetName() == m_bucket)
                    {
                        m_available = true;
                        break;
                    }
                }
            }
            else {
                std::cout << "S3Client::ListBuckets: Failed with error: " << outcome.GetError() << std::endl;
            }
            if(m_available == false)
            {
                std::cout << __LINE__ << ":" << __func__ << "Connection lost!! "  << std::endl;
                m_available = aux::establishS3Connection(m_url,m_accessKey,m_secretKey,m_bucket,m_impl);
            }
        }
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::isAvailable:" << m_available << std::endl;
        return m_available;
    }
    IODevice *STORAGE_METHOD_CALL S3Storage::open(const char *uri, int flags, int *ecode) const
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::open" << ",uri:" << uri << ",flags:" << flags << std::endl;
        *ecode = error::NoError;
        IODevice *ret = nullptr;
        if (!getAvail())
        {
            *ecode = error::StorageUnavailable;
            return nullptr;
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
        catch (const aux::BadUrlException&)
        {
            *ecode = error::UrlNotExists;
            return nullptr;
        }
        catch (...)
        {
            *ecode = error::UnknownError;
            return nullptr;
        }
    }
    
    uint64_t STORAGE_METHOD_CALL S3Storage::getFreeSpace(int *ecode) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ecode)
        *ecode = error::NoError;
        uint64_t totalSize = 0;
        
        if(m_bucketSizeNeedUpdate)
        {
            try
            {
                totalSize = aux::remoteFolderSize(m_bucket + "/",m_bucket,m_impl);
                std::cout << __LINE__ << ":" << __func__ << " totalSize: " 
                << totalSize
                << std::endl;
            }
            catch(const std::runtime_error& e)
            {
                std::cout << __LINE__ << ":" << __func__ << " Error: " << e.what() << '\n';
            }
            m_freebucketSize = getTotalSpace(ecode) - totalSize;
            m_bucketSizeNeedUpdate = false;
        }
        
        std::cout << __LINE__ << ":" << __func__ << " m_freebucketSize: " 
                << m_freebucketSize
                << std::endl;
        return m_freebucketSize; // for tests 
    }

    uint64_t STORAGE_METHOD_CALL S3Storage::getTotalSpace(int *ecode) const
    {
        if (ecode)
        *ecode = error::NoError;
        return m_freebucketSize;
    }
    int STORAGE_METHOD_CALL S3Storage::getCapabilities() const
    {
        int ret = 0;
        ret |= cap::ListFile;
        ret |= cap::WriteFile;
        ret |= cap::ReadFile;
        ret |= cap::RemoveFile;
        return ret;
    }

    void STORAGE_METHOD_CALL S3Storage::removeFile(const char *url, int *ecode)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::removeFile" 
        // << " url:" << url << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return;
        
        *ecode = error::NoError;
        
        if(m_impl.get() != nullptr)
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << "dir:" << dir << std::endl;
            uint64_t size = fileSize(url,ecode);
            Aws::S3::Model::DeleteObjectRequest request;
            request.WithBucket(m_bucket)
                    .WithKey(url);

            const auto response = m_impl->DeleteObject(request);
            if (!response.IsSuccess()) {
                std::cout << __LINE__ << ":" << __func__ << "Error: DeleteObject: " <<
                    response.GetError().GetMessage() << std::endl;
                    *ecode = error::UnknownError;
            }
            else 
            {
                *ecode = error::NoError;
                m_freebucketSize = m_freebucketSize + size;
                std::cout << __LINE__ << ":" << __func__ << " m_freebucketSize: " 
                << m_freebucketSize
                << std::endl;
            }
        }
        else
        {
            std::cout << __LINE__ << ":" << __func__ << " : " << "implPtrType is nullptr!!" << std::endl; 
            *ecode = error::UnknownError;
        }

        return ;
    }

    void STORAGE_METHOD_CALL S3Storage::removeDir(const char *url, int *ecode)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::removeDir" 
        // << " url:" << url << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return;
        
        if(m_impl.get() != nullptr)
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << "dir:" << dir << std::endl;
            Aws::S3::Model::DeleteObjectRequest request;
            request.WithBucket(m_bucket)
                    .WithKey(url);

            const auto response = m_impl->DeleteObject(request);
            if (!response.IsSuccess()) {
                std::cout << __LINE__ << ":" << __func__ << "Error: DeleteObject: " <<
                    response.GetError().GetMessage() << std::endl;
                    *ecode = error::UnknownError;
            }
            else 
            {
                *ecode = error::NoError;
                m_bucketSizeNeedUpdate = true;
            }
        }
        else
        {
            std::cout << __LINE__ << ":" << __func__ << " : " << "implPtrType is nullptr!!" << std::endl; 
            *ecode = error::UnknownError;
        }
        return ;
    }

    void STORAGE_METHOD_CALL S3Storage::renameFile(const char *oldUrl, const char *newUrl, int *ecode)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::renameFile"
        // << ", oldUrl:" << oldUrl
        // << " ,newUrl:"<< newUrl << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return;
        
        if(m_impl.get() != nullptr)
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << "dir:" << dir << std::endl;
            Aws::S3::Model::CopyObjectRequest copyRequest;
            copyRequest.WithBucket(m_bucket)
                        .WithCopySource(m_bucket + "/" + oldUrl)
                        .WithKey(newUrl);

            const auto response = m_impl->CopyObject(copyRequest);
            if (!response.IsSuccess()) {
                std::cout << __LINE__ << ":" << __func__ << "Error: CopyObject: " <<
                    response.GetError().GetMessage() << std::endl;
                *ecode = error::UnknownError;
            }
            else 
            {
                Aws::S3::Model::DeleteObjectRequest request;
                request.WithBucket(m_bucket)
                        .WithKey(oldUrl);

                const auto response = m_impl->DeleteObject(request);
                if (!response.IsSuccess()) {
                    std::cout << __LINE__ << ":" << __func__ << "Error: DeleteObject: " <<
                        response.GetError().GetMessage() << std::endl;
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
            std::cout << __LINE__ << ":" << __func__ << " : " << "implPtrType is nullptr!!" << std::endl; 
            *ecode = error::UnknownError;
        }
        return ;
    }

    FileInfoIterator *STORAGE_METHOD_CALL S3Storage::getFileIterator(const char *dirUrl, int *ecode) const
    {
        std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::getFileIterator" << ",dirUrl:" << dirUrl << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return nullptr;

        if(m_impl.get() == nullptr)
            return nullptr;
            
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
                std::cout << __LINE__ << ":" << __func__ << " : " << "line:" << line << std::endl;
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
                std::cout << __LINE__ << ":" << __func__ << " : " << "line:" << line << std::endl;
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
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::fileExists" << " url:" << url << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return 0;

        try
        {
            if (!aux::remoteUriExists(url,m_bucket, m_impl))
                return 0;
            else
                return 1;
        }
        catch (const std::exception&)
        {
            *ecode = error::UnknownError;
            return 0;
        }
    }

    int STORAGE_METHOD_CALL S3Storage::dirExists(const char *url, int *ecode) const
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::dirExists" << " url:" << url << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return 0;
        
        if(aux::createDir(url,m_bucket,m_impl))
        {
            return 1;
        }
        else
        {
            std::cout << __LINE__ << ":" << __func__ << ",Error: not able to create directory, url:" << url << std::endl;
            return 0;
        }
    }

    uint64_t STORAGE_METHOD_CALL S3Storage::fileSize(const char *url, int *ecode) const
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::fileSize" << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(aux::checkECode(ecode, getAvail()) != nx_spl::error::NoError)
            return 0;

        uint64_t size = 0;
        if(m_impl.get() != nullptr)
        {
            // std::cout << __LINE__ << ":" << __func__ << " : " << "dir:" << dir << std::endl;
            Aws::S3::Model::HeadObjectRequest request;
            request.SetBucket(m_bucket);
            request.SetKey(url);
            const auto response = m_impl->HeadObject(request);
            if (!response.IsSuccess()) {
                std::cout << __LINE__ << ":" << __func__ << "Error: HeadObject: " <<
                    response.GetError().GetMessage() << std::endl;
            }
            else 
            {
                size = response.GetResult().GetContentLength();
                std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::fileSize"
                << ",url:" << url << ",size:" << size << std::endl;
                m_freebucketSize = m_freebucketSize - size;
                std::cout << __LINE__ << ":" << __func__ << " m_freebucketSize: " 
                << m_freebucketSize
                << std::endl;
            }
        }
        else
        {
            std::cout << __LINE__ << ":" << __func__ << " : " << "implPtrType is nullptr!!" << std::endl; 
        }
        return size;
    }

    void *S3Storage::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::queryInterface" << std::endl;
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
        return nullptr;
    }
    int S3Storage::addRef() const
    {
        //std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::addRef" << std::endl;
        return p_addRef();
    }
    int S3Storage::releaseRef() const
    {
        //std::cout << __LINE__ << ":" << __func__ << " : " << "S3Storage::releaseRef" << std::endl;
        return p_releaseRef();
    }
    S3Storage::~S3Storage()
    {
        
    }

    nx_spl::S3IODevice::S3IODevice(const char *uri, 
                                    const char *bucket, 
                                    int mode, 
                                    const  implPtrType &impl
        ): m_mode(mode),
        m_bucket(bucket),
        m_pos(0),
        m_altered(false),
        m_localsize(0),
        m_impl(impl)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::S3IODevice" << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        try
        {
            std::string remoteDir, remoteFile;
            aux::dirFromUri(uri, &remoteDir, &remoteFile);
            m_localfile = aux::localUniqueFilePath("--" + remoteFile);
            m_uri = remoteDir + "/" + remoteFile;
            // std::cout << __LINE__ << ":" << __func__ << " : " 
            //     << ",m_localfile:" << m_localfile.fullPath 
            //     << ",m_uri:" << m_uri 
            //     << ",mode:" << mode << std::endl;

            bool fileExists = false;

            try
            {
                fileExists = aux::remoteUriExists(uri,m_bucket,m_impl);
            }
            catch(const aux::BadUrlException& e)
            {
                std::cout << __LINE__ << ":" << __func__ << " : " 
                        << "BadUrlException" 
                        <<",e.what():" << e.what() << std::endl;

                if(false == aux::createDir(remoteDir,m_bucket,m_impl))
                    throw aux::InternalErrorException(e.what());
                else fileExists = false;
            }
            catch (const std::exception& e)
            {
                std::cout << __LINE__ << ":" << __func__ << " : " 
                            << "InternalErrorException" 
                            <<",e.what():" << e.what() << std::endl;
                throw aux::InternalErrorException(e.what());
            }

            if(mode & io::WriteOnly)
            {
                if (!fileExists)
                {
                    FILE *f = fopen(m_localfile.fullPath.c_str(), "wb");
                    if (f == NULL)
                    {
                        std::cout << __LINE__ << ":" << __func__ << " : " 
                            << "couldn't create local temporary file" 
                            <<",m_localfile.fullPath:" << m_localfile.fullPath << std::endl;
                        throw aux::InternalErrorException("couldn't create local temporary file");
                    }
                    fclose(f);

                    std::shared_ptr<Aws::IOStream> inputData =
                    Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
                        m_localfile.fullPath.c_str(),
                        std::ios_base::in | std::ios_base::binary);

                    if (!*inputData) {
                        std::cout << __LINE__ << ":" << __func__ << " : "  << "Error unable to read file " << m_localfile.fullPath << std::endl;
                        throw aux::InternalErrorException("Error unable to read file ");
                    }

                    Aws::S3::Model::PutObjectRequest request;
                    request.SetBucket(m_bucket);
                    request.SetKey(uri);
                    request.SetBody(inputData);

                    Aws::S3::Model::PutObjectOutcome outcome =
                        m_impl->PutObject(request);

                    if (!outcome.IsSuccess()) {
                        std::cout << __LINE__ << ":" << __func__ << " : " << "S3Client::putObject :Error: PutObject: " << outcome.GetError().GetMessage() << std::endl;
                        throw aux::InternalErrorException(outcome.GetError().GetMessage().c_str());
                    }
                    else {
                        std::cout << __LINE__ << ":" << __func__ << " : " << "Uploaded file '" << uri << "' to bucket '" << m_bucket << "'." << std::endl;
                    }
                }
                else
                {
                    Aws::S3::Model::GetObjectRequest request;
                    request.SetBucket(m_bucket);
                    request.SetKey(uri);

                    Aws::S3::Model::GetObjectOutcome outcome =
                        m_impl->GetObject(request);

                    if (!outcome.IsSuccess()) {
                        std::cout << __LINE__ << ":" << __func__ << " : " << "S3Client::putObject :Error: GetObject: " << outcome.GetError().GetMessage() << std::endl;
                        throw aux::InternalErrorException(outcome.GetError().GetMessage().c_str());
                    }
                    else 
                    {
                        auto& objectStream = outcome.GetResultWithOwnership().GetBody();

                        // Open a file stream to write the object's contents
                        std::ofstream fileStream(m_localfile.fullPath.c_str(), std::ios::out | std::ios::binary);

                        if (fileStream) {
                            fileStream << objectStream.rdbuf();
                            fileStream.close();

                            std::cout << "Object downloaded and stored in file: " << m_localfile.fullPath << std::endl;
                        } else {
                            std::cout << __LINE__ << ":" << __func__ << " : " << "Failed to open file for writing: " << m_localfile.fullPath << std::endl;
                            throw aux::InternalErrorException("s3 get failed");
                        }
                    }
                }
            }
            else if(mode & io::ReadOnly)
            {
                FILE *f = fopen(m_localfile.fullPath.c_str(), "wb");
                if (f == NULL)
                {
                    std::cout << __LINE__ << ":" << __func__ << " : " 
                        << "couldn't create local temporary file" 
                        <<",m_localfile.fullPath:" << m_localfile.fullPath << std::endl;
                    throw aux::BadUrlException("couldn't create local temporary file");
                }

                Aws::S3::Model::GetObjectRequest request;
                request.SetBucket(m_bucket);
                request.SetKey(m_uri);

                Aws::S3::Model::GetObjectOutcome outcome =
                    m_impl->GetObject(request);

                if (!outcome.IsSuccess()) {
                    std::cout << __LINE__ << ":" << __func__ << " : " << "S3Client::putObject :Error: GetObject: " << outcome.GetError().GetMessage() << std::endl;
                    throw aux::BadUrlException(outcome.GetError().GetMessage().c_str());
                }
                else 
                {
                    auto& objectStream = outcome.GetResultWithOwnership().GetBody();

                    // Open a file stream to write the object's contents
                    std::ofstream fileStream(m_localfile.fullPath.c_str(), std::ios::out | std::ios::binary);

                    if (fileStream) {
                        fileStream << objectStream.rdbuf();
                        fileStream.close();

                        std::cout << "Object downloaded and stored in file: " << m_localfile.fullPath << std::endl;
                    } else {
                        std::cout << __LINE__ << ":" << __func__ << " : " << "Failed to open file for writing: " << m_localfile.fullPath << std::endl;
                        throw aux::InternalErrorException("s3 get failed");
                    }
                }
            }


            // calculate local file size
            if ((m_localsize = aux::getFileSize(m_localfile.fullPath.c_str())) == -1)
            {
                std::cout << __LINE__ << ":" << __func__ << " : "
                    << "local file calculate size failed"
                    << ",m_localfile.fullPath:" << m_localfile.fullPath << std::endl;
                throw aux::InternalErrorException("local file calculate size failed");
            }
        }
        catch(...)
        {
            std::cout << __LINE__ << ":" << __func__ << " : " << "Error:" << std::endl;
            throw ;
        }
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::write(const void *src, const uint32_t size, int *ecode)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::write" << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ecode)
            *ecode = error::NoError;

        if (!(m_mode & io::WriteOnly))
        {
            *ecode = error::WriteNotSupported;
            return 0;
        }
        // std::cout << __LINE__ << ":" << __func__ << " :m_localfile.fullPath: " << m_localfile.fullPath << std::endl;
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
        return size;

    bad_end:
        if (f != NULL)
            fclose(f);
        *ecode = error::UnknownError;
        return 0;
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::read(void *dst, const uint32_t size, int *ecode) const
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::read" << std::endl;
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
        *ecode = error::UnknownError;
        return 0;
    }

    int STORAGE_METHOD_CALL nx_spl::S3IODevice::seek(uint64_t pos, int *ecode)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::seek" << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ecode)
            *ecode = error::NoError;

        if ((long long)pos > m_localsize)
        {
            *ecode = error::UnknownError;
            return 0;
        }

        m_pos = pos;
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::seek" << ",pos:" << pos << std::endl;
        return 1;
    }

    int STORAGE_METHOD_CALL nx_spl::S3IODevice::getMode() const
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::getMode" << std::endl;
        return m_mode;
    }

    uint32_t STORAGE_METHOD_CALL nx_spl::S3IODevice::size(int *ecode) const
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::size" << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (ecode)
            *ecode = error::NoError;

        long long ret;
        if ((ret = aux::getFileSize(m_localfile.fullPath.c_str())) == -1)
        {
            if (ecode)
                *ecode = error::UnknownError;
            return 0;
        }
        // std::cout << __LINE__ << ":" << __func__ << " : " 
        // << "m_localfile.fullPath:" << m_localfile.fullPath 
        // << ",Size:" << ret << std::endl;
        return static_cast<uint32_t>(ret);
    }

    void *nx_spl::S3IODevice::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::queryInterface" << std::endl;
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
        return nullptr;
    }

    int nx_spl::S3IODevice::addRef() const
    {
        return p_addRef();
    }

    int nx_spl::S3IODevice::releaseRef() const
    {
        return p_releaseRef();
    }

    void nx_spl::S3IODevice::flush()
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::flush: m_uri" << m_uri << std::endl;
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_altered)
        {
            try
            {
                std::shared_ptr<Aws::IOStream> inputData =
                Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
                    m_localfile.fullPath.c_str(),
                    std::ios_base::in | std::ios_base::binary);

                if (!*inputData) {
                    std::cout << __LINE__ << ":" << __func__ << " : "  << "Error unable to read file " << m_localfile.fullPath << std::endl;
                    throw aux::InternalErrorException("Error unable to read file ");
                }

                Aws::S3::Model::PutObjectRequest request;
                request.SetBucket(m_bucket);
                request.SetKey(m_uri);
                request.SetBody(inputData);

                Aws::S3::Model::PutObjectOutcome outcome =
                    m_impl->PutObject(request);

                if (!outcome.IsSuccess()) {
                    std::cout << __LINE__ << ":" << __func__ << " : " << "S3Client::putObject :Error: PutObject: " << outcome.GetError().GetMessage() << std::endl;
                    throw aux::InternalErrorException(outcome.GetError().GetMessage().c_str());
                }
                else {
                    std::cout << __LINE__ << ":" << __func__ << " : " << "Uploaded file '" << m_uri << "' to bucket '" << m_bucket << "'." << std::endl;
                }
            }
            catch(aux::InternalErrorException &e)
            {
                std::cout << __LINE__ << ":" << __func__ << " : " 
                        << "InternalErrorException" 
                        <<",e.what():" << e.what() << std::endl;
            }
            
        }
    }

    S3IODevice::~S3IODevice()
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3IODevice::~S3IODevice" << std::endl;
        flush();
        remove(m_localfile.fullPath.c_str());
    }

    S3FileInfoIterator::S3FileInfoIterator(FileListType &&fileList, const std::string &baseDir):
    m_fileList(std::move(fileList)),
    m_curFile(m_fileList.cbegin())
    {
    }

    FileInfo *STORAGE_METHOD_CALL S3FileInfoIterator::next(int *ecode) const
    {
        std::cout << __LINE__ << ":" << __func__ << " : " << "S3FileInfoIterator::next" << std::endl;
        if (ecode)
            *ecode = nx_spl::error::NoError;

        if (m_curFile != m_fileList.cend())
        {
            std::cout << __LINE__ << ":" << __func__ << " : " 
            << "S3FileInfoIterator::next" 
            << "File name:"<< m_curFile->c_str() << std::endl;
            std::string line = m_curFile->c_str();
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
                std::cout << __LINE__ << ":" << __func__ << "Invalid file url:" << m_curFile->c_str() << std::endl;
            }
        }
        return nullptr;
    }
    
    void *nx_spl::S3FileInfoIterator::queryInterface(const nxpl::NX_GUID &interfaceID)
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3FileInfoIterator::queryInterface" << std::endl;
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
        return nullptr;
    }

    int nx_spl::S3FileInfoIterator::addRef() const
    {
        //std::cout << __LINE__ << ":" << __func__ << " : " << "S3FileInfoIterator::addRef" << std::endl;
        return p_addRef();
    }

    int nx_spl::S3FileInfoIterator::releaseRef() const
    {
        //std::cout << __LINE__ << ":" << __func__ << " : " << "S3FileInfoIterator::releaseRef" << std::endl;
        return p_releaseRef();  
    }

    nx_spl::S3FileInfoIterator::~S3FileInfoIterator()
    {
        // std::cout << __LINE__ << ":" << __func__ << " : " << "S3FileInfoIterator::~S3FileInfoIterator" << std::endl;
    }
}

extern "C"
{
    NX_PLUGIN_API nxpl::PluginInterface* createNXPluginInstance()
    {
        remove("logs.txt");

        freopen("logs.txt", "w", stdout);
        freopen("logs.txt", "w", stderr);

        std::cout << __LINE__ << ":" << __func__ << " : " << "createNXPluginInstance" << std::endl;
        return new nx_spl::S3StorageFactory();
    }
}