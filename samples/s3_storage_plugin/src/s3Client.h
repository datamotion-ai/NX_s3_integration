#ifndef S3_CLIENT_H
#define S3_CLIENT_H

#if defined(__linux__) || defined(__APPLE__)
#   include <sys/stat.h>
#   include <sys/utsname.h>
#elif defined (_WIN32)
#   include <Windows.h>
#   include <winternl.h>
#endif

#include <mutex>
#include <vector>
#include <condition_variable>

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/CreateBucketRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/CopyObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>

#include "daily_loger.hpp"
#include "common.hpp"
#include "timer.h"

typedef std::shared_ptr<Aws::S3::S3Client> s3PtrType;

class s3Client
{
    public:
    s3Client(const std::string  &url, const std::string  &uaccessKey, const std::string  &usecreatKey, const std::string  &bucket);
    ~s3Client();

    bool establishS3Connection();
    bool remoteUriExists(const std::string& uri);
    bool remoteDirExists(const std::string& uri);
    uint64_t remoteFolderSize(const std::string& uri);
    uint64_t getRemoteFileSize(const std::string& uri);
    std::vector<std::string> getobjectKeys(const char *dirUrl);
    bool renameFile(const char *oldUrl, const char *newUrl);
    bool removeUrl(const char *url);
    bool addFileToUploadInQueue(const char *url);
    bool uploadFile(const char *url,std::string fileName);
    bool downloadFile(const char *url,std::string fileName);
    bool isAvailable();
    void stopThread();
    bool isFileInUploadList(std::string file)const;

    private:
    bool createBucket();
    void fileUploadThread();
    void keepAliveActivator();
    std::string getNextFileToUpload();
    void removeFileFromUploadList(std::string file);

    private:
    bool        m_running;
    bool        m_storageAvailable;
    s3PtrType   m_impl;
    s3PtrType   m_uploadImpl;
    mutable std::mutex  m_mutex;
    mutable std::mutex  m_waitmutex;
    bool        m_isMutexUnlocked;
    std::condition_variable condition;
    std::string m_url;
    std::string m_accessKey;
    std::string m_secretKey;
    std::string m_bucket;
    uint64_t    m_space;
    std::vector<std::string> m_fileToUpload;
    std::thread uploadThread;
    Timer       m_keepAliveTimer;
};

#endif //S3_CLIENT_H