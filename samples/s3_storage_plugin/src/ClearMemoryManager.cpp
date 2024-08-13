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

ClearMemoryManager::ClearMemoryManager()
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
