#ifndef DAILY_LOGER_H
#define DAILY_LOGER_H

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

#define DEBUGLOG(...) ""
// #define INFOLOG(...) ""
// #define DEBUGLOG(...) nx_spl::aux::DailyLogger::Log(nx_spl::aux::DailyLogger::LogPriority::DebugP, __FUNCTION__, __LINE__, __VA_ARGS__);
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
                    DebugP, InfoP, WarnP, ErrorP, CriticalP, FatalP
                };

            private:
                static LogPriority m_verbosity;
                static std::string m_logDirectory;
                static std::string m_currentLogFile;
                static std::mutex  m_mutex;
                static std::ofstream m_file;

            public:
                static void SetVerbosity(LogPriority new_priority) 
                {
                    m_verbosity = new_priority;
                }

                template <typename... Args>
                static void Log(LogPriority priority, const char* functionName, int lineNumber, Args&&... args) 
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_file.is_open())
                    {
                        if (priority >= m_verbosity) 
                        {
                            switch (priority) 
                            {
                                case DebugP: m_file << "Debug:\t"; break;
                                case InfoP: m_file << "Info:\t"; break;
                                case WarnP: m_file << "Warn:\t"; break;
                                case ErrorP: m_file << "Error:\t"; break;
                                case CriticalP: m_file << "Critical:\t"; break;
                                case FatalP: m_file << "Fatal:\t"; break;
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

                static void Initialize() 
                {
                    createLogDirectory();
                    updateLogFile();
                }

                static void Dinitialize()
                {
                    if (m_file.is_open())
                        m_file.close();
                    m_currentLogFile.clear();
                }

            private:
                static void updateLogFile() 
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

                static void createLogDirectory() 
                {
                    if (!fs::exists(m_logDirectory)) 
                    {
                        fs::create_directory(m_logDirectory);
                    }
                }

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

nx_spl::aux::DailyLogger::LogPriority nx_spl::aux::DailyLogger::m_verbosity = nx_spl::aux::DailyLogger::LogPriority::DebugP;
std::string nx_spl::aux::DailyLogger::m_logDirectory = "./logs";  // Default log directory
std::string nx_spl::aux::DailyLogger::m_currentLogFile;
std::mutex  nx_spl::aux::DailyLogger::m_mutex;
std::ofstream nx_spl::aux::DailyLogger::m_file;

#endif //DAILY_LOGER_H