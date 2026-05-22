#include <iostream>
#include <cstring>
#include <fstream>
#include <json/json.h>

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem> 
namespace fs = std::experimental::filesystem;
#else
error "Missing the <filesystem> header."
#endif

using namespace std;

int main(int argc, char* argv[])
{
    std::cout << "Start Configuring storage sdk" << std::endl;

    // std::string host = "localhost:7001";
    int local_buffer_size = 1;
    int log_level = 1;
    int log_max = 3;
    int thread_max = 10;

    // std::string tmp;
    // std::cout << "Enter host[default is localhost:7001]: ";
    // std::getline(std::cin, tmp);

    // if (!tmp.empty())
    // host = tmp;

    std::string tmp_buf;
    std::cout << "Enter Local Buffer Size GB[default is 1GB]: ";
    std::getline(std::cin, tmp_buf);

    if (!tmp_buf.empty())
    {
        local_buffer_size = std::stoi(tmp_buf);
    }

    std::string tmp_log;
    std::cout << "Enter 0-2 for log level[default is INFO] 0)DEBUG, 1)INFO, 2)ERROR : ";
    std::getline(std::cin, tmp_log);

    if (!tmp_log.empty())
    {
        log_level = std::stoi(tmp_log);
    }

    std::string tmp_max;
    std::cout << "Enter max log file count[default is 3]: ";
    std::getline(std::cin, tmp_max);

    if (!tmp_max.empty())
    {
        log_max = std::stoi(tmp_max);
    }

    std::string tmp_thread_max;
    std::cout << "Enter max upload thread count[default is 10]: ";
    std::getline(std::cin, tmp_thread_max);

    if (!tmp_thread_max.empty())
    {
        thread_max = std::stoi(tmp_thread_max);
    }

    Json::Value root;
    // root["host"] = host;
    root["local_buffer"] = local_buffer_size;
    root["log_level"] = log_level;
    root["log_max"] = log_max;
    root["max_parallel_upload"] = thread_max;

    if (fs::exists("license.config"))
    {
        remove("license.config");
    }

    std::ofstream outputFile("license.config");
    if (!outputFile.is_open()) {
        std::cerr << "Error opening JSON file: license.json" << std::endl;
        return -1;
    }
    else
    {
        Json::StyledStreamWriter writer;
        writer.write(outputFile, root);
        outputFile.close();
    }

    std::cout << "Configuration has been done successfully" << std::endl;

    return 0;
}