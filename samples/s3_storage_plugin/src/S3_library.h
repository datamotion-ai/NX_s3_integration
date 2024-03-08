#ifndef S3_LIBRARY_H
#define S3_LIBRARY_H

#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <stdint.h>
#include <mutex>
#include <fstream>
#include <ctime>
#include <sstream>
#include <iostream>

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

#include <curl/curl.h>

#include "storage/third_party_storage.h"
#include "timer.h"

namespace nx_spl
{
    namespace aux
    { 
        // Generic reference counter Mix-In. Private inherit it.
        template <typename P>
        class PluginRefCounter
        {
        public:
            PluginRefCounter()
                : m_count(1)
            {}

            int p_addRef() const { return ++m_count; }

            int p_releaseRef() const
            {
                int new_count = --m_count;
                if (new_count <= 0) {
                    delete static_cast<const P*>(this);
                }
                return new_count;
            }
        private:
            mutable std::atomic<int> m_count;
        }; // class PluginRefCounter

        class NonCopyable
        {
        public:
            NonCopyable() {}
            NonCopyable(const NonCopyable&);
            NonCopyable& operator =(const NonCopyable&);

            NonCopyable(NonCopyable&&);
            NonCopyable& operator =(NonCopyable&&);
        }; // class NonCopyable

        /**
         * A file name and a full path, containing this name. When we communicate with the remote
         * FTP server, * we operate bare file names. But locally we need to map this file to a full
         * path. That's why it's convenient to have both.
         */
        struct FileNameAndPath
        {
            std::string name;
            std::string fullPath;
        }; // struct FileNameAndPath
        
    } //namespace aux

    typedef std::shared_ptr<Aws::S3::S3Client> implPtrType;

    class S3IODevice
        : public IODevice,
          private aux::NonCopyable,
          private aux::PluginRefCounter<S3IODevice>
    {
        friend class aux::PluginRefCounter<S3IODevice>;
    public:
            S3IODevice(
            const char *uri,
            const char *bucket, 
            int mode, 
            const implPtrType &impl
        );

        virtual uint32_t STORAGE_METHOD_CALL write(
            const void*     src,
            const uint32_t  size,
            int*            ecode
        ) override;

        virtual uint32_t STORAGE_METHOD_CALL read(
            void*           dst,
            const uint32_t  size,
            int*            ecode
        ) const override;

        virtual int STORAGE_METHOD_CALL seek(
            uint64_t    pos,
            int*        ecode
        ) override;

        virtual int      STORAGE_METHOD_CALL getMode() const override;
        virtual uint32_t STORAGE_METHOD_CALL size(int* ecode) const override;

    public: // plugin interface implementation
        virtual void* queryInterface(const nxpl::NX_GUID& interfaceID) override;

        virtual int addRef() const override;
        virtual int releaseRef() const override;

    private:
        // synchronize localfile with remote one
        void flush();
        // delete only via releaseRef()
        ~S3IODevice();

    private:
        int                     m_mode;
        int                     m_fileWriteCount;
        mutable int64_t         m_pos;
        std::string             m_uri; //file URI
        std::string             m_bucket;
        implPtrType             m_impl;
        aux::FileNameAndPath    m_localfile;
        bool                    m_altered;
        long long               m_localsize;
        mutable std::mutex      m_mutex;
        std::string             m_implurl;
        std::string             m_user;
        std::string             m_passwd;
        mutable FILE*           m_file;
    }; // class S3IODevice

    // Fileinfo list is obtained from the server at construction phase.
    // After this phase there are no real interactions with FTP server.
    class S3FileInfoIterator
        : public FileInfoIterator,
          private aux::NonCopyable,
          private aux::PluginRefCounter<S3FileInfoIterator>
    {
        friend class aux::PluginRefCounter<S3FileInfoIterator>;

        typedef std::vector<std::string>        FileListType;
        typedef FileListType::const_iterator    FileListIteratorType;

    public:
        S3FileInfoIterator(
            FileListType      &&fileList, 
            const std::string   &baseDir
        );

        virtual FileInfo* STORAGE_METHOD_CALL next(int* ecode) const override;

    public: // plugin interface implementation
        virtual void* queryInterface(const nxpl::NX_GUID& interfaceID) override;

        virtual int addRef() const override;
        virtual int releaseRef() const override;

    private:
        // delete only with releaseRef()
        ~S3FileInfoIterator();

    private:
        mutable std::vector<char>   m_urlData;
        mutable FileInfo            m_fileInfo;
        FileListType                m_fileList;
        mutable FileListIteratorType        m_curFile;
        int                         m_basedirsize;
    }; // class S3FileListIterator

