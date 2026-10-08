#include "ClearMemoryManager.h"

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
    bool shouldClear = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::find(m_writeFileList.begin(), m_writeFileList.end(), strFile);
        if (it == m_writeFileList.end())
        {
            auto it = std::find(m_removeFileList.begin(), m_removeFileList.end(), strFile);
            if (it == m_removeFileList.end())
            {
                if (m_removeFileList.size() >= 10)
                    shouldClear = true;
                m_removeFileList.push_back(strFile);
            }
        }
    }
    if (shouldClear)
        clearMemory();
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
    while (true)
    {
        std::string filename;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_removeFileList.empty())
                break;
            filename = m_removeFileList.back();
        }

        try
        {
            INFOLOG("Delete File:", filename);
            bool removed = false;
            {
                std::lock_guard<std::mutex> cacheLock(nx_spl::aux::localCacheMutex());
                if (!fs::exists(filename.c_str()) || remove(filename.c_str()) == 0)
                    removed = true;
            }
            if (!removed)
            {
                ERRORLOG("Failed to remove file:", filename.c_str());
                break;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_removeFileList.empty() && m_removeFileList.back() == filename)
                m_removeFileList.pop_back();
        }
        catch (const fs::filesystem_error& e)
        {
            INFOLOG("Filesystem error: ", e.what());
        }
        catch (const std::exception& e)
        {
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
                std::vector<std::string> jsonFiles;
                {
                    std::lock_guard<std::mutex> cacheLock(nx_spl::aux::localCacheMutex());
                    for (const auto& entry : fs::recursive_directory_iterator(localFolder))
                    {
                        if (fs::is_regular_file(entry))
                        {
                            const std::string fileName(entry.path().string());
                            if (fileName.find("UploadList.json") != std::string::npos)
                                jsonFiles.push_back(fileName);
                        }
                    }
                }

                for (const auto& jsonFile : jsonFiles)
                    loadJsonFile(jsonFile);

                std::vector<std::string> filesToDelete;
                {
                    std::lock_guard<std::mutex> cacheLock(nx_spl::aux::localCacheMutex());
                    for (const auto& entry : fs::recursive_directory_iterator(localFolder))
                    {
                        if (fs::is_regular_file(entry))
                        {
                            const std::string file(entry.path().filename().string());
                            if (file.find(".mkv") != std::string::npos)
                            {
                                auto it = std::find(m_uploadingFiles.begin(), m_uploadingFiles.end(), file);
                                if (it == m_uploadingFiles.end())
                                    filesToDelete.push_back(entry.path().string());
                            }
                        }
                    }
                }

                for (const auto& filePath : filesToDelete)
                {
                    INFOLOG("delete file:", filePath);
                    bool deleted = false;
                    {
                        std::lock_guard<std::mutex> cacheLock(nx_spl::aux::localCacheMutex());
                        deleted = remove(filePath.c_str()) == 0;
                    }
                    if (!deleted)
                    {
                        ERRORLOG("Failed to delete temp storage file", filePath);
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_removeFileList.push_back(filePath);
                    }
                }

                m_uploadingFiles.clear();
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
                                for (auto& file : filesArray)
                                {
                                    std::string url = file.asString();
                                    // size_t last_underscore_pos = url.find_last_of('_');
                                    // if (last_underscore_pos != std::string::npos) 
                                    // {
                                    //     url = url.substr(0, last_underscore_pos);
                                    //     url.append(".mkv");
                                    // }
                                    nx_spl::aux::FileNameAndPath file_name = nx_spl::aux::localUniqueFilePath(url);
                                    if(fs::exists(file_name.fullPath))
                                    {
                                        DEBUGLOG("m_uploadingFiles:",file_name.name);
                                        m_uploadingFiles.push_back(file_name.name);
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
