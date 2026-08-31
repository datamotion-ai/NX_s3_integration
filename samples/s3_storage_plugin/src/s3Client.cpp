#include <json/json.h>
#include "s3Client.h"
#include "common.hpp"
#include "ServerManager.h"
#include "ClearMemoryManager.h"

#define SYNC_FILE "Test.txt"
#define S3_CLIENT_ALLOCATION_TAG "S3Client"

namespace {

std::shared_ptr<Aws::S3::S3Client> createS3Client(
    const Aws::Auth::AWSCredentials& credentials,
    const Aws::S3::S3ClientConfiguration& clientConfig)
{
    return std::make_shared<Aws::S3::S3Client>(
        credentials,
        Aws::MakeShared<Aws::S3::S3EndpointProvider>(S3_CLIENT_ALLOCATION_TAG),
        clientConfig);
}

} // namespace

s3Client::s3Client(const std::string  &url, const std::string  &accessKey, const std::string  &secreatKey, const std::string  &bucket):
m_url(url),
m_accessKey(accessKey),
m_secretKey(secreatKey),
m_bucket(bucket),
m_totalSpaceUpdating(false),
m_running(false),
m_storageAvailable(false),
m_reUpdateSpace(false),
m_impl(nullptr),
m_spaceImpl(nullptr),
m_space(0),
m_threadPool(0,10),
m_uploadThreadAlive(false),
m_lastUploadProgress(std::chrono::steady_clock::now()),
m_lastQueuedToUploaded(std::chrono::steady_clock::now()),
m_pendingUploadWatchdog(false)
{
    INFOLOG("s3Client",url,accessKey,secreatKey,bucket);
}

s3Client::~s3Client()
{
    INFOLOG("~s3Client",m_url);
    stopThread();
    if(m_impl.get() != nullptr)
    {
       m_impl.reset(); 
    }
    if(m_spaceImpl.get() != nullptr)
    {
        m_spaceImpl.reset();
    }
}

bool s3Client::establishS3Connection()
{
    INFOLOG("establishS3Connection");
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Aws::S3::S3ClientConfiguration clientConfig;
        clientConfig.scheme = Aws::Http::Scheme::HTTPS;
        clientConfig.endpointOverride = Aws::String(m_url);

        Aws::Auth::AWSCredentials credentials;
        credentials.SetAWSAccessKeyId(m_accessKey);
        credentials.SetAWSSecretKey(m_secretKey);

        m_impl = createS3Client(credentials, clientConfig);

        if(m_impl.get() != nullptr)
        {
            bool bucketFound = false;
            auto outcome = m_impl->ListBuckets();
            if (outcome.IsSuccess()) 
            {
                auto objects = outcome.GetResult().GetBuckets();
                for (const auto& object : objects) 
                {
                    if(object.GetName() == m_bucket)
                    {
                        bucketFound = true;
                        break;
                    } 
                }
                if((bucketFound == false) && (createBucket() == false))
                {
                    ERRORLOG("Failed to create bucket S3",m_bucket);
                }
                else
                {
                    Aws::S3::Model::PutObjectRequest request;
                    request.SetBucket(m_bucket);
                    request.SetKey(SYNC_FILE);
                    auto input_data = Aws::MakeShared<Aws::StringStream>("StringStream");
                    *input_data << "Test";
                    request.SetBody(input_data);
                    Aws::S3::Model::PutObjectOutcome outcome = m_impl->PutObject(request);
                    if (!outcome.IsSuccess()) 
                    {
                        ERRORLOG("Unable to upload file:",m_bucket,SYNC_FILE,outcome.GetError().GetMessage().c_str());
                    }
                    else 
                    {
                        m_storageAvailable = true;
                        bucketFound = true;
                    }
                }
            }
            else
            {
                ERRORLOG("Failed to list bucket lists!! connection failed!!",outcome.GetError().GetMessage(),m_url + "/" + m_bucket);
            }

            if(!ServerManager::getInstance()->isServerIntialize())
            {
                bucketFound = true;
            }
            
            if(bucketFound == true)
            {
                INFOLOG("SuccessFully establish s3 connection with host: ",m_url + "/" + m_bucket);
                return true;
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr");
        }
    }
    catch(const std::exception& e)
    {
         ERRORLOG("Exception Error:",e.what());
    }
    return false;
}

