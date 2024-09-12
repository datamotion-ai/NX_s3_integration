

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

std::string encryption_str(const std::string input) {

    unsigned char ckey[16] = "*&^%$#@!$%^&#*^";
    const char ivecstr[AES_BLOCK_SIZE] = "NXStorageSDK\0";

    unsigned char ivec_enc[AES_BLOCK_SIZE];
    memcpy(ivec_enc, ivecstr, AES_BLOCK_SIZE);

    /* data structure that contains the key itself */
    AES_KEY keyEn;

    unsigned char enc[AES_BLOCK_SIZE];
    memset(enc, '\0', AES_BLOCK_SIZE);


    /* set the encryption key */
    AES_set_encrypt_key(ckey, 128, &keyEn);

    /* set where on the 128 bit encrypted block to begin encryption*/
    int num = 0;
    int plaintext_len = input.size();

    AES_cfb128_encrypt(reinterpret_cast<const unsigned char*>(input.c_str()), enc, plaintext_len, &keyEn, ivec_enc, &num, AES_ENCRYPT);

    std::string pwd_str(reinterpret_cast<char*>(enc), std::strlen(reinterpret_cast<char*>(enc)));


    return pwd_str;
}

std::string decryption_str(const std::string input) {

    unsigned char ckey[16] = "*&^%$#@!$%^&#*^";
    const char ivecstr[AES_BLOCK_SIZE] = "NXStorageSDK\0";

    unsigned char ivec_enc[AES_BLOCK_SIZE];
    memcpy(ivec_enc, ivecstr, AES_BLOCK_SIZE);

    /* data structure that contains the key itself */
    AES_KEY keyEn;

    unsigned char enc[AES_BLOCK_SIZE];
    memset(enc, '\0', AES_BLOCK_SIZE);


    /* set the encryption key */
    AES_set_encrypt_key(ckey, 128, &keyEn);

    /* set where on the 128 bit encrypted block to begin encryption*/
    int num = 0;
    int plaintext_len = input.size();

    AES_cfb128_encrypt(reinterpret_cast<const unsigned char*>(input.c_str()), enc, plaintext_len, &keyEn, ivec_enc, &num, AES_DECRYPT);

    std::string pwd_str(reinterpret_cast<char*>(enc), std::strlen(reinterpret_cast<char*>(enc)));

    return pwd_str;
}

int main(int argc, char* argv[])
{
    std::cout << "Start Configuring storage sdk" << std::endl;

    std::string username;
    std::string password;
    std::string host = "localhost:7001";
    std::vector<std::string> OEM_name;
    if(argc >= 2)
    {
        for(int i = 1; i < argc; i++)
        {
            std::string OEM = argv[i]; //"Nx Witness";
            // std::cout << "before decrypt OEM_name: " << OEM << std::endl;
            std::string str_hex = hex_to_string(OEM);
            std::string dec_OEM = decryption_str(str_hex);
            // std::cout << "after decrypt OEM_name: " << dec_OEM << std::endl;
            if(dec_OEM == "Data Motion")
            {
                OEM_name.clear();
                break;
            }
            else
            {
                OEM_name.push_back(dec_OEM);
            }
        }
    }
    else
    {
        std::cerr << "use ./Hax_Key_generator OEM_Name " << std::endl;
        return -1;
    }

    std::string tmp;
    std::cout << "Enter host[default is localhost:7001]: ";
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

    std::string pwd_str = encryption_str(password);
//    std::cout << "pwd_str:" << pwd_str  << std::endl;
//    std::cout << "de pwd_str:" << decryption_str(pwd_str) << std::endl;
    Json::Value root;
    root["host"] = host;
    root["username"] = username;
    root["password"] = string_to_hex(pwd_str);
    Json::Value oemArray(Json::arrayValue);
    
    for(int i =0; i < OEM_name.size(); i++)
    {
        std::string oem_enc_str = encryption_str(OEM_name[i]);
        std::string oem_hex_str = string_to_hex(oem_enc_str);
        oemArray.append(oem_hex_str);
    }
    root["OEM"] = oemArray;

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