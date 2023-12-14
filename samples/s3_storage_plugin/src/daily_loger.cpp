// #include "daily_loger.h"

// namespace fs = std::filesystem;

// // DailyLogger::LogPriority DailyLogger::DailyLogger::verbosity = DailyLogger::LogPriority::DebugP;
// // std::string DailyLogger::logDirectory = "./logs";  // Default log directory
// // std::string DailyLogger::currentLogFile;

// void DailyLogger::SetVerbosity(LogPriority new_priority)
// {
//     verbosity = new_priority;
// }

// template <typename... Args>
// void DailyLogger::Log(LogPriority priority, const char* functionName, int lineNumber, Args&&... args)
// {

//     if (priority >= DailyLogger::verbosity) 
//     {
//         std::ofstream FILE(currentLogFile, std::ios_base::app);

//         switch (priority) 
//         {
//             case DebugP: FILE << "Debug:\t"; break;
//             case InfoP: FILE << "Info:\t"; break;
//             case WarnP: FILE << "Warn:\t"; break;
//             case ErrorP: FILE << "Error:\t"; break;
//             case CriticalP: FILE << "Critical:\t"; break;
//             case FatalP: FILE << "Fatal:\t"; break;
//         }

//         // Get current timestamp
//         std::time_t rawTime;
//         std::tm* timeInfo;
//         char buffer[80];

//         std::time(&rawTime);
//         timeInfo = std::localtime(&rawTime);

//         std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeInfo);
//         std::string timestamp(buffer);

//         FILE << "[" << timestamp << "] ";
        
//         FILE << lineNumber << " : " << functionName << "\t";
//         logMultipleStrings(FILE, std::forward<Args>(args)...);
//         FILE << "\n";
//         FILE.close();
//         updateLogFile();
//     }
// }

// void DailyLogger::Initialize() 
// {
//     updateLogFile();
//     createLogDirectory();
// }

// void DailyLogger::updateLogFile() 
// {
//     std::time_t rawTime;
//     std::tm* timeInfo;
//     char buffer[80];

//     std::time(&rawTime);
//     timeInfo = std::localtime(&rawTime);

//     std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", timeInfo);
//     std::string currentDate(buffer);

//     if (currentLogFile.empty() || currentDate != currentLogFile) 
//     {
//         currentLogFile = logDirectory + "/log_" + currentDate + ".txt";
//     }
// }

// void DailyLogger::createLogDirectory() 
// {
//     if (!fs::exists(logDirectory)) 
//     {
//         fs::create_directory(logDirectory);
//     }
// }

// template <typename T>
// void DailyLogger::logMultipleStrings(std::ostream& stream, T&& arg) 
// {
//     stream << "," << arg ;
// }

// template <typename T, typename... Args>
// void DailyLogger::logMultipleStrings(std::ostream& stream, T&& arg, Args&&... args) 
// {
//     stream << "," << arg ;
//     logMultipleStrings(stream, std::forward<Args>(args)...);
// }