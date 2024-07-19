

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
    std::cout << "Start Configuring" << std::endl;

    std::string OEM_name;
    if(argc > 1)
    {
        OEM_name = argv[1]; //"Nx Witness";
    }
    else
    {
        std::cerr << "use ./Hax_Key_generator OEM_Name " << std::endl;
        return -1;
    }

    std::string oem_str = encryption_str(OEM_name);
    std::string hex_str = string_to_hex(oem_str);
    std::cout << "OEM_name:" << OEM_name << ", Encryption:" << hex_str << std::endl;

    std::string str_hex = hex_to_string(hex_str);
    std::string dec_str = decryption_str(str_hex);
    std::cout << "hex_str:" << hex_str << ", Decryption:" << dec_str << std::endl;

    return 0;
}