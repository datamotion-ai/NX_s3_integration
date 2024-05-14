#include <json/json.h>
#include "s3Client.h"
#include "common.hpp"
#include "ServerManager.h"
#include "ClearMemoryManager.h"

#define SYNC_FILE "Test.txt"

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
m_uploadImpl(nullptr),
m_spaceImpl(nullptr),
m_space(S3_DEFAULT_TOTAL_SPACE)
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
    if(m_uploadImpl.get() != nullptr)
    {
        m_uploadImpl.reset();
    }
    if(m_spaceImpl.get() != nullptr)
    {
        m_spaceImpl.reset();
    }
}

bool s3Client::establishS3Connection()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    INFOLOG("establishS3Connection");
    Aws::Client::ClientConfiguration clientConfig;
    clientConfig.scheme = Aws::Http::Scheme::HTTPS;
    clientConfig.endpointOverride = Aws::String(m_url);

    Aws::Auth::AWSCredentials credentials;
    credentials.SetAWSAccessKeyId(m_accessKey);
    credentials.SetAWSSecretKey(m_secretKey);
    
    #if defined (_WIN32)
        m_impl.reset(new Aws::S3::S3Client(credentials, Aws::MakeShared<Aws::S3::S3EndpointProvider>(Aws::S3::S3Client::ALLOCATION_TAG), clientConfig));
    #else
        m_impl.reset(new Aws::S3::S3Client(credentials, nullptr, clientConfig));
    #endif

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
                bucketFound = true;
                m_storageAvailable = true;
            }
        }
        else
        {
            ERRORLOG("Failed to list bucket lists!! connection failed!!");
            ServerManager::getInstance()->postEvent(outcome.GetError().GetMessage(),m_url + "/" + m_bucket);
        }

        if(!ServerManager::getInstance()->isServerIntialize())
        {
            bucketFound = true;
        }
        
        if(bucketFound == true)
        {
            INFOLOG("SuccessFully establish s3 connection with host: ");
            return true;
        }
    }
    else
    {
        ERRORLOG("implPtrType is nullptr");
    }

    return false;
}

bool s3Client::initializeConnection()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    INFOLOG("initializeConnection");
    Aws::Client::ClientConfiguration clientConfig;
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
    
    #if defined (_WIN32)
        m_impl.reset(new Aws::S3::S3Client(credentials, Aws::MakeShared<Aws::S3::S3EndpointProvider>(Aws::S3::S3Client::ALLOCATION_TAG), clientConfig));
        m_uploadImpl.reset(new Aws::S3::S3Client(credentials, Aws::MakeShared<Aws::S3::S3EndpointProvider>(Aws::S3::S3Client::ALLOCATION_TAG), clientConfig));
        m_spaceImpl.reset(new Aws::S3::S3Client(credentials, Aws::MakeShared<Aws::S3::S3EndpointProvider>(Aws::S3::S3Client::ALLOCATION_TAG), clientConfig));
    #else
        m_impl.reset(new Aws::S3::S3Client(credentials, nullptr, clientConfig));
        m_uploadImpl.reset(new Aws::S3::S3Client(credentials, nullptr, clientConfig));
        m_spaceImpl.reset(new Aws::S3::S3Client(credentials, nullptr, clientConfig));
    #endif

    if((m_impl.get() != nullptr) && (m_uploadImpl.get() != nullptr) && (m_spaceImpl.get() != nullptr))
    {
        m_running = true;
        m_reUpdateSpace = false;
        uploadThread = std::thread(&s3Client::fileUploadThread, this);
        spaceThread = std::thread(&s3Client::updateRemoteFolderSize, this);
        m_keepAliveTimer.start(this,&s3Client::keepAliveActivator,ONE_MINUTE);
        INFOLOG("SuccessFully initialise s3 connection with host: ");
        return true;
    }
    else
    {
        ERRORLOG("implPtrType is nullptr");
    }

    return false;
}

bool s3Client::remoteUriExists(const std::string& uri) 
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG(uri,m_bucket);
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

bool s3Client::remoteDirExists(const std::string &uri)
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

uint64_t s3Client::remoteFolderSize(bool update)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("s3Client::remoteFolderSize");
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

uint64_t s3Client::getRemoteFileSize(const std::string& uri)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG(uri,m_bucket);
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

