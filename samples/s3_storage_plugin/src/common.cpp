#include "common.hpp"

bool g_bucketSizeNeedUpdate = true;
std::vector<std::string> g_removeFileList;

namespace nx_spl
{
    namespace aux
    { 
        /**
         * Generates a pseudo-random file name and a file path to the OS TMP directory + the generated name.
         * \param fileName If set it is appended to the result file name.
         */
        FileNameAndPath localUniqueFilePath(const std::string& fileName)
        {
            /* First, get a system tmp path*/
            std::string tmpFolder;
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
            /* Now, when the base path is found, generate pseudo random bytes for a file name. */
            std::string tempFile = fileName;
            std::replace(tempFile.begin(), tempFile.end(), '/', '_');
            FileNameAndPath nameAndPath;
            nameAndPath.name = tempFile;
            nameAndPath.fullPath = tmpFolder + "/Nx Storage/" + nameAndPath.name;
            return nameAndPath;
        }

        // set error code to initial state (NoError generally if storage is available)
        error::code_t checkECode(int *checked, const int avail, error::code_t toSet)
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
                return -1;

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
                return -1;
            return st.st_size;
        #endif
        }
    }
}