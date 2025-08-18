#include "daily_loger.hpp"

nx_spl::aux::DailyLogger::LogPriority nx_spl::aux::DailyLogger::m_verbosity = nx_spl::aux::DailyLogger::LogPriority::DebugP;
std::string nx_spl::aux::DailyLogger::m_logDirectory = "./logs";  // Default log directory
std::string nx_spl::aux::DailyLogger::m_currentLogFile;
std::mutex  nx_spl::aux::DailyLogger::m_mutex;
std::ofstream nx_spl::aux::DailyLogger::m_file;
int nx_spl::aux::DailyLogger::m_maxLogFiles = 3;
bool nx_spl::aux::DailyLogger::m_initialized = false;


void nx_spl::aux::DailyLogger::updateLogFile() 
{
    std::time_t rawTime;
    std::tm* timeInfo;
    char buffer[80];
    std::size_t maxSizeInBytes = 5 * 1024 * 1024; // 5 MB

    std::time(&rawTime);
    timeInfo = std::localtime(&rawTime);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", timeInfo);
    std::string currentDate(buffer);

    if (m_currentLogFile.empty() || (m_currentLogFile.find(currentDate) == std::string::npos)) 
    {
        if (m_file.is_open())
            m_file.close();
        if((m_currentLogFile.empty() == false) && fs::exists(m_currentLogFile))
        {
            std::string rotatedLogFile = generateRotatedLogFileName(m_currentLogFile);
            fs::rename(m_currentLogFile, rotatedLogFile);
        }
        m_currentLogFile = m_logDirectory + "/log_" + currentDate + ".txt";
        m_file.open(m_currentLogFile,std::ios_base::app);
        deleteOldLogFiles();
    }
    else if (fs::exists(m_currentLogFile) && fs::file_size(m_currentLogFile) >= maxSizeInBytes) 
    {
        if (m_file.is_open())
            m_file.close();
        
        std::string rotatedLogFile = generateRotatedLogFileName(m_currentLogFile);
        fs::rename(m_currentLogFile, rotatedLogFile);

        m_currentLogFile = m_logDirectory + "/log_" + currentDate + ".txt";
        m_file.open(m_currentLogFile,std::ios_base::app);
        deleteOldLogFiles();
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
    if(m_verbosity != new_priority)
    {
        INFOLOG("Log level changed",new_priority)
        m_verbosity = new_priority;
    }
}

void nx_spl::aux::DailyLogger::SetMaxLogFileCount(const int logCount)
{
    if((m_maxLogFiles != logCount) && (logCount > 0))
    {
        INFOLOG("Log count changed",m_maxLogFiles)
        m_maxLogFiles = logCount;
    }
    m_initialized = true;
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

void nx_spl::aux::DailyLogger::deleteOldLogFiles() 
{
    std::string logFilePrefix = "log_"; 
    std::vector<fs::path> logFiles;

    for (const auto& entry : fs::directory_iterator(m_logDirectory)) 
    {
        if (entry.is_regular_file()) 
        {
            const std::string fileName = entry.path().filename().string();
        
            if (fileName.find(logFilePrefix) == 0) 
            {
                logFiles.push_back(entry.path());
            }
        }
    }

    if(logFiles.empty() || !m_initialized || (logFiles.size() <= m_maxLogFiles))
        return;

    std::sort(logFiles.begin(), logFiles.end(), [](const fs::path& a, const fs::path& b) 
    {
        return fs::last_write_time(a) < fs::last_write_time(b);
    });

    while (logFiles.size() > m_maxLogFiles) 
    {
        fs::remove(logFiles.front());
        logFiles.erase(logFiles.begin());
    }
    logFiles.clear();
}

// Helper function to generate a new filename for rotated log files
std::string nx_spl::aux::DailyLogger::generateRotatedLogFileName(const std::string& logFilePath) 
{
    // Get the current time
    std::time_t rawTime = std::time(nullptr);
    std::tm* timeInfo = std::localtime(&rawTime);

    // Create a timestamp string
    std::ostringstream timestamp;
    timestamp << std::put_time(timeInfo, "%Y-%m-%d_%H-%M-%S");

    // Append the timestamp to the current log filename (before the extension if any)
    std::string rotatedFileName = m_logDirectory + "/log_" + timestamp.str() + ".txt";

    // Return the full path of the rotated log file
    return rotatedFileName;
}