// #pragma once

// #include <fstream>
// #include <ctime>
// #include <sstream>
// #include <filesystem>
// #include <iostream>

// // #define DEBUGLOG(...) ""
// // //#define DEBUGLOG(...) DailyLogger::Log(DailyLogger::LogPriority::DebugP, __FUNCTION__, __LINE__, __VA_ARGS__);
// // #define INFOLOG(...) DailyLogger::Log(DailyLogger::LogPriority::InfoP, __FUNCTION__, __LINE__, __VA_ARGS__);
// // #define ERRORLOG(...) DailyLogger::Log(DailyLogger::LogPriority::ErrorP, __FUNCTION__, __LINE__, __VA_ARGS__);


// class DailyLogger 
// {
//     public:
//         enum LogPriority 
//         {
//             DebugP, InfoP, WarnP, ErrorP, CriticalP, FatalP
//         };

//     private:
//         static LogPriority verbosity;
//         static std::string logDirectory;
//         static std::string currentLogFile;

//     public:
//         static void SetVerbosity(LogPriority new_priority);

//         template <typename... Args>
//         static void Log(LogPriority priority, const char* functionName, int lineNumber, Args&&... args);

//         static void Initialize();

//     private:
//         static void updateLogFile();

//         static void createLogDirectory();

//         template <typename T>
//         static void logMultipleStrings(std::ostream& stream, T&& arg);

//         template <typename T, typename... Args>
//         void logMultipleStrings(std::ostream& stream, T&& arg, Args&&... args);
// };