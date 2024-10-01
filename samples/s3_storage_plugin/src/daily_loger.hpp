#ifndef _DAILY_LOGGER_H_
#define _DAILY_LOGGER_H_

#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <stdint.h>
#include <mutex>
#include <fstream>
#include <ctime>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace fs = std::filesystem;

// #define DEBUGLOG(...) ""
//#define INFOLOG(...) ""
#define DEBUGLOG(...) nx_spl::aux::DailyLogger::Log(nx_spl::aux::DailyLogger::LogPriority::DebugP, __FUNCTION__, __LINE__, __VA_ARGS__);
#define INFOLOG(...) nx_spl::aux::DailyLogger::Log(nx_spl::aux::DailyLogger::LogPriority::InfoP, __FUNCTION__, __LINE__, __VA_ARGS__);
#define ERRORLOG(...) nx_spl::aux::DailyLogger::Log(nx_spl::aux::DailyLogger::LogPriority::ErrorP, __FUNCTION__, __LINE__, __VA_ARGS__);

namespace nx_spl
{
    namespace aux
    { 

        class DailyLogger 
        {
            public:
                enum LogPriority 
                {
                    DebugP = 0, InfoP, ErrorP
                };

            private:
                static LogPriority m_verbosity;
                static std::string m_logDirectory;
                static std::string m_currentLogFile;
                static std::mutex  m_mutex;
                static std::ofstream m_file;

            public:
                static void SetVerbosity(LogPriority new_priority);

                template <typename... Args>
                static void Log(LogPriority priority, const char* functionName, int lineNumber, Args&&... args) 
                {
                    if ((priority < m_verbosity) || m_currentLogFile.empty())
                    {
                        return;
                    } 
                    else
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_file.open(m_currentLogFile,std::ios_base::app);
                        if (m_file.is_open())
                        {
                            switch (priority) 
                            {
                                case DebugP: m_file << "Debug:\t"; break;
                                case InfoP: m_file << "Info:\t"; break;
                                case ErrorP: m_file << "Error:\t"; break;
                            }

                            // Get current timestamp
                            std::time_t rawTime;
                            std::tm* timeInfo;
                            char buffer[80];

                            std::time(&rawTime);
                            timeInfo = std::localtime(&rawTime);

                            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeInfo);
                            std::string timestamp(buffer);

                            m_file << "[" << timestamp << "] ";
                            
                            m_file << lineNumber << " : " << functionName << "\t";
                            logMultipleStrings(m_file, std::forward<Args>(args)...);
                            m_file << "\n";
                            m_file.close();
                            updateLogFile();
                        }
                    }  
                    
                }

                static void Initialize();

                static void Dinitialize();

                static void deleteOldLogFiles();

                static std::string generateRotatedLogFileName(const std::string &logFilePath);

            private:
                static void updateLogFile();
                static void createLogDirectory();
                template <typename T>
                static void logMultipleStrings(std::ostream& stream, T&& arg) 
                {
                    stream << "," << arg ;
                }

                template <typename T, typename... Args>
                static void logMultipleStrings(std::ostream& stream, T&& arg, Args&&... args) 
                {
                    stream << "," << arg ;
                    logMultipleStrings(stream, std::forward<Args>(args)...);
                }
        };
    }
}

#endif //_DAILY_LOGGER_H_