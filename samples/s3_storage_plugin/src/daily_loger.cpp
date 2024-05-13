#pragma once

#include "daily_loger.hpp"

nx_spl::aux::DailyLogger::LogPriority nx_spl::aux::DailyLogger::m_verbosity = nx_spl::aux::DailyLogger::LogPriority::DebugP;
std::string nx_spl::aux::DailyLogger::m_logDirectory = "./logs";  // Default log directory
std::string nx_spl::aux::DailyLogger::m_currentLogFile;
std::mutex  nx_spl::aux::DailyLogger::m_mutex;
std::ofstream nx_spl::aux::DailyLogger::m_file;


void nx_spl::aux::DailyLogger::updateLogFile() 
{
    std::time_t rawTime;
    std::tm* timeInfo;
    char buffer[80];

    std::time(&rawTime);
    timeInfo = std::localtime(&rawTime);

    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", timeInfo);
    std::string currentDate(buffer);

    if (m_currentLogFile.empty() || currentDate != m_currentLogFile) 
    {
        if (m_file.is_open())
            m_file.close();
        m_currentLogFile = m_logDirectory + "/log_" + currentDate + ".txt";
        m_file.open(m_currentLogFile,std::ios_base::app);
    }
}

void nx_spl::aux::DailyLogger::createLogDirectory() 
{
    if (!fs::exists(m_logDirectory)) 
    {
        fs::create_directory(m_logDirectory);
    }
}

void nx_spl::aux::DailyLogger::SetVerbosity(LogPriority new_priority) 
{
    m_verbosity = new_priority;
}

void nx_spl::aux::DailyLogger::Initialize() 
{
    createLogDirectory();
    updateLogFile();
}

void nx_spl::aux::DailyLogger::Dinitialize()
{
    if (m_file.is_open())
        m_file.close();
    m_currentLogFile.clear();
}

