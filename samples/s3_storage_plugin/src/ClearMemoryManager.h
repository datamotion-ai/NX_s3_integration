#ifndef CLEARMEMORYMANAGER_H
#define CLEARMEMORYMANAGER_H

#include "common.hpp"
#include "timer.h"

class ClearMemoryManager
{
    public:
    static ClearMemoryManager* getInstance();
    static void deleteInstance();
    void addFileToRemoveList(std::string strFile);
    void deleteFileFromRemoveList(std::string strFile);
    void addFileToWriteList(std::string strFile);
    void deleteFileFromWriteList(std::string strFile);

    private:
    ClearMemoryManager();
    ~ClearMemoryManager();
    void clearMemory();

    private:
    static ClearMemoryManager* m_clManagerPtr;
    std::vector<std::string> m_removeFileList;
    std::vector<std::string> m_writeFileList;
    std::mutex  m_mutex;
    Timer       m_timer;
};

#endif //CLEARMEMORYMANAGER_H