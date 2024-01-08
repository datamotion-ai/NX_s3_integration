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

namespace fs = std::filesystem;

#define DEBUGLOG(...) ""
//#define DEBUGLOG(...) nx_spl::aux::DailyLogger::Log(nx_spl::aux::DailyLogger::LogPriority::DebugP, __FUNCTION__, __LINE__, __VA_ARGS__);
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
                static LogPriority verbosity;
                static std::string logDirectory;
                static std::string currentLogFile;

            public:
                static void SetVerbosity(LogPriority new_priority) 
                {
                    verbosity = new_priority;
                }

                template <typename... Args>
                static void Log(LogPriority priority, const char* functionName, int lineNumber, Args&&... args) 
                {
                    if (priority >= verbosity) 
                    {
                        std::ofstream FILE(currentLogFile, std::ios_base::app);

                        switch (priority) 
                        {
                            case DebugP: FILE << "Debug:\t"; break;
                            case InfoP: FILE << "Info:\t"; break;
                            case WarnP: FILE << "Warn:\t"; break;
                            case ErrorP: FILE << "Error:\t"; break;
                            case CriticalP: FILE << "Critical:\t"; break;
                            case FatalP: FILE << "Fatal:\t"; break;
                        }

                        // Get current timestamp
                        std::time_t rawTime;
                        std::tm* timeInfo;
                        char buffer[80];

                        std::time(&rawTime);
                        timeInfo = std::localtime(&rawTime);

                        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeInfo);
                        std::string timestamp(buffer);

                        FILE << "[" << timestamp << "] ";
                        
                        FILE << lineNumber << " : " << functionName << "\t";
                        logMultipleStrings(FILE, std::forward<Args>(args)...);
                        FILE << "\n";
                        FILE.close();
                        updateLogFile();
                    }
                }

                static void Initialize() 
                {
                    updateLogFile();
                    createLogDirectory();
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

                    if (currentLogFile.empty() || currentDate != currentLogFile) 
                    {
                        currentLogFile = logDirectory + "/log_" + currentDate + ".txt";
                    }
                }

                static void createLogDirectory() 
                {
                    if (!fs::exists(logDirectory)) 
                    {
                        fs::create_directory(logDirectory);
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

nx_spl::aux::DailyLogger::LogPriority nx_spl::aux::DailyLogger::verbosity = nx_spl::aux::DailyLogger::LogPriority::DebugP;
std::string nx_spl::aux::DailyLogger::logDirectory = "./logs";  // Default log directory
std::string nx_spl::aux::DailyLogger::currentLogFile;

#endif //DAILY_LOGER_H