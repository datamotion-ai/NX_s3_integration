#include "request_handler.h"

//std::string DEVICE_URL = "https://9invlkt11k.execute-api.eu-west-2.amazonaws.com/prod/device";

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
{
    //DEBUGLOG("TagoServer::WriteCallback");
    size_t total_size = size * nmemb;
    output->append(static_cast<char*>(contents), total_size);
    return total_size;
}

size_t headerCallback(char* buffer, size_t size, size_t nitems, std::string* output)
{
    //DEBUGLOG("TagoServer::headerCallback");
    size_t total_size = size * nitems;
    //std::cout <<"Header: " << std::string(buffer) << std::endl;
    output->append(static_cast<char*>(buffer), total_size);
    return total_size;
}

std::string xstr(std::string word) {
    if (!word.empty() && word.front() == '"' && word.back() == '"') {
        word = word.substr(1, word.size() - 2);
    }
    return word;
}

std::string processQueryParams(nlohmann::json params, bool log = false) {
    std::string Qparams;
    int i = 0;
    for (auto it = params.begin(); it != params.end(); ++it) {
        //std::cout << "QUERY PARAMS" << it.value(); // << ": " << it.value().dump() << std::endl;
        if (i != 0) {
            Qparams += "&";
        }
        i += 1;
        Qparams += xstr(it.key()) + "=" + xstr(it.value().dump());

        if (log)
            std::cout << "Key: " << it.key() << ", Value: " << it.value().dump() << std::endl;
    }
    return Qparams;
}

nlohmann::json make_request(std::string URL, std::string MODE, std::string GET_PARAMS, std::string PAYLOAD, std::string API_KEY, bool log) {
    CURL* curl = curl_easy_init();
    if (log) {
        if ((GET_PARAMS == "")) {
            std::cout << URL << MODE << API_KEY << std::endl;

        }
        else {
            std::cout << URL << MODE << API_KEY << "---" << std::endl;
        }
    }

    //std::string MODE = "get_status";
    nlohmann::json response_json;
    if (!curl)
    {
        std::cout << "Error initializing libcurl.";
    }
    else {
        try {
            if (log)
                std::cout << "Send data!\n";
            //curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
            std::string LINK;
            if (GET_PARAMS == "") {
                LINK = URL;
            }
            else {
                LINK = URL + "?" + GET_PARAMS;
            }

            if (log)
                std::cout << "Link: " << LINK << std::endl;

            curl_easy_setopt(curl, CURLOPT_URL, LINK.c_str());

            //curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            if (MODE == "GET") {
                //curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
                if (log)
                    std::cout << "Method: GET " << CURLOPT_HTTPGET << std::endl;
            }
            else if (MODE == "POST") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
                if (log)
                    std::cout << "Method: POST " << CURLOPT_POST << std::endl;
            }
            else {
                return nlohmann::json::parse("{\"error\":\"mode\"}");
                if (log)
                    std::cout << "Method: Error! " << 0 << std::endl;

            }

            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 0L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
            //curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
            if (log)
                curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
            //curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");

            struct curl_slist* headers = NULL;
            if (API_KEY != "") {
                std::string api_header = "x-api-key: " + API_KEY;
                headers = curl_slist_append(headers, api_header.c_str());
            }
            if (PAYLOAD != "") {
                //std::string data = xstr(PAYLOAD.dump());
                if (log)
                    std::cout << "POST Body: " << PAYLOAD << std::endl;
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, PAYLOAD.c_str());
            }
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            std::string response;
            std::string headerresponse;


            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &headerCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerresponse);

            CURLcode res = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            if (log)
                std::cout << "Send respose!\n";
            if (res != CURLE_OK)
            {
                if (log)
                    std::cout << "curl_easy_perform() failed:" << curl_easy_strerror(res) << headerresponse;
                response_json = nlohmann::json::parse("{}");
            }
            else {
                if (log) {
                    if (response[0] == '<') {
                        std::cout << "Error: ";
                    }
                    std::cout << response << std::endl;
                }
                response_json = nlohmann::json::parse(response);
            }
        }
        catch (const std::exception& ex) {
            if (log)
                std::cout << "curl failed:" << ex.what() << std::endl;
            response_json = nlohmann::json::parse("{}");
        }
    }
    curl_easy_cleanup(curl);
    if (log)
        std::cout << "End request!\n";
    return response_json;
}