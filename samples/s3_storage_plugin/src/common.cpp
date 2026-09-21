#include "common.hpp"

std::string g_userAgent;
std::string g_VMS;

namespace nx_spl
{
    namespace aux
    { 

        std::string localUniqueFolder()
        {
            /* First, get a system tmp path*/
            std::string tmpFolder ;
            #if defined (_WIN32)
                char buf[MAX_PATH + 1];
                DWORD result = GetTempPathA(sizeof(buf), buf);
                assert(result > 0);
                if (result == 0)
                    ERRORLOG("Failed to get a temporary folder path")
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
            if (!fs::exists(tmpFolder + "/Nx Storage"))
            {
                try 
                {
                    fs::create_directory(tmpFolder + "/Nx Storage");
                } 
                catch (const std::exception& e) 
                {
                    ERRORLOG("Failed to create folder:",e.what());
                }
            }
            tmpFolder += "/Nx Storage";
            return tmpFolder;
        }
        /**
         * Generates a pseudo-random file name and a file path to the OS TMP directory + the generated name.
         * \param fileName If set it is appended to the result file name.
         */
        FileNameAndPath localUniqueFilePath(const std::string& fileName)
        {
            std::string tmpFolder = nx_spl::aux::localUniqueFolder();
            /* Now, when the base path is found, generate pseudo random bytes for a file name. */
            std::string tempFile = tmpFolder + fileName;
            // std::replace(tempFile.begin(), tempFile.end(), '/', '_');
            fs::path p(tempFile);
            FileNameAndPath nameAndPath;
            nameAndPath.name = p.filename().string();
            nameAndPath.folderPath = p.parent_path().string();
            nameAndPath.fullPath = tempFile;
            return nameAndPath;
        }

        // set error code to initial state (NoError generally if storage is available)
        error::code_t checkECode(int *checked, const bool avail, error::code_t toSet)
        {
            try
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
            catch(const std::exception& e)
            {
                ERRORLOG("Error:",e.what());
                return error::code_t::UnknownError;
            }
        }

        size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) 
        {
            size_t total_size = size * nmemb;
            output->append(static_cast<char*>(contents), total_size);
            return total_size;
        }

        size_t headerCallback(char* buffer, size_t size, size_t nitems, std::string* output)
        {
            size_t total_size = size * nitems;
            std::string header(buffer, total_size);
            if (header.compare(0, 7, "Server:") == 0)
            {
                *output = header.substr(8);
            }
            return total_size;
        }

        void dirFromUri(const std::string   &uri, std::string *dir,  std::string *file)
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

        // get local file size by it's name
        long long getFileSize(const char *fname)
        {
        #ifdef _WIN32

            HANDLE hFile = CreateFileA(
                fname,
                GENERIC_READ,
                FILE_SHARE_READ,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            if (hFile == INVALID_HANDLE_VALUE)
            {
                ERRORLOG("Failed to get file size INVALID_HANDLE_VALUE",fname);
                return -1;
            }

            LARGE_INTEGER s;
            if (!GetFileSizeEx(hFile, &s))
            {
                ERRORLOG("Failed to get file size",fname);
                CloseHandle(hFile);
                return -1;
            }
            CloseHandle(hFile);
            return s.QuadPart;

        #elif defined(__linux__) || defined(__APPLE__)

            struct stat st;
            if (stat(fname, &st) == -1)
            {
                ERRORLOG("Failed to get file size",fname);
                return -1;
            }
            return st.st_size;
        #endif
        }

        uintmax_t getFolderSize(const fs::path &folder_path)
        {
            // Never throws: this is called from ABI entry points (write/open/isAvailable) and
            // from getCapabilities(), where an escaping exception would cross into the Media Server.
            uintmax_t size = 0;
            std::error_code ec;
            if (!fs::exists(folder_path, ec) || ec)
                return 0;
            try{
                fs::recursive_directory_iterator it(folder_path, fs::directory_options::skip_permission_denied, ec);
                fs::recursive_directory_iterator end;
                if (ec)
                {
                    ERRORLOG("DMError: getFolderSize cannot open", folder_path.string(), ec.message());
                    return 0;
                }
                while (it != end)
                {
                    size += getFileSize(*it);
                    it.increment(ec);
                    if (ec)
                    {
                        // A file removed by the uploader/cleaner mid-walk is normal; keep what we have.
                        DEBUGLOG("getFolderSize increment error", ec.message());
                        ec.clear();
                        break;
                    }
                }
            }catch (const std::exception &ex) {
                ERRORLOG("DMError: Exception in getFolderSize ", ex.what());
            }catch (...) {
                ERRORLOG("DMError: Unknown exception in getFolderSize");
            }
            DEBUGLOG("getFolderSize",size);
            return size;
        }

        bool isClosedSegmentName(const std::string& fileName)
        {
            static const std::string kExt = ".mkv";
            if (fileName.size() <= kExt.size()
                || fileName.compare(fileName.size() - kExt.size(), kExt.size(), kExt) != 0)
            {
                return false;
            }
            const std::string stem = fileName.substr(0, fileName.size() - kExt.size());
            const size_t underscore = stem.find('_');
            if (underscore == std::string::npos || underscore == 0 || underscore + 1 >= stem.size())
                return false;
            for (size_t i = 0; i < stem.size(); ++i)
            {
                if (i == underscore)
                    continue;
                if (!std::isdigit(static_cast<unsigned char>(stem[i])))
                    return false;
            }
            return true;
        }

        StagingUsage& StagingUsage::instance()
        {
            static StagingUsage s_instance;
            return s_instance;
        }

        void StagingUsage::refreshLocked(const std::string& scope, Entry& entry)
        {
            const auto now = std::chrono::steady_clock::now();
            if (entry.measuredAt.time_since_epoch().count() != 0
                && now - entry.measuredAt < std::chrono::seconds(STAGING_USAGE_REFRESH_SECONDS))
            {
                return;
            }
            std::string root = localUniqueFolder();
            fs::path target = scope.empty() ? fs::path(root) : fs::path(root) / scope;
            entry.bytes = getFolderSize(target);
            entry.measuredAt = now;
        }

        void StagingUsage::refreshDiskLocked()
        {
            const auto now = std::chrono::steady_clock::now();
            if (m_diskMeasuredAt.time_since_epoch().count() != 0
                && now - m_diskMeasuredAt < std::chrono::seconds(STAGING_USAGE_REFRESH_SECONDS))
            {
                return;
            }
            std::error_code ec;
            const fs::space_info info = fs::space(localUniqueFolder(), ec);
            if (!ec && info.available != static_cast<uintmax_t>(-1))
                m_diskFree = info.available;
            // On error keep the last good value.
            m_diskMeasuredAt = now;
        }

        uintmax_t StagingUsage::bytes(const std::string& scope)
        {
            try
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                Entry& entry = m_entries[scope];
                refreshLocked(scope, entry);
                return entry.bytes;
            }
            catch (const std::exception& e)
            {
                ERRORLOG("StagingUsage::bytes", e.what());
                return 0;
            }
        }

