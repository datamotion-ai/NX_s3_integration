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

#include <curl/curl.h>

#include "s3Client.h"
#include "storage/third_party_storage.h"
#include "timer.h"

namespace nx_spl
{

    typedef std::shared_ptr<s3Client> implPtrType;

    class S3IODevice
        : public IODevice,
          private aux::NonCopyable,
          private aux::PluginRefCounter<S3IODevice>
    {
        friend class aux::PluginRefCounter<S3IODevice>;
    public:
            S3IODevice(
            const char *uri, 
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
        implPtrType             m_impl;
        aux::FileNameAndPath    m_localfile;
        bool                    m_altered;
        long long               m_localsize;
        mutable std::mutex      m_mutex;
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
        mutable std::mutex  m_mutex;
        mutable bool        m_available;
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

    public: // plugin interface implementation
        virtual void* queryInterface(const nxpl::NX_GUID& interfaceID) override;

        virtual int addRef() const override;

        virtual int releaseRef() const override;

    private:
        ~S3StorageFactory();
        void clearMemory();
    private:
        Aws::SDKOptions m_options;
        static std::mutex  m_mutex;
        Timer m_clearMemoryTimer;
    }; // class S3StorageFactory

}

#endif //S3_LIBRARY_H