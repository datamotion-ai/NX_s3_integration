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
    std::lock_guard<std::mutex> lock(m_mutex);
    DEBUGLOG("ClearMemoryManager::addFileToRemoveList");
    auto it = std::find(m_writeFileList.begin(), m_writeFileList.end(), strFile);
    if (it == m_writeFileList.end()) 
    {
        auto it = std::find(m_removeFileList.begin(), m_removeFileList.end(), strFile);
        if (it == m_removeFileList.end()) 
        {
            if(m_removeFileList.size() >= 10)
            {
                clearMemory();
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

ClearMemoryManager::ClearMemoryManager(): m_folderCleaned(false)
{
    DEBUGLOG("ClearMemoryManager::ClearMemoryManager");
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
    if(m_folderCleaned == false)
    {
        freeTempStorage();
        m_folderCleaned = true;
    }
    while(!m_removeFileList.empty())
    {
        std::string filename = m_removeFileList.back();
        if (fs::exists(filename.c_str()) && (remove(filename.c_str()) != 0)) 
        {
            ERRORLOG("Failed to remove file:",filename.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(ONE_MINUTE));
        }
        else
        {
            m_removeFileList.pop_back();
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
                for (const auto& entry : fs::recursive_directory_iterator(localFolder)) 
                {
                    if (fs::is_regular_file(entry)) 
                    {
                        const std::string file(entry.path().string());
                        if ((file.find("UploadList.json") == std::string::npos) 
                            && (file.find("info.txt") == std::string::npos)
                            && (file.find(".nxdb") == std::string::npos))
                        {
                            INFOLOG("file:",file);
                            auto it = std::find(m_uploadingFiles.begin(), m_uploadingFiles.end(), file);
                            if (it == m_uploadingFiles.end()) 
                            {
                                INFOLOG("delete file:",entry.path().filename());
                                if(remove(file.c_str()) != 0)
                                {
                                    m_removeFileList.push_back(file);
                                }
                            }
                        }
                    }
                }
                m_uploadingFiles.clear();
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Filesystem error: " << e.what() << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
            }
        }
        INFOLOG("ClearMemoryManager::freeTempStorage Done");
    }

    void ClearMemoryManager::loadJsonFile(std::string filename)
    {
        std::ifstream inputFile(filename);
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
                        if(jsonObject["files"].isArray())
                        {
                            Json::Value& filesArray = jsonObject["files"];
                            if(filesArray.empty() == false)
                            {
                                for (auto& file : filesArray)
                                {
                                    std::string url = file.asString();
                                    size_t last_underscore_pos = url.find_last_of('_');
                                    if (last_underscore_pos != std::string::npos) 
                                    {
                                        url = url.substr(0, last_underscore_pos);
                                        url.append(".mkv");
                                    }
                                    nx_spl::aux::FileNameAndPath file = nx_spl::aux::localUniqueFilePath(std::string(url));
                                    if(fs::exists(file.fullPath))
                                    {
                                        m_uploadingFiles.push_back(file.fullPath);
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
            DEBUGLOG("Error opening JSON file:",file.fullPath);
        }
    }