bool s3Client::initializeConnection()
{
    INFOLOG("initializeConnection");
    bool started = false;
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Aws::S3::S3ClientConfiguration clientConfig;
        clientConfig.scheme = Aws::Http::Scheme::HTTPS;
        clientConfig.endpointOverride = Aws::String(m_url);

        #if defined (_WIN32)

            NTSTATUS(WINAPI *RtlGetVersion)(LPOSVERSIONINFOEXW);
            OSVERSIONINFOEXW osInfo;

            *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

            if (NULL != RtlGetVersion)
            {
                osInfo.dwOSVersionInfoSize = sizeof(osInfo);
                RtlGetVersion(&osInfo);
            }
            g_userAgent = "Wasabi/"  + std::string(VERSION) + " " + g_VMS + 
            + " Windows/" + std::to_string(osInfo.dwMajorVersion) + "." 
            + std::to_string( osInfo.dwMinorVersion) + "." + std::to_string( osInfo.dwBuildNumber);
            INFOLOG("userAgent",g_userAgent);

        #else
            struct utsname unameData;
            uname(&unameData);
            g_userAgent = "Wasabi/"  + std::string(VERSION) + " " + g_VMS + " LINUX/" + unameData.release;
            INFOLOG("userAgent",g_userAgent);
        #endif

        clientConfig.userAgent = g_userAgent;

        Aws::Auth::AWSCredentials credentials;
        credentials.SetAWSAccessKeyId(m_accessKey);
        credentials.SetAWSSecretKey(m_secretKey);

        m_impl = createS3Client(credentials, clientConfig);
        m_spaceImpl = createS3Client(credentials, clientConfig);

        if((m_impl.get() != nullptr) && (m_spaceImpl.get() != nullptr))
        {
            m_running = true;
            m_reUpdateSpace = false;
            m_uploadThreadAlive = true;
            uploadThread = std::thread(&s3Client::fileUploadThread, this);
            spaceThread = std::thread(&s3Client::updateRemoteFolderSize, this);
            m_keepAliveTimer.start(this,&s3Client::keepAliveActivator,ONE_MINUTE);
            INFOLOG("---->SuccessFully initialise s3 connection with host: ", m_url);
            started = true;
        }
        else
        {
            ERRORLOG("implPtrType is nullptr");
        }
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
    }
    if(started)
        requeueStagedUploads();
    return started;
}

