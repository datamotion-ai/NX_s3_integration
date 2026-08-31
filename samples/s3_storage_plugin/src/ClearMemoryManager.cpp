#include "ClearMemoryManager.h"
#include <thread>
#include <algorithm>

ClearMemoryManager* ClearMemoryManager::m_clManagerPtr = nullptr;

ClearMemoryManager *ClearMemoryManager::getInstance()
{
    DEBUGLOG("ClearMemoryManager::getInstance");
    if(m_clManagerPtr == nullptr)
    {
        m_clManagerPtr = new ClearMemoryManager();
    }
    return m_clManagerPtr;
}

void ClearMemoryManager::deleteInstance()
{
    DEBUGLOG("ClearMemoryManager::deleteInstance");
    if(m_clManagerPtr != nullptr)
    {
        delete m_clManagerPtr;
        m_clManagerPtr = nullptr;
    }
}

void ClearMemoryManager::addFileToRemoveList(std::string strFile)
{
    DEBUGLOG("ClearMemoryManager::addFileToRemoveList");
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find(m_writeFileList.begin(), m_writeFileList.end(), strFile);
    if (it == m_writeFileList.end()) 
    {
        auto it = std::find(m_removeFileList.begin(), m_removeFileList.end(), strFile);
        if (it == m_removeFileList.end()) 
        {
            if(m_removeFileList.size() >= 10)
            {
                // Defer bulk clear to timer; do not delete under this lock while open/queued.
            }
            m_removeFileList.push_back(strFile); 
        }
    }
}

void ClearMemoryManager::deleteFileFromRemoveList(std::string strFile)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("ClearMemoryManager::deleteFileFromRemoveList");
    auto it = std::find(m_removeFileList.begin(), m_removeFileList.end(), strFile);
    if (it != m_removeFileList.end()) 
    {
        m_removeFileList.erase(it); 
    }
}

void ClearMemoryManager::addFileToWriteList(std::string strFile)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("ClearMemoryManager::addFileToWriteList");
    auto it = std::find(m_writeFileList.begin(), m_writeFileList.end(), strFile);
    if (it == m_writeFileList.end()) 
    {
        m_writeFileList.push_back(strFile); 
    }
}

void ClearMemoryManager::deleteFileFromWriteList(std::string strFile)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("ClearMemoryManager::deleteFileFromWriteList");
    auto it = std::find(m_writeFileList.begin(), m_writeFileList.end(), strFile);
    if (it != m_writeFileList.end()) 
    {
        m_writeFileList.erase(it); 
    }
}

bool ClearMemoryManager::isProtectedFile(const std::string& strFile) const
{
    auto wit = std::find(m_writeFileList.begin(), m_writeFileList.end(), strFile);
    if (wit != m_writeFileList.end())
        return true;

    const std::string base = fs::path(strFile).filename().string();
    for (const std::string& uploading : m_uploadingFiles)
    {
        if (uploading == base || uploading == strFile)
            return true;
    }
    return false;
}

ClearMemoryManager::ClearMemoryManager(): m_folderCleaned(false)
{
    DEBUGLOG("ClearMemoryManager::ClearMemoryManager");
    freeTempStorage();
    m_timer.start(this,&ClearMemoryManager::clearMemory,ONE_MINUTE);
}

ClearMemoryManager::~ClearMemoryManager()
{
    DEBUGLOG("ClearMemoryManager::~ClearMemoryManager");
    m_timer.stop();
    clearMemory();
}

void ClearMemoryManager::clearMemory()
{
    INFOLOG("ClearMemoryManager::clearMemory");
    // Refresh upload-queue protection set each pass.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_uploadingFiles.clear();
    }
    std::string localFolder = nx_spl::aux::localUniqueFolder();
    if(!localFolder.empty() && fs::exists(localFolder))
    {
        try {
            for (const auto& entry : fs::recursive_directory_iterator(localFolder))
            {
                if (fs::is_regular_file(entry))
                {
                    const std::string fileName(entry.path().string());
                    if (fileName.find("UploadList.json") != std::string::npos)
                        loadJsonFile(fileName);
                }
            }
        } catch (const std::exception& e) {
            INFOLOG("Error refreshing upload list: ", e.what());
        }
    }

    while(true)
    {
        std::string filename;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if(m_removeFileList.empty())
                break;
            filename = m_removeFileList.back();
            if(isProtectedFile(filename))
            {
                INFOLOG("Skip delete; file open or queued:", filename);
                m_removeFileList.pop_back();
                continue;
            }
            m_removeFileList.pop_back();
        }
        try {
            INFOLOG("Delete File:", filename);
            if (fs::exists(filename.c_str()) && (remove(filename.c_str()) != 0))
            {
                ERRORLOG("Failed to remove file:", filename.c_str());
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if(std::find(m_removeFileList.begin(), m_removeFileList.end(), filename) == m_removeFileList.end()
                        && !isProtectedFile(filename))
                    {
                        m_removeFileList.push_back(filename);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(ONE_SECOND));
            }
        }
        catch (const fs::filesystem_error& e) {
            INFOLOG("Filesystem error: ", e.what());
        }
        catch (const std::exception& e) {
            INFOLOG("Error: ", e.what());
        }
    }
}

void ClearMemoryManager::freeTempStorage()
{
    INFOLOG("ClearMemoryManager::freeTempStorage--");
    std::string localFolder = nx_spl::aux::localUniqueFolder();
    if(localFolder.empty() == false)
    {
        try {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_uploadingFiles.clear();
            }
            for (const auto& entry : fs::recursive_directory_iterator(localFolder)) 
            {
                if (fs::is_regular_file(entry)) 
                {
                    const std::string fileName(entry.path().string());
                    if (fileName.find("UploadList.json") != std::string::npos)
                    {
                        INFOLOG("Json file:",fileName);
                        loadJsonFile(fileName);
                    }
                }
            }
            // Do NOT delete staged .mkv/.nxdb here. Pending UploadList entries and
            // orphans are re-queued by s3Client::requeueStagedUploads on connect.
            // Deleting the backlog on restart was permanently destroying footage.
            for (const auto& entry : fs::recursive_directory_iterator(localFolder)) 
            {
                if (fs::is_regular_file(entry)) 
                {
                    const std::string file(entry.path().filename().string());
                    if ((file.find(".mkv") != std::string::npos) || (file.find(".nxdb") != std::string::npos))
                    {
                        INFOLOG("Preserving staged file for requeue:", file);
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            INFOLOG( "Filesystem error: ", e.what());
        } catch (const std::exception& e) {
            INFOLOG( "Error: " , e.what() );
        }
    }
    INFOLOG("ClearMemoryManager::freeTempStorage Done");
}

void ClearMemoryManager::loadJsonFile(std::string filename)
{
    INFOLOG("ClearMemoryManager::loadJsonFile",filename);
    std::ifstream inputFile(filename);
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
                    if(jsonObject["files"].isArray())
                    {
                        Json::Value& filesArray = jsonObject["files"];
                        if(filesArray.empty() == false)
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            for (auto& file : filesArray)
                            {
                                std::string url = file.asString();
                                nx_spl::aux::FileNameAndPath file_name = nx_spl::aux::localUniqueFilePath(url);
                                if(fs::exists(file_name.fullPath))
                                {
                                    DEBUGLOG("m_uploadingFiles:",file_name.name);
                                    m_uploadingFiles.push_back(file_name.name);
                                    m_uploadingFiles.push_back(file_name.fullPath);
                                }
                            } 
                        }
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
        DEBUGLOG("Error opening JSON file:",filename);
    }
}
