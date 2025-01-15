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
    void freeTempStorage();
    void loadJsonFile(std::string filename);

    private:
    bool m_folderCleaned;
    static ClearMemoryManager* m_clManagerPtr;
    std::vector<std::string> m_removeFileList;
    std::vector<std::string> m_writeFileList;
    std::mutex  m_mutex;
    std::vector<std::string> m_uploadingFiles;
    Timer       m_timer;
};

#endif //CLEARMEMORYMANAGER_H