bool s3Client::remoteUriExists(const std::string& uri) 
{
    DEBUGLOG(uri,m_bucket);
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool found = false;
        if(m_storageAvailable && (m_impl.get() != nullptr))
        {
            Aws::S3::Model::HeadObjectRequest request;
            request.WithBucket(m_bucket).WithKey(uri);
            const auto response = m_impl->HeadObject(request);
            found = response.IsSuccess();
        }
        else
        {
            if(!m_storageAvailable)
            {
                INFOLOG("Storage not available!!");
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
        }
        return found;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

bool s3Client::remoteDirExists(const std::string &uri)
{
    DEBUGLOG(uri,m_bucket);
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool found = false;
        if(m_storageAvailable && (m_impl.get() != nullptr))
        {
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
                DEBUGLOG("Directory found:",dir,m_bucket);
                return 1;
            }
            else
            {
                ERRORLOG("Failed to find directory:",dir,m_bucket);
                return 0;
            }
        }
        else
        {
            if(!m_storageAvailable)
            {
                INFOLOG("Storage not available!!");
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
            return 0;
        }
        return found;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

uint64_t s3Client::remoteFolderSize(bool update)
{
    DEBUGLOG("s3Client::remoteFolderSize");
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if((update || m_reUpdateSpace) && !m_totalSpaceUpdating)
        {
            if(m_storageAvailable) 
            {
                m_reUpdateSpace = false;
                spaceThread = std::thread(&s3Client::updateRemoteFolderSize, this);
            }
            else
            {
                m_reUpdateSpace = true;
            }
        }
        return m_space;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return 0;
    }
}

uint64_t s3Client::getRemoteFileSize(const std::string& uri)
{
    DEBUGLOG(uri,m_bucket);
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t size = 0;
        if(m_storageAvailable && (m_impl.get() != nullptr))
        {
            Aws::S3::Model::HeadObjectRequest request;
            request.SetBucket(m_bucket);
            request.SetKey(uri);
            const auto response = m_impl->HeadObject(request);
            if (!response.IsSuccess()) 
            {
                ERRORLOG("File not found",uri,response.GetError().GetMessage());
            }
            else 
            {
                size = response.GetResult().GetContentLength();
            }
        }
        else
        {
            if(!m_storageAvailable)
            {
                INFOLOG("Storage not available!!");
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
        }
        return size;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return 0;
    }
}

std::vector<std::string> s3Client::getobjectKeys(const char *dirUrl, const std::vector<std::string>& localObjects)
{
    DEBUGLOG("getobjectKeys",dirUrl);
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::vector<std::string> objectList;
        if(m_impl.get() == nullptr)
        {
            ERRORLOG("implPtrType is nullptr");
            return objectList;
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
            for (const auto& object : folderObjects) 
            {
                std::string line;
                std::string folderName;
                folderName.assign(object.GetPrefix().begin()+dir.size(),object.GetPrefix().end()-1);
                auto it = std::find_if( localObjects.begin(), localObjects.end(),
                    [&](const std::string& s)
                    {
                        return s.find(folderName) != std::string::npos;
                    }
                );

                if (it != localObjects.end())
                {
                    INFOLOG("-------->Found: ",folderName);
                    continue;
                }
                line.append(folderName.c_str());
                line.append(",");
                line.append(std::to_string(nx_spl::isDir));
                line.append(",");
                line.append("0");
                INFOLOG("------------->s3:",line);
                objectList.push_back(line);
                line.clear();
            }
            
            auto fileObjects = outcome.GetResult().GetContents();
            for (const auto& object : fileObjects) 
            {
                std::string line;
                std::string fileName;
                fileName.assign(object.GetKey().begin()+dir.size(),object.GetKey().end());
                auto it = std::find_if( localObjects.begin(), localObjects.end(),
                    [&](const std::string& s)
                    {
                        return s.find(fileName) != std::string::npos;
                    }
                );

                if (it != localObjects.end())
                {
                    INFOLOG("---------->Found: ",fileName);
                    continue;
                }
                line.append(fileName);
                line.append(",");
                line.append(std::to_string(nx_spl::isFile));
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
                INFOLOG("-------------->s3:",line);
                objectList.push_back(line);
                line.clear();
            }
        }
        return std::move(objectList);
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return std::vector<std::string>();
    }
}

bool s3Client::renameFile(const char *oldUrl, const char *newUrl)
{
    DEBUGLOG("renameFile",oldUrl,newUrl);
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        bool ret = false;
        if(m_storageAvailable && (m_impl.get() != nullptr))
        {
            Aws::S3::Model::CopyObjectRequest copyRequest;
            copyRequest.WithBucket(m_bucket)
                        .WithCopySource(m_bucket + "/" + oldUrl)
                        .WithKey(newUrl);

            const auto response = m_impl->CopyObject(copyRequest);
            if (!response.IsSuccess()) 
            {
                ERRORLOG("Failed to copy object",oldUrl,newUrl,m_bucket,response.GetError().GetMessage().c_str());
            }
            else 
            {
                Aws::S3::Model::DeleteObjectRequest request;
                request.WithBucket(m_bucket).WithKey(oldUrl);

                const auto response = m_impl->DeleteObject(request);
                if (!response.IsSuccess()) 
                {
                    ERRORLOG("Failed to delete object!!",oldUrl,m_bucket,response.GetError().GetMessage().c_str());
                }
                else 
                {
                    ret = true;
                }
            }
        }
        else
        {
            if(!m_storageAvailable)
            {
                INFOLOG("Storage not available!!");
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
        }
        return ret;    
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

bool s3Client::removeUrl(const char *url)
{
    DEBUGLOG("removeUrl",url);
    try
    {
        uint64_t fileSize = getRemoteFileSize(url);
        std::lock_guard<std::mutex> lock(m_mutex);
        bool ret = false;
        if(m_storageAvailable && (m_impl.get() != nullptr))
        {
            Aws::S3::Model::DeleteObjectRequest request;
            request.WithBucket(m_bucket)
                    .WithKey(url);

            const auto response = m_impl->DeleteObject(request);
            if (!response.IsSuccess()) 
            {
                ERRORLOG("Failed to delete file",url,m_bucket,response.GetError().GetMessage().c_str());
            }
            else 
            {
                INFOLOG("deleted file",url,m_bucket);
                if(fileSize > 0)
                {
                    m_space -= fileSize;
                }
                ret = true;
            }
        }
        else
        {
            if(!m_storageAvailable)
            {
                INFOLOG("Storage not available!!");
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
        }
        return ret;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

bool s3Client::addFileToUploadInQueue(const char *url)
{
    DEBUGLOG("s3Client::addFileToUploadInQueue",url);
    try
    {
        ensureUploadDispatcherRunning();
        std::lock_guard<std::mutex> lock(m_mutex);
        nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath("/"+ m_bucket + FILE_UPLOAD_JSON);
        if(!fs::exists(file.fullPath))
        {
            Json::Value root(Json::arrayValue);
            Json::Value jsonObject;
            jsonObject["host"] = m_url;
            jsonObject["bucket"] = m_bucket;
            Json::Value filesArray(Json::arrayValue);
            jsonObject["files"] = filesArray;
            root.append(jsonObject);
            std::ofstream outputFile(file.fullPath);
            if (!outputFile.is_open()) {
                ERRORLOG("Error opening JSON file:",file.fullPath);
                return false;
            }
            else
            {
                Json::StyledStreamWriter writer;
                writer.write(outputFile, root);
                outputFile.close();
            }
        }
        std::ifstream inputFile(file.fullPath);
        if (inputFile.is_open())
        {
            Json::Value root;
            Json::Reader reader;
            if (reader.parse(inputFile, root)) 
            {
                inputFile.close();
                if(root.isArray())
                {
                    bool hostFound = false;
                    for (auto& jsonObject : root) 
                    {
                        if(jsonObject["host"].isString() && (jsonObject["host"].asString() == m_url)  && 
                            jsonObject["bucket"].isString() && (jsonObject["bucket"].asString() == m_bucket)&& 
                            jsonObject["files"].isArray())
                        {
                            Json::Value& filesArray = jsonObject["files"];
                            // Avoid duplicate queue entries for the same object key.
                            bool alreadyQueued = false;
                            for(Json::ArrayIndex i = 0; i < filesArray.size(); ++i)
                            {
                                if(filesArray[i].isString() && filesArray[i].asString() == std::string(url))
                                {
                                    alreadyQueued = true;
                                    break;
                                }
                            }
                            if(!alreadyQueued)
                                filesArray.append(std::string(url));

                            std::ofstream outputFile(file.fullPath);
                            if (!outputFile.is_open()) {
                                ERRORLOG("Error opening JSON file:",file.fullPath);
                                return false;
                            }

                            Json::StyledStreamWriter writer;
                            writer.write(outputFile, root);
                            outputFile.close();
                            hostFound = true;
                            break;
                        }
                    }

                    if(hostFound == false)
                    {
                        Json::Value jsonObject;
                        jsonObject["host"] = m_url;
                        jsonObject["bucket"] = m_bucket;
                        Json::Value filesArray(Json::arrayValue);
                        filesArray.append(std::string(url));
                        jsonObject["files"] = filesArray;
                        root.append(jsonObject);
                        std::ofstream outputFile(file.fullPath);
                        if (!outputFile.is_open()) {
                            ERRORLOG("Error opening JSON file:",file.fullPath);
                            return false;
                        }
                        else
                        {
                            Json::StyledStreamWriter writer;
                            writer.write(outputFile, root);
                            outputFile.close();
                        }
                    }
                }
            }
            else
            {
                inputFile.close();
                ERRORLOG("Error parsing JSON from file:",reader.getFormattedErrorMessages());
                INFOLOG("Delete File:", file.fullPath);
                remove(file.fullPath.c_str());
                return false;
            }
            inputFile.close();
        }
        else
        {
            ERRORLOG("Error opening JSON file:",file.fullPath);
            return false;
        }
        m_lastQueuedToUploaded = std::chrono::steady_clock::now();
        m_pendingUploadWatchdog = true;
        return true;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

bool s3Client::uploadFile(const char *url, std::string fileName)
{
    DEBUGLOG("uploadFile",url,fileName);
    try
    {
        bool storageOk = false;
        s3PtrType implCopy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            storageOk = m_storageAvailable && (m_impl.get() != nullptr);
            implCopy = m_impl;
        }
        bool ret = false;
        if(storageOk && (implCopy.get() != nullptr))
        {
            std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
                                                                                    fileName.c_str(),
                                                                                    std::ios_base::in | std::ios_base::binary);
            if (!*inputData) 
            {
                ERRORLOG("Unable to read local file:",fileName);
            }
            else
            {
                INFOLOG("Uploading file!!",fileName);
                Aws::S3::Model::PutObjectRequest request;
                request.SetBucket(m_bucket);
                request.SetKey(url);
                request.SetBody(inputData);
                // Do not hold m_mutex across PutObject â€” that deadlocks/starves fileUploadThread.
                Aws::S3::Model::PutObjectOutcome outcome = implCopy->PutObject(request);
                #if defined(_WIN32)
                    static_cast<Aws::FStream*>(inputData.get())->close();
                #endif
                if (!outcome.IsSuccess()) 
                {
                    ERRORLOG("Unable to upload file:",url,outcome.GetError().GetMessage().c_str());
                }
                else 
                {
                    INFOLOG("Successfully uploaded file:",fileName,url,m_bucket);
                    ret = true;
                    noteUploadProgress();
                }
                #if defined(__linux__)
                    inputData.reset();
                #endif
            }
        }
        else
        {
            if(!storageOk)
            {
                INFOLOG("Storage not available!!");
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
            
        }
        return ret;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

bool s3Client::downloadFile(const char *url, std::string fileName)
{
    DEBUGLOG("downloadFile",url,fileName);
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        bool ret = false;
        if(m_storageAvailable && (m_impl.get() != nullptr))
        {
            Aws::S3::Model::GetObjectRequest request;
            request.SetBucket(m_bucket);
            request.SetKey(url);
            Aws::S3::Model::GetObjectOutcome outcome = m_impl->GetObject(request);

            if (!outcome.IsSuccess()) 
            {
                ERRORLOG("Download failed:",url,m_bucket,outcome.GetError().GetMessage().c_str(),outcome.GetError().GetResponseCode());
            }
            else 
            {
                auto& objectStream = outcome.GetResultWithOwnership().GetBody();

                std::ofstream fileStream(fileName.c_str(), std::ios::out | std::ios::binary);
                if (fileStream) 
                {
                    fileStream << objectStream.rdbuf();
                    fileStream.close();
                    INFOLOG("File downaloded and stored in file",fileName);
                    ret = true;
                } 
                else 
                {
                    ERRORLOG("Local file write failed",fileName);
                }
            }
        }
        else
        {
            if(!m_storageAvailable)
            {
                INFOLOG("Storage not available!!");
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
        }
        return ret;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

bool s3Client::isAvailable()
{
    DEBUGLOG("isAvailable");
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_storageAvailable;
}

void s3Client::stopThread()
{
    INFOLOG("stopThread");
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_threadPool.shutdown();
    if(uploadThread.joinable()) 
    {
        uploadThread.join();
    }
    if(spaceThread.joinable())
    {
        spaceThread.join();
    }
    m_keepAliveTimer.stop();
    m_uploadThreadAlive = false;
    INFOLOG("stopThread Done");
}

void s3Client::noteUploadProgress()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastUploadProgress = std::chrono::steady_clock::now();
    m_pendingUploadWatchdog = false;
}

void s3Client::ensureUploadDispatcherRunning()
{
    bool running = false;
    bool alive = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        running = m_running;
        alive = m_uploadThreadAlive.load();
    }
    if(!running || alive)
        return;

    // m_uploadThreadAlive is cleared only on thread exit, so join cannot block on a live loop.
    INFOLOG("Upload dispatcher not alive; attempting respawn");
    try
    {
        if(uploadThread.joinable())
            uploadThread.join();
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_running && !m_uploadThreadAlive.load())
        {
            m_uploadThreadAlive = true;
            uploadThread = std::thread(&s3Client::fileUploadThread, this);
            INFOLOG("Upload dispatcher respawned");
        }
    }
    catch(const std::exception& e)
    {
        m_uploadThreadAlive = false;
        ERRORLOG("Failed to respawn upload dispatcher:", e.what());
    }
}

bool s3Client::uploadListHasPendingFiles() const
{
    nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath("/"+ m_bucket + FILE_UPLOAD_JSON);
    std::ifstream inputFile(file.fullPath);
    if(!inputFile.is_open())
        return false;
    Json::Value root;
    Json::Reader reader;
    if(!reader.parse(inputFile, root) || !root.isArray())
        return false;
    for(const auto& jsonObject : root)
    {
        if(jsonObject["host"].isString() && (jsonObject["host"].asString() == m_url)  &&
            jsonObject["bucket"].isString() && (jsonObject["bucket"].asString() == m_bucket) &&
            jsonObject["files"].isArray() && !jsonObject["files"].empty())
        {
            return true;
        }
    }
    return false;
}

void s3Client::clearStaleWorkfilesIfStalled()
{
    const int queuedTasks = m_threadPool.getWorkingTaskCount();
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_workfiles.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    if(now - m_lastUploadProgress < std::chrono::seconds(UPLOAD_STALL_WATCHDOG_SECONDS))
        return;
    if(queuedTasks > 0)
        return;
    INFOLOG("Clearing stale upload workfiles after stall", m_workfiles.size());
    m_workfiles.clear();
}

void s3Client::requeueStagedUploads()
{
    INFOLOG("s3Client::requeueStagedUploads");
    try
    {
        // Files already in UploadList.json for this host/bucket are drained by fileUploadThread.
        // freeTempStorage no longer deletes the backlog, so a restart preserves queued .mkv.
        // Also recover orphans that exist on disk next to keys already listed for this client
        // (e.g. rename succeeded but JSON append failed mid-flight).
        nx_spl::aux::FileNameAndPath listFile =
            nx_spl::aux::localUniqueFilePath("/" + m_bucket + FILE_UPLOAD_JSON);
        if(!fs::exists(listFile.fullPath))
            return;

        std::ifstream inputFile(listFile.fullPath);
        if(!inputFile.is_open())
            return;

        Json::Value root;
        Json::Reader reader;
        if(!reader.parse(inputFile, root) || !root.isArray())
            return;
        inputFile.close();

        int pending = 0;
        for(const auto& jsonObject : root)
        {
            if(!(jsonObject["host"].isString() && jsonObject["host"].asString() == m_url
                && jsonObject["bucket"].isString() && jsonObject["bucket"].asString() == m_bucket
                && jsonObject["files"].isArray()))
            {
                continue;
            }
            for(const auto& entry : jsonObject["files"])
            {
                if(!entry.isString())
                    continue;
                const std::string key = entry.asString();
                nx_spl::aux::FileNameAndPath local = nx_spl::aux::localUniqueFilePath(key);
                if(fs::exists(local.fullPath))
                {
                    ++pending;
                    INFOLOG("Pending staged upload retained", key);
                }
                else
                {
                    INFOLOG("UploadList entry missing local file (will be dropped by dispatcher)", key);
                }
            }
        }
        INFOLOG("requeueStagedUploads pending count", pending);
        noteUploadProgress();
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:", e.what());
    }
}

void s3Client::fileUploadThread()
{
    DEBUGLOG("s3Client::fileUploadThread");
    // m_uploadThreadAlive is set true by the starter before this thread runs.
    INFOLOG("s3Client::fileUploadThread started");
    try
    {
        while (1) 
        {
            try
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if(m_running == false)
                        break;
                }

                clearStaleWorkfilesIfStalled();

                bool warnWatchdog = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if(m_pendingUploadWatchdog.load()
                        && (std::chrono::steady_clock::now() - m_lastQueuedToUploaded
                            >= std::chrono::seconds(UPLOAD_STALL_WATCHDOG_SECONDS)))
                    {
                        m_pendingUploadWatchdog = false;
                        warnWatchdog = true;
                    }
                }
                if(warnWatchdog)
                {
                    INFOLOG("Upload watchdog: file added to uploaded without progress for",
                            UPLOAD_STALL_WATCHDOG_SECONDS, "seconds",
                            "pending=", uploadListHasPendingFiles());
                }
                
                std::vector<std::string> fileToUpload = getNextFileToUpload();
                if(fileToUpload.empty())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(ONE_SECOND));
                    continue;
                }
                m_threadPool.setMaxThreads(ServerManager::getInstance()->getMaxThread());
                for(const std::string& filename : fileToUpload)
                {
                    std::string url = filename;
                    nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath(std::string(url));
                    
                    if(fs::exists(file.fullPath))
                    {
                        std::weak_ptr<s3Client> weakThis = shared_from_this();
                        m_threadPool.enqueueTask(
                        [weakThis,file,filename]() 
                        {
                            INFOLOG("added file in queue",file.name);
                            if (auto self = weakThis.lock()) {
                                try 
                                {
                                    self->noteUploadProgress();
                                    s3PtrType   uploadImpl; 
                                    Aws::S3::S3ClientConfiguration clientConfig;
                                    clientConfig.scheme = Aws::Http::Scheme::HTTPS;
                                    clientConfig.endpointOverride = Aws::String(self->m_url);
                                    clientConfig.userAgent = g_userAgent;

                                    Aws::Auth::AWSCredentials credentials;
                                    credentials.SetAWSAccessKeyId(self->m_accessKey);
                                    credentials.SetAWSSecretKey(self->m_secretKey);

                                    uploadImpl = createS3Client(credentials, clientConfig);

                                    if(uploadImpl.get() != nullptr)
                                    {
                                        std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
                                                                                                                file.fullPath.c_str(),
                                                                                                                std::ios_base::in | std::ios_base::binary);
                                        if ((inputData.get() != nullptr) && !inputData->good()) 
                                        {
                                            ERRORLOG("Unable to read local file:",file.fullPath);
                                        }
                                        else
                                        {
                                            uint64_t size = nx_spl::aux::getFileSize(file.fullPath.c_str());
                                            const bool isNxdb = nx_spl::aux::isGenerationalNxdb(filename)
                                                || (file.name.find(".nxdb") != std::string::npos);
                                            if(isNxdb && size < MIN_NXDB_BYTES)
                                            {
                                                INFOLOG("nxdb create deferred upload, size=", size, filename);
                                                self->removeFileFromUploadList(filename);
                                            }
                                            else
                                            {
                                                INFOLOG("Uploading file!!",file.name,size);
                                                Aws::S3::Model::PutObjectRequest request;
                                                request.SetBucket(self->m_bucket);
                                                request.SetKey(filename);
                                                request.SetBody(inputData);
                                                Aws::S3::Model::PutObjectOutcome outcome = uploadImpl->PutObject(request);
                                                #if defined(_WIN32)
                                                    static_cast<Aws::FStream*>(inputData.get())->close();
                                                #endif
                                                if (!outcome.IsSuccess()) 
                                                {
                                                    #if defined(__linux__)
                                                        inputData.reset();
                                                    #endif
                                                    ERRORLOG("Unable to upload file:",filename,outcome.GetError().GetMessage().c_str());
                                                }
                                                else 
                                                {
                                                    #if defined(__linux__)
                                                        inputData.reset();
                                                    #endif
                                                    INFOLOG("Successfully uploaded file:",filename);
                                                    self->noteUploadProgress();
                                                    {
                                                        std::lock_guard<std::mutex> lock(self->m_mutex);
                                                        self->m_space += size;
                                                    }
                                                    if(isNxdb)
                                                    {
                                                        // Keep local catalog durable; only remove prior cloud generation.
                                                        nx_spl::nxdb::onCatalogUploaded(self, filename);
                                                    }
                                                    else
                                                    {
                                                        INFOLOG("Delete File:", file.fullPath);
                                                        if (remove(file.fullPath.c_str()) != 0) 
                                                        {
                                                            ERRORLOG("Failed to remove file:",file.fullPath.c_str());
                                                            ClearMemoryManager::getInstance()->addFileToRemoveList(file.fullPath);
                                                        }
                                                    }
                                                    self->removeFileFromUploadList(filename);
                                                }
                                            }
                                        }
                                    }
                                    else
                                    {
                                        ERRORLOG("implPtrType is nullptr!");
                                    }
                                    {
                                        std::lock_guard<std::mutex> lock(self->m_mutex);
                                        DEBUGLOG("self->m_workfiles.size:", self->m_workfiles.size(), filename);
                                        self->m_workfiles.erase(std::remove(self->m_workfiles.begin(), self->m_workfiles.end(), filename), self->m_workfiles.end());
                                        DEBUGLOG("self->m_workfiles.size:", self->m_workfiles.size(), filename);
                                    }
                                    uploadImpl.reset();
                                }
                                catch(const std::exception& e)
                                {
                                    ERRORLOG("Exception Error:",e.what());
                                    std::lock_guard<std::mutex> lock(self->m_mutex);
                                    self->m_workfiles.erase(std::remove(self->m_workfiles.begin(), self->m_workfiles.end(), filename), self->m_workfiles.end());
                                }
                                catch(...)
                                {
                                    ERRORLOG("Exception Error: Unknown");
                                    std::lock_guard<std::mutex> lock(self->m_mutex);
                                    self->m_workfiles.erase(std::remove(self->m_workfiles.begin(), self->m_workfiles.end(), filename), self->m_workfiles.end());
                                }
                            }
                        }
                        );
                    }
                    else
                    {
                        ERRORLOG("File do not exist to uplaod!!",filename);
                        removeFileFromUploadList(filename);
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            DEBUGLOG("m_workfiles.size:", m_workfiles.size(), filename);
                            m_workfiles.erase(std::remove(m_workfiles.begin(), m_workfiles.end(), filename), m_workfiles.end());
                            DEBUGLOG("m_workfiles.size:", m_workfiles.size(), filename);
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(ONE_SECOND * 5)); 
            }
            catch(const std::exception& e)
            {
                ERRORLOG("fileUploadThread loop Error:",e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(ONE_SECOND));
            }
            catch(...)
            {
                ERRORLOG("fileUploadThread loop Error: Unknown");
                std::this_thread::sleep_for(std::chrono::milliseconds(ONE_SECOND));
            }
        }
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
    }
    catch(...)
    {
        ERRORLOG("Exception Error: Unknown");
    }
    m_uploadThreadAlive = false;
    INFOLOG("s3Client::fileUploadThread exited");
}

void s3Client::keepAliveActivator()
{
    DEBUGLOG("s3Client::keepAliveActivator");
    try
    {
        ensureUploadDispatcherRunning();

        bool storageOk = false;
        s3PtrType implCopy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            storageOk = (m_impl.get() != nullptr);
            implCopy = m_impl;
        }
        if(storageOk && implCopy.get() != nullptr)
        {
            Aws::S3::Model::PutObjectRequest request;
            request.SetBucket(m_bucket);
            request.SetKey(SYNC_FILE);
            auto input_data = Aws::MakeShared<Aws::StringStream>("StringStream");
            *input_data << "Test";
            request.SetBody(input_data);
            Aws::S3::Model::PutObjectOutcome outcome = implCopy->PutObject(request);
            if (!outcome.IsSuccess()) 
            {
                ERRORLOG("Unable to upload file:",m_bucket,SYNC_FILE,outcome.GetError().GetMessage().c_str());
                INFOLOG("Connection Failed!!", m_bucket);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_storageAvailable = false;
                }
                INFOLOG("Re-Establishing connection!!");
                Aws::S3::S3ClientConfiguration clientConfig;
                clientConfig.scheme = Aws::Http::Scheme::HTTPS;
                clientConfig.endpointOverride = Aws::String(m_url);

                Aws::Auth::AWSCredentials credentials;
                credentials.SetAWSAccessKeyId(m_accessKey);
                credentials.SetAWSSecretKey(m_secretKey);

                auto newImpl = createS3Client(credentials, clientConfig);
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_impl = newImpl;
                }
            }
            else 
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_storageAvailable = true;
            }
        }
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
    }
}

