#ifndef COMMON_H
#define COMMON_H

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
#include <json/json.h>
#include <vector>
#include <memory>
#include <stdint.h>
#include <mutex>

#if defined(__linux__) || defined(__APPLE__)
#   include <sys/stat.h>
#   include <sys/utsname.h>
#elif defined (_WIN32)
#   include <Windows.h>
#   include <winternl.h>
#endif

#if __has_include(<filesystem>)
  #include <filesystem>
  namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
  #include <experimental/filesystem> 
  namespace fs = std::experimental::filesystem;
#else
  error "Missing the <filesystem> header."
#endif

#include "storage/third_party_storage.h"
#include "daily_loger.hpp"


#define VERSION "beta-NX_Enterprise-Arm_Global-1.1.6"

#ifdef _MSC_VER
#   define NOEXCEPT
#elif defined __GNUC__
#   define NOEXCEPT noexcept
#endif

#define S3_CONFIG_FILE "s3.config"

#define DEFAULT_200_MB  200 * 1024 * 1024 //200 MB
#define DEFAULT_1_GB  1024 * 1024 * 1024 //1 GB
#define S3_DEFAULT_TOTAL_SPACE 1024LL * DEFAULT_1_GB //100 GB

#define LICENSE_CONFIG_FILE "license.config"
#define FILE_UPLOAD_JSON "UploadList.json"
#define ONE_SECOND 1000
#define ONE_MINUTE 60 * 1000
#define FIVE_MINUTE 5 * ONE_MINUTE
#define TEN_MINUTE 10 * ONE_MINUTE
#define MAX_FILE_WRITE_COUNT 1

extern bool g_bucketSizeNeedUpdate;
extern std::vector<std::string> g_removeFileList;
extern std::string g_userAgent;
extern std::string g_VMS;

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

        struct Url
        {
            std::string uaccessKey;
            std::string usecreatKey;
            std::string host;
            std::string path;
            std::string port;

            static Url fromString(const std::string& s)
            {
                enum
                {   // parse states
                    scheme,
                    accessKey,
                    secreatKey,
                    host,
                    port
                } 
                
                ps = scheme;

                const int schemeSize = 5; // "S3://" size
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
                        if (s.substr(0, schemeSize) != "s3://")
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
                            ps = port;
                        }
                        break;
                    case port:
                        c = s[cur];
                        if (c == '/') //path begins
                        {
                            u.port.assign(s.begin() + start, s.begin() + cur);
                            u.path.assign(s.begin() + cur + 1, s.end());
                            goto end;
                        }
        
                        if (cur == (int) s.size())
                        {
                            if (cur - start == 0) // If you wrote ':' after hostname, provide some valid port value too
                                throw std::logic_error("Url parse failed. Port is empty");
                            u.port.assign(s.begin() + start, s.begin() + cur);
                            goto end;
                        }
                        if (!std::isdigit(s[cur]))
                            throw std::logic_error("Url parse failed. Port should contain digits only");
                        ++cur;
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

        std::string localUniqueFolder();

        FileNameAndPath localUniqueFilePath(const std::string& fileName);

        size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output); 

        size_t headerCallback(char* buffer, size_t size, size_t nitems, std::string* output);

        error::code_t checkECode (int *checked, const bool avail, error::code_t toSet = error::NoError);

        void dirFromUri(const std::string   &uri, std::string *dir,  std::string *file);

        long long getFileSize(const char *fname);

        uintmax_t getFileSize(const fs::directory_entry &entry);

        uintmax_t getFolderSize(const fs::path& folder_path); 

    }
}

#endif //COMMON_H