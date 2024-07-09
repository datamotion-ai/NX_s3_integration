// Storage_SDK_Installation.cpp : Defines the entry point for the application.
//

#include <openssl/aes.h>
#include <openssl/rand.h>
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

std::string hex_to_string(const std::string& hex_input) {
    std::string output;
    for (size_t i = 0; i < hex_input.length(); i += 2) {
        std::string byte = hex_input.substr(i, 2);
        char chr = (char)(int)strtol(byte.c_str(), nullptr, 16);
        output.push_back(chr);
    }
    return output;
}

std::string string_to_hex(const std::string& input) {
    std::ostringstream hex_stream;
    hex_stream << std::hex << std::setfill('0');
    for (unsigned char c : input) {
        hex_stream << std::setw(2) << static_cast<int>(c);
    }
    return hex_stream.str();
}

int main()
{
    std::cout << "Start Configuring storage sdk" << std::endl;

    std::string username;
    std::string password;
    std::string host = "localhost:7001";
    std::string OEM_name = "Nx Witness";

    unsigned char ckey[16] = "*&^%$#@!$%^&#*^";
    const char ivecstr[AES_BLOCK_SIZE] = "NXStorageSDK\0";

    unsigned char ivec_enc_pass[AES_BLOCK_SIZE];
    memcpy(ivec_enc_pass, ivecstr, AES_BLOCK_SIZE);

    unsigned char ivec_enc_oem[AES_BLOCK_SIZE];
    memcpy(ivec_enc_oem, ivecstr, AES_BLOCK_SIZE);


    std::string tmp;
    std::cout << "Enter host[deafult is localhost:7001]: ";
    std::getline(std::cin, tmp);

    if (!tmp.empty())
        host = tmp;

    std::cout << "Enter username: ";
    std::getline(std::cin, username);

    std::cout << "Enter password: ";
    std::getline(std::cin, password);

    if (username.empty() || password.empty())
    {
        std::cout << "Enter user detailes" << std::endl;
        return -1;
    }
    else if (password.size() > AES_BLOCK_SIZE)
    {
        std::cout << "Password is too large!!, Enter password less then length of " << AES_BLOCK_SIZE << std::endl;
        return -1;
    }

    /* data structure that contains the key itself */
    AES_KEY keyEn;

    int bytes_read;
    unsigned char indata[AES_BLOCK_SIZE];
    unsigned char enc_pass[AES_BLOCK_SIZE];
    unsigned char enc_oem[AES_BLOCK_SIZE];

    /* set the encryption key */
    AES_set_encrypt_key(ckey, 128, &keyEn);

    /* set where on the 128 bit encrypted block to begin encryption*/
    int num = 0;

    strcpy((char*)indata, password.c_str());
    bytes_read = sizeof(indata);

    AES_cfb128_encrypt(indata, enc_pass, bytes_read, &keyEn, ivec_enc_pass, &num, AES_ENCRYPT);

    strcpy((char*)indata, OEM_name.c_str());
    bytes_read = sizeof(indata);

    AES_cfb128_encrypt(indata, enc_oem, bytes_read, &keyEn, ivec_enc_oem, &num, AES_ENCRYPT);

    std::cout << "enc_pass:" << enc_pass << ", enc_oem:" << enc_oem << std::endl;

    std::string pwd_str(reinterpret_cast<char*>(enc_pass), std::strlen(reinterpret_cast<char*>(enc_pass)));
    std::string oem_str(reinterpret_cast<char*>(enc_oem), std::strlen(reinterpret_cast<char*>(enc_oem)));

    std::cout << "pwd_str:" << pwd_str << ", oem_str:" << oem_str << std::endl;

    Json::Value root;
    root["host"] = host;
    root["username"] = username;
    root["password"] = string_to_hex(pwd_str);
    root["OEM"] = string_to_hex(oem_str);

    if (fs::exists("licence.json"))
    {
        remove("licence.json");
    }

    std::ofstream outputFile("licence.json");
    if (!outputFile.is_open()) {
        std::cerr << "Error opening JSON file: licence.json" << std::endl;
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
