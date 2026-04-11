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
    bool logFileCreated = false;
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
        logFileCreated = true;
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
        logFileCreated = true;
        deleteOldLogFiles();
    }
    if (logFileCreated)
    {
        INFOLOG("============================= NXPlugin ================================");
        std::string os_name;
        std::string os_version;
        std::string architecture = "x64";
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* localTime = std::localtime(&now_c);
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::stringstream ss;
        ss << std::put_time(localTime, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << milliseconds.count();
        std::string local_time = ss.str();
        ss.clear();

#if defined (_WIN32)

        os_name = "Windows";
        NTSTATUS(WINAPI * RtlGetVersion)(LPOSVERSIONINFOEXW);
        OSVERSIONINFOEXW osInfo;

        *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

        if (NULL != RtlGetVersion)
        {
            osInfo.dwOSVersionInfoSize = sizeof(osInfo);
            RtlGetVersion(&osInfo);
        }
        os_version = std::to_string(osInfo.dwMajorVersion) + "." + std::to_string(osInfo.dwMinorVersion) + "." + std::to_string(osInfo.dwBuildNumber);

#else
        os_name = "LINUX";
        struct utsname unameData;
        uname(&unameData);
        os_version = unameData.release;
#endif
        INFOLOG("============= software_name:", "Wasabi_storage_sdk");
        INFOLOG("============= software_version:", VERSION);
        INFOLOG("============= os_name:", os_name);
        INFOLOG("============= os_version:", os_version);
        INFOLOG("============= architecture:", architecture);
        INFOLOG("============= software_localtime:", local_time);
        INFOLOG("=======================================================================");
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
    if (!m_initialized)
    {
        return;
    }

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

    if(logFiles.empty() || (logFiles.size() <= m_maxLogFiles))
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