    class S3Storage
        : public Storage,
          private aux::NonCopyable,
          private aux::PluginRefCounter<S3Storage>
    {
        friend class aux::PluginRefCounter<S3Storage>;
    public: 
        S3Storage(const std::string& url);
        int getAvail() const {return m_available;}

    public: // Storage interface implementation
        virtual int STORAGE_METHOD_CALL isAvailable() const override;

        virtual IODevice* STORAGE_METHOD_CALL open(
            const char*     uri,
            int             flags,
            int*            ecode
        ) const override;

        virtual uint64_t STORAGE_METHOD_CALL getFreeSpace(int* ecode) const override;
        virtual uint64_t STORAGE_METHOD_CALL getTotalSpace(int* ecode) const override;
        virtual int STORAGE_METHOD_CALL getCapabilities() const override;

        virtual void STORAGE_METHOD_CALL removeFile(
            const char* url,
            int*        ecode
        ) override;

        virtual void STORAGE_METHOD_CALL removeDir(
            const char* url,
            int*        ecode
        ) override;

        virtual void STORAGE_METHOD_CALL renameFile(
            const char*     oldUrl,
            const char*     newUrl,
            int*            ecode
        ) override;

        virtual FileInfoIterator* STORAGE_METHOD_CALL getFileIterator(
            const char*     dirUrl,
            int*            ecode
        ) const override;

        virtual int STORAGE_METHOD_CALL fileExists(
            const char*     url,
            int*            ecode
        ) const override;

        virtual int STORAGE_METHOD_CALL dirExists(
            const char*     url,
            int*            ecode
        ) const override;

        virtual uint64_t STORAGE_METHOD_CALL fileSize(
            const char*     url,
            int*            ecode
        ) const override;

    public: // plugin interface implementation
        virtual void* queryInterface(const nxpl::NX_GUID& interfaceID) override;

        virtual int addRef() const override;
        virtual int releaseRef() const override;

    private:
        // destroy only via releaseRef()
        ~S3Storage();

    private:
        mutable implPtrType m_impl;
        mutable uint64_t    m_freebucketSize;
        uint64_t            m_totalSpace;
        std::string         m_url;
        std::string         m_accessKey;
        std::string         m_secretKey;
        std::string         m_bucket;
        mutable std::mutex  m_mutex;
        mutable int         m_available;
        mutable std::map<std::string, IODevice*> m_IODeviceMap;
    }; // class S3storage


    class S3StorageFactory
        : public StorageFactory,
          private aux::NonCopyable,
          private aux::PluginRefCounter<S3StorageFactory>
    {
        friend class aux::PluginRefCounter<S3StorageFactory>;
    public:
        S3StorageFactory();

        virtual const char** STORAGE_METHOD_CALL findAvailable() const override;

        virtual Storage* STORAGE_METHOD_CALL createStorage(const char* url,int* ecode) override;

        virtual const char* STORAGE_METHOD_CALL storageType() const override;

        virtual const char* lastErrorMessage(int ecode) const override;

        static bool isLicenseAvailable();

    public: // plugin interface implementation
        virtual void* queryInterface(const nxpl::NX_GUID& interfaceID) override;

        virtual int addRef() const override;

        virtual int releaseRef() const override;

    private:
        ~S3StorageFactory();
        void verifyLicenses() ;
        void clearMemory();
        bool createSession(const std::string &host, const std::string &usr, const std::string &pswd, std::string& token) const;
        bool isSessionExpired(const std::string &host,std::string& token) const;
    private:
        Aws::SDKOptions m_options;
        static std::mutex  m_mutex;
        Timer m_timer;
        Timer m_clearMemoryTimer;
    }; // class S3StorageFactory

}

#endif //S3_LIBRARY_H