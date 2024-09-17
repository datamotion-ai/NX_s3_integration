#include <iostream>
#include <curl/curl.h>
#include "json.hpp"


std::string xstr(std::string word);

nlohmann::json make_request(std::string URL, std::string MODE, std::string GET_PARAMS = "", std::string PAYLOAD = "", std::string API_KEY = "", bool log = false);