bool s3Client::isFileInUploadList(std::string fileName) const
{
    DEBUGLOG("s3Client::isFileInUploadList",fileName);
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool ret = false;
        nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath("/"+ m_bucket + FILE_UPLOAD_JSON);
        std::ifstream inputFile(file.fullPath);
        if (inputFile.is_open())
        {
            Json::Value root;
            Json::Reader reader;
            if (reader.parse(inputFile, root)) 
            {
                inputFile.close();
                if(root.isArray())
                {
                    for (auto& jsonObject : root) 
                    {
                        if(jsonObject["host"].isString() && (jsonObject["host"].asString() == m_url)  && 
                            jsonObject["bucket"].isString() && (jsonObject["bucket"].asString() == m_bucket) 
                            && jsonObject["files"].isArray())
                        {
                            Json::Value& filesArray = jsonObject["files"];
                            for(auto& jsonValue: filesArray)
                            {
                                if(jsonValue.isString() && (jsonValue.asString() == fileName))
                                {
                                    ret = true;
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
            }
            else
            {
                ERRORLOG("Error parsing JSON from file:",reader.getFormattedErrorMessages());
            }
            inputFile.close();
        }
        else
        {
            ERRORLOG("Error opening JSON file:",file.fullPath);
        }
        return ret;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

bool s3Client::isTotalSpaceUpdating()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalSpaceUpdating;
}

bool s3Client::createBucket()
{
    DEBUGLOG("createBucket");
    try
    {
        bool ret = false;
        if(m_impl.get() != nullptr)
        {
            Aws::S3::Model::CreateBucketRequest request;
            request.SetBucket(m_bucket);
            
            Aws::S3::Model::CreateBucketOutcome outcome = m_impl->CreateBucket(request);
            if (!outcome.IsSuccess()) 
            {
                ERRORLOG("Failed to create bucket",m_bucket,outcome.GetError().GetMessage());
            }
            else 
            {
                INFOLOG("bucket created",m_bucket);
                ret = true;
            }
        }
        else
        {
            ERRORLOG("implPtrType is nullptr");
        }
        return ret;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return false;
    }
}

void s3Client::updateRemoteFolderSize()
{
    INFOLOG("Calculating Free space");
    try
    {
        if(m_spaceImpl.get() == nullptr)
        {
            ERRORLOG("m_spaceImpl is nullptr!");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_totalSpaceUpdating = true;
            m_space = 0;
        }

        std::string url("/");
        uint64_t totalSize = 0;
        Aws::S3::Model::ListObjectsRequest request;
        request.SetBucket(m_bucket);
        request.WithPrefix(m_bucket + url);
        
        auto outcome = m_spaceImpl->ListObjects(request);
        if (outcome.IsSuccess()) 
        {
            for (const auto& object : outcome.GetResult().GetContents())
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if(m_running == false)
                        return;
                }

                Aws::S3::Model::HeadObjectRequest headObjectRequest;
                headObjectRequest.WithBucket(m_bucket).WithKey(object.GetKey());

                Aws::S3::Model::HeadObjectOutcome headObjectOutcome = m_spaceImpl->HeadObject(headObjectRequest);

                if (headObjectOutcome.IsSuccess())
                {
                    totalSize += headObjectOutcome.GetResult().GetContentLength();
                }
                else
                {
                    ERRORLOG("Failed to get metadata",object.GetKey(),headObjectOutcome.GetError().GetResponseCode());
                    if(headObjectOutcome.GetError().GetResponseCode() == Aws::Http::HttpResponseCode::SERVICE_UNAVAILABLE)
                    {
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_reUpdateSpace = true;
                        }
                        break;
                    }
                }
            }
        }
        else 
        {
            ERRORLOG("Remote dir not exists",m_bucket,outcome.GetError().GetMessage().c_str());
            std::lock_guard<std::mutex> lock(m_mutex);
            m_reUpdateSpace = true;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_space = totalSize;
            m_totalSpaceUpdating = false;
        }

        INFOLOG("Calculating Free space Done..",m_space);
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
    }
}

std::vector<std::string> s3Client::getNextFileToUpload()
{
    DEBUGLOG("s3Client::getNextFileToUpload");
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(!m_storageAvailable) {
            INFOLOG("Connection Lost storage not awailable!!");
            return std::vector<std::string>{};
        }
        if(m_totalSpaceUpdating 
            || m_workfiles.size() >= ServerManager::getInstance()->getMaxThread())
        {
            DEBUGLOG("Uploading is bussy:", m_workfiles.size(), m_totalSpaceUpdating);
            return std::vector<std::string>{};
        }
        std::vector<std::string> fileName;
        nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath("/"+ m_bucket + FILE_UPLOAD_JSON);
        std::ifstream inputFile(file.fullPath);
        if (inputFile.is_open())
        {
            Json::Value root;
            Json::Reader reader;
            if (reader.parse(inputFile, root)) 
            {
                if(root.isArray())
                {
                    for (auto& jsonObject : root) 
                    {
                        if(jsonObject["host"].isString() && (jsonObject["host"].asString() == m_url)  && 
                            jsonObject["bucket"].isString() && (jsonObject["bucket"].asString() == m_bucket) 
                            && jsonObject["files"].isArray())
                        {
                            Json::Value& filesArray = jsonObject["files"];
                            int max_upload_thread = ServerManager::getInstance()->getMaxThread();
                            int current_task_count = m_threadPool.getWorkingTaskCount();
                            
                            if(filesArray.size() >= 600)
                            {
                                INFOLOG("level 3 queue transfer warrning");
                                m_minute_count = 12;
                                m_notification_sent = false;
                            }
                            else if(filesArray.size() >= 400)
                            {
                                if(m_minute_count >= 12)
                                {
                                    INFOLOG("level 2 queue transfer warrning");
                                    m_minute_count = 0;
                                }
                                m_minute_count++;
                            }
                            else if (filesArray.size() >= 200)
                            {
                                if(m_notification_sent == false)
                                    INFOLOG("level 1 queue transfer warrning");
                                m_notification_sent = true;
                            }
                            else
                            {
                                m_notification_sent = false;
                            }
                            for(Json::ArrayIndex i = 0; i < filesArray.size(); i++)
                            {
                                if(m_workfiles.size() >= ServerManager::getInstance()->getMaxThread())
                                    break;

                                if (filesArray[i].isString()) 
                                {
                                    std::string file = filesArray[i].asString();
                                    if(std::find(m_workfiles.begin(),m_workfiles.end(),file) == m_workfiles.end()) {
                                        fileName.push_back(file);
                                        m_workfiles.push_back(file);
                                        DEBUGLOG("file:",file);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }
            else
            {
                ERRORLOG("Error parsing JSON from file:",reader.getFormattedErrorMessages());
            }
            inputFile.close();
        }
        else
        {
            DEBUGLOG("Error opening JSON file:",file.fullPath);
        }
        return fileName;
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
        return std::vector<std::string>{};
    }
}

void s3Client::removeFileFromUploadList(std::string fileName)
{
    INFOLOG("s3Client::removeFileFromUploadList",fileName);
    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath("/"+ m_bucket + FILE_UPLOAD_JSON);
        std::ifstream inputFile(file.fullPath);
        if (inputFile.is_open())
        {
            Json::Value root;
            Json::Reader reader;
            if (reader.parse(inputFile, root)) 
            {
                if(root.isArray())
                {
                    for (auto& jsonObject : root) 
                    {
                        if(jsonObject["host"].isString() && (jsonObject["host"].asString() == m_url)  && 
                            jsonObject["bucket"].isString() && (jsonObject["bucket"].asString() == m_bucket) && 
                            jsonObject["files"].isArray())
                        {
                            Json::Value& filesArray = jsonObject["files"];
                            for(Json::ArrayIndex i = 0; i < filesArray.size(); i++)
                            {
                                if(filesArray[i].isString() && (filesArray[i].asString() == fileName))
                                {
                                    filesArray.removeIndex(i, &filesArray[i]);
                                    std::ofstream outputFile(file.fullPath);
                                    if (!outputFile.is_open()) {
                                        ERRORLOG("Error opening JSON file:",file.fullPath);
                                    }
                                    else
                                    {
                                        Json::StyledStreamWriter writer;
                                        writer.write(outputFile, root);
                                        outputFile.close();
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }
            else
            {
                ERRORLOG("Error parsing JSON from file:",reader.getFormattedErrorMessages());
            }
            inputFile.close();
        }
        else
        {
            ERRORLOG("Error opening JSON file:",file.fullPath);
        }
    }
    catch(const std::exception& e)
    {
        ERRORLOG("Exception Error:",e.what());
    }
}
