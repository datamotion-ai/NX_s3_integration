//#pragma once

#include "request_handler.h"


//#include <string>

#ifndef PLUGINLICENSEHANDLER_H
#define PLUGINLICENSEHANDLER_H

namespace LicenseManager {
    struct licenseResponse {
        bool action;
        std::string response;
        nlohmann::json data;
    };

    class License_Handler
    {
    public:
        License_Handler(std::string id, bool log = false);
        ~License_Handler();
        bool check_license();
        nlohmann::json validate_device(std::string device_id);
        nlohmann::json add_device(std::string device_id, std::string device_name);
        std::string STATUS = "EXPIRED";
        bool ACTIVE = false;
        int EXPIRY = 0;

    private:
        std::string ID;
        bool log_flag;
    };

    licenseResponse checkLicense(std::string license_id, std::string device_id, std::string device_name, int enable_logger = 0);
}

#endif

// g++ -fPIC -shared -o libplugin_registration_interface.so plugin_registration_interface.cpp request_handler.cpp -lcurl 