        uintmax_t StagingUsage::diskFree()
        {
            try
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                refreshDiskLocked();
                return m_diskFree;
            }
            catch (const std::exception& e)
            {
                ERRORLOG("StagingUsage::diskFree", e.what());
                return static_cast<uintmax_t>(-1);
            }
        }

        bool StagingUsage::isFull(const std::string& scope, uint64_t limitBytes)
        {
            try
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                Entry& entry = m_entries[scope];
                refreshLocked(scope, entry);
                refreshDiskLocked();

                const bool diskLow = (m_diskMeasuredAt.time_since_epoch().count() != 0)
                    && (m_diskFree < STAGING_MIN_DISK_FREE_BYTES);
                const uint64_t resumeBelow = static_cast<uint64_t>(limitBytes * BUFFER_RESUME_RATIO);

                if (!entry.full)
                {
                    if (entry.bytes > limitBytes || diskLow)
                    {
                        entry.full = true;
                        INFOLOG("Local buffer full, storage temporarily unavailable",
                                "scope=", scope, "bytes=", entry.bytes, "limit=", limitBytes,
                                "diskFree=", m_diskFree);
                    }
                }
                else
                {
                    if (entry.bytes < resumeBelow && !diskLow)
                    {
                        entry.full = false;
                        INFOLOG("Local buffer drained, storage available",
                                "scope=", scope, "bytes=", entry.bytes, "resumeBelow=", resumeBelow,
                                "diskFree=", m_diskFree);
                    }
                }
                return entry.full;
            }
            catch (const std::exception& e)
            {
                ERRORLOG("StagingUsage::isFull", e.what());
                return false;
            }
        }

        void StagingUsage::invalidate()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& kv : m_entries)
                kv.second.measuredAt = std::chrono::steady_clock::time_point{};
            m_diskMeasuredAt = std::chrono::steady_clock::time_point{};
        }

        uintmax_t getFileSize(const fs::directory_entry &entry) 
        {
            try {

                if (fs::is_regular_file(entry)) 
                {
                    return fs::file_size(entry);
                }

            } catch (const fs::filesystem_error &fs_err) {  
                ERRORLOG("DMError: Filesystem error in getFileSize ", fs_err.what());
            } catch (const std::exception &ex) {
                ERRORLOG("DMError: Exception in getFileSize ", ex.what());
            } catch (...) {
                ERRORLOG("DMError: Unknown error in getFileSize while accessing file size.");
            }
            return 0; // Return 0 if there was an error
        }

        std::string getCurrentDate() {
            std::time_t now = std::time(nullptr);
            std::tm local_tm = *std::localtime(&now);

            std::mktime(&local_tm);

            char buffer[20];
            std::strftime(buffer, sizeof(buffer), "%d-%m-%Y", &local_tm);

            return std::string(buffer);
        }

        bool parseGenerationalNxdb(const std::string& path, std::string* prefix, int* generation)
        {
            static const std::string kSuffix = ".nxdb";
            if (path.size() < kSuffix.size() + 3) // "--0.nxdb" minimum after some prefix
                return false;
            if (path.compare(path.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
                return false;

            const size_t dotPos = path.size() - kSuffix.size();
            const size_t dashPos = path.rfind("--", dotPos);
            if (dashPos == std::string::npos || dashPos + 2 >= dotPos)
                return false;

            int gen = 0;
            for (size_t i = dashPos + 2; i < dotPos; ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(path[i])))
                    return false;
                gen = gen * 10 + (path[i] - '0');
            }

            if (prefix)
                *prefix = path.substr(0, dashPos);
            if (generation)
                *generation = gen;
            return true;
        }

        bool isGenerationalNxdb(const std::string& path)
        {
            return parseGenerationalNxdb(path, nullptr, nullptr);
        }

        std::string successorGenerationalNxdb(const std::string& path)
        {
            std::string prefix;
            int generation = 0;
            if (!parseGenerationalNxdb(path, &prefix, &generation))
                return std::string();
            return prefix + "--" + std::to_string(generation + 1) + ".nxdb";
        }
    }
}