std::vector<std::string> s3Client::getobjectKeys(const char *dirUrl)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("getobjectKeys",dirUrl);
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
            line.append(folderName.c_str());
            line.append(",");
            line.append(std::to_string(nx_spl::isDir));
            line.append(",");
            line.append("0");
            DEBUGLOG(line);
            objectList.push_back(line);
            line.clear();
        }
        
        auto fileObjects = outcome.GetResult().GetContents();
        for (const auto& object : fileObjects) 
        {
            std::string line;
            std::string fileName;
            fileName.assign(object.GetKey().begin()+dir.size(),object.GetKey().end());
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
            DEBUGLOG(line);
            objectList.push_back(line);
            line.clear();
        }
    }
    return std::move(objectList);
}

bool s3Client::renameFile(const char *oldUrl, const char *newUrl)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("renameFile",oldUrl,newUrl);
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

bool s3Client::removeUrl(const char *url)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("removeUrl",url);
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

bool s3Client::addFileToUploadInQueue(const char *url)
{
    DEBUGLOG("s3Client::addFileToUploadInQueue",url);
    std::lock_guard<std::mutex> lock(m_mutex);
    nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath(m_bucket + FILE_UPLOAD_JSON);
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
                    if((jsonObject["host"].asString() == m_url)  && 
                        (jsonObject["bucket"].asString() == m_bucket))
                    {
                        Json::Value& filesArray = jsonObject["files"];
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
    return true;
}

bool s3Client::uploadFile(const char *url, std::string fileName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("uploadFile",url,fileName);
    bool ret = false;
    if(m_storageAvailable && (m_impl.get() != nullptr))
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
            Aws::S3::Model::PutObjectOutcome outcome = m_impl->PutObject(request);
            static_cast<Aws::FStream*>(inputData.get())->close();
            if (!outcome.IsSuccess()) 
            {
                ERRORLOG("Unable to upload file:",url,outcome.GetError().GetMessage().c_str());
            }
            else 
            {
                INFOLOG("Successfully uploaded file:",fileName,url,m_bucket);
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

bool s3Client::downloadFile(const char *url, std::string fileName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("downloadFile",url,fileName);
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

bool s3Client::isAvailable()
{
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
    if(uploadThread.joinable()) 
    {
        uploadThread.join();
    }
    if(spaceThread.joinable())
    {
        spaceThread.join();
    }
    
    m_keepAliveTimer.stop();
    INFOLOG("stopThread Done");
}

bool s3Client::isFileInUploadList(std::string fileName) const
{
    DEBUGLOG("s3Client::isFileInUploadList",fileName);
    std::lock_guard<std::mutex> lock(m_mutex);
    bool ret = false;
    nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath(m_bucket + FILE_UPLOAD_JSON);
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
                    if((jsonObject["host"].asString() == m_url)  && 
                        (jsonObject["bucket"].asString() == m_bucket) && jsonObject["files"].isArray())
                    {
                        Json::Value& filesArray = jsonObject["files"];
                        for(auto& jsonValue: filesArray)
                        {
                            if(jsonValue.asString() == fileName)
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

bool s3Client::isTotalSpaceUpdating()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalSpaceUpdating;
}

bool s3Client::createBucket()
{
    DEBUGLOG("createBucket");
    bool ret = false;
    if(m_impl.get() != nullptr)
    {
        Aws::S3::Model::CreateBucketRequest request;
        request.SetBucket(m_bucket);
        
        Aws::S3::Model::CreateBucketOutcome outcome = m_impl->CreateBucket(request);
        if (!outcome.IsSuccess()) 
        {
            ERRORLOG("Failed to create bucket",m_bucket,outcome.GetError().GetMessage());
            ServerManager::getInstance()->postEvent(outcome.GetError().GetMessage(),m_url + "/" + m_bucket);
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

void s3Client::fileUploadThread()
{
    DEBUGLOG("s3Client::fileUploadThread");
    while (1) 
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(m_running == false)
               return;
        }
        
        std::string fileToUpload = getNextFileToUpload();
        if(!isAvailable() || fileToUpload.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(ONE_SECOND));
            continue;
        }
        std::string url = fileToUpload;
        size_t last_underscore_pos = url.find_last_of('_');
        if (last_underscore_pos != std::string::npos) 
        {
            url = url.substr(0, last_underscore_pos);
            url.append(".mkv");
        }
        nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath(std::string(url));
        bool fileUploaded = false;
        if(fs::exists(file.fullPath))
        {
            if(m_uploadImpl.get() != nullptr)
            {
                std::shared_ptr<Aws::IOStream> inputData = Aws::MakeShared<Aws::FStream>("SampleAllocationTag",
                                                                                        file.fullPath.c_str(),
                                                                                        std::ios_base::in | std::ios_base::binary);
                if (!*inputData) 
                {
                    ERRORLOG("Unable to read local file:",file.fullPath);
                }
                else
                {
                    INFOLOG("Uploading file!!",file.fullPath);
                    Aws::S3::Model::PutObjectRequest request;
                    request.SetBucket(m_bucket);
                    request.SetKey(fileToUpload);
                    request.SetBody(inputData);
                    Aws::S3::Model::PutObjectOutcome outcome = m_uploadImpl->PutObject(request);
                    static_cast<Aws::FStream*>(inputData.get())->close();
                    if (!outcome.IsSuccess()) 
                    {
                        ERRORLOG("Unable to upload file:",fileToUpload,outcome.GetError().GetMessage().c_str());
                    }
                    else 
                    {
                        INFOLOG("Successfully uploaded file:",file.fullPath,fileToUpload,m_bucket);
                        if (remove(file.fullPath.c_str()) != 0) 
                        {
                            ERRORLOG("Failed to remove file:",file.fullPath.c_str());
                            ClearMemoryManager::getInstance()->addFileToRemoveList(file.fullPath);
                        }
                        fileUploaded = true;
                    }
                }
            }
            else
            {
                ERRORLOG("implPtrType is nullptr!");
            }
        }
        else
        {
           ERRORLOG("File do not exist to uplaod!!",fileToUpload);
           fileUploaded = true;
        }
        if(fileUploaded)
        {
            removeFileFromUploadList(fileToUpload);
        }
    }
}

void s3Client::keepAliveActivator()
{
    DEBUGLOG("s3Client::keepAliveActivator");
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_impl.get() != nullptr)
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
            ERRORLOG("Unable to upload file:",SYNC_FILE,outcome.GetError().GetMessage().c_str());
            ServerManager::getInstance()->postEvent(outcome.GetError().GetMessage() + "\n Local Storage Enabled!!",m_url + "/" + m_bucket);
            m_storageAvailable = false;
        }
        else 
        {
            if(m_storageAvailable == false)
            {
                m_storageAvailable = true;
            }
        }
    }
}

void s3Client::updateRemoteFolderSize()
{
    INFOLOG("Calculating Free space");

    if(m_spaceImpl.get() == nullptr)
    {
        ERRORLOG("m_spaceImpl is nullptr!");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_totalSpaceUpdating = true;
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
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_space = totalSize;
        m_totalSpaceUpdating = false;
    }

    INFOLOG("Calculating Free space Done..");
}

std::string s3Client::getNextFileToUpload()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("s3Client::getNextFileToUpload");
    std::string fileName;
    nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath(m_bucket + FILE_UPLOAD_JSON);
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
                    if((jsonObject["host"].asString() == m_url)  && 
                        (jsonObject["bucket"].asString() == m_bucket) && jsonObject["files"].isArray())
                    {
                        Json::Value& filesArray = jsonObject["files"];
                        if(filesArray.empty() == false)
                        {
                            fileName = filesArray[0].asString();
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
    return fileName;
}

void s3Client::removeFileFromUploadList(std::string fileName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    INFOLOG("s3Client::removeFileFromUploadList",fileName);
    nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath(m_bucket + FILE_UPLOAD_JSON);
    std::ifstream inputFile(file.fullPath);
    if (inputFile.is_open())
    {
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(inputFile, root)) 
        {
            inputFile.close();
            INFOLOG("==============================>1");
            if(root.isArray())
            {
                for (auto& jsonObject : root) 
                {
                    if((jsonObject["host"].asString() == m_url)  && 
                        (jsonObject["bucket"].asString() == m_bucket) && 
                        jsonObject["files"].isArray())
                    {
                        Json::Value& filesArray = jsonObject["files"];
                        INFOLOG("==============================>2");
                        if(filesArray[0].asString() == fileName)
                        {
                            INFOLOG("==============================>3");
                            filesArray.removeIndex(0, &filesArray[0]);
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
                        break;
                    }
                }
            }
        }
        else
        {
            ERRORLOG("Error parsing JSON from file:",reader.getFormattedErrorMessages());
        }
        INFOLOG("==============================>4");
        inputFile.close();
        INFOLOG("==============================>5");
    }
    else
    {
        ERRORLOG("Error opening JSON file:",file.fullPath);
    }
}
