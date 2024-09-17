#include "license_handler.h"

#include <thread>
#include <chrono>

using namespace LicenseManager;


std::string DEVICE_URL = "https://u4v0bvs3v8.execute-api.eu-west-2.amazonaws.com/prod/device";
std::string API_KEY = "MTeB8cBKbs4PxC7EBrrCm5gaOGSuNkm03pSaRVMF";

License_Handler::License_Handler(std::string id,  bool log) {
    ID = id;
    log_flag = log;
}
License_Handler::~License_Handler() {
    if (log_flag)
        std::cout << "License Manager is Terminating!\n"; //handler
}
bool License_Handler::check_license() {
    //std::string qparams = "{ \"mode\" : \"get_status\", \"id\" : \""+ID+"\"}";
    std::string qparams = "mode=get_status&id=" + ID;
    nlohmann::json result = make_request(DEVICE_URL, "GET", qparams, "", API_KEY, log_flag);
    if (result.contains("status") && result.contains("expiry_time")) {
        if (log_flag) {
            std::cout << "License Status: " << xstr(result["status"]) << std::endl;
            std::cout << "License Expiry Time: " << result["expiry_time"] << std::endl;
        }

        STATUS = xstr(result["status"]);
        EXPIRY = result["expiry_time"];
        ACTIVE = ( xstr(result["status"]) == "ACTIVE" );
    }
    else {
        if (log_flag)
            std::cout << "response: Invalid JSON! \n Result:" << result.dump(4) << std::endl;
        ACTIVE = false;
    }
    return ACTIVE;
}
nlohmann::json License_Handler::validate_device(std::string device_id) {
    //std::string qparams = "{ \"mode\" : \"validate_device\", \"id\" : \"" + ID + "\", \"device_id\" : \"" + device_id + "\"}";
    std::string qparams = "mode=validate_device&id=" + ID + "&device_id=" + device_id;
    nlohmann::json result = make_request(DEVICE_URL, "GET", qparams, "", API_KEY, log_flag);
    bool response = false;
    if (result.contains("status") && result.contains("expiry_time") && result.contains("validation")) {
        response = result["validation"]==true;

        if (log_flag) {
            std::cout << "License Status: " << xstr(result["status"]) << std::endl;
            std::cout << "License Expiry Time: " << result["expiry_time"] << " days remaining" << std::endl;
        }

        if (response) {
            if (log_flag)
                std::cout << "License Validation: True" << std::endl;
            ACTIVE = true;
        }
        else {
            if (log_flag)
                std::cout << "License Validation: False" << std::endl;
            ACTIVE = false;
        }
        
        

        STATUS = xstr(result["status"]);
        EXPIRY = result["expiry_time"];
    }
    else {
        if (log_flag)
            std::cout << "response: Invalid JSON! \n Result:" << result.dump(4) << std::endl;

        ACTIVE = false;
    }

    nlohmann::json ret;
    if(response) 
        ret= nlohmann::json::parse("{ \"response\" : true, \"status\" : \"" + STATUS + "\", \"expiry_time\" : \"" + std::to_string(EXPIRY) + " days remaining\"}");
    else
        ret = nlohmann::json::parse("{ \"response\" : false, \"status\" : \"" + STATUS + "\", \"expiry_time\" : \"" + std::to_string(EXPIRY) + " days remaining\"}");

    if (log_flag)
        std::cout << ret.dump(4) << std::endl;

    return ret;
}
nlohmann::json License_Handler::add_device( std::string device_id, std::string device_name) {
    try {
        std::string post_body = "{\"id\":\"" + ID + "\", \"device_record\" : {\"device_id\": \"" + device_id + "\", \"device_name\" : \"" + device_name + "\"}}";
        nlohmann::json result = make_request(DEVICE_URL, "POST", "", post_body, API_KEY, log_flag);
        std::string response_message = "", response = "false";
        bool response_flag = false;

        if (result.contains("action") && result.contains("message")) {
            if (result["action"] == true) {
                response_flag = true;
                response_message = "\"License Activated!\"";
                ACTIVE = true;
                //return nlohmann::json::parse("{ \"response\" : true, \"status\" : \"License Activated!\"");

            }
            else {
                if (log_flag)
                    std::cout << "Error: " << xstr(result["message"]) << std::endl;
                response_message = "\"" + xstr(result["message"]) + "\"";
                ACTIVE = false;
            }
        }
        else {
            //std::cout << "response error: Invalid Request! \n Result:" << result.dump(4) << std::endl;
            response_message = xstr(result.dump());

            if (log_flag)
                std::cout << "response error:" << response_message << std::endl;
            ACTIVE = false;
        }
        if (response_flag) {
            response = "true";
        }
        nlohmann::json out = nlohmann::json::parse("{ \"response\" : " + response + ", \"message\" : " + response_message + "}");
        //std::cout << "Out:" << out.dump(4) << std::endl;
        return out;
    }
    catch (const std::exception& ex) {
        if (log_flag)
            std::cout << "response failed:" << ex.what() << std::endl;
        return nlohmann::json::parse("{}");
    }
}




licenseResponse LicenseManager::checkLicense(std::string license_id, std::string device_id, std::string device_name, int enable_logger) {

    bool log = false, logFlag = false;

    if (enable_logger <= 0) {
        log = false;
        logFlag = false;
    }
    else if (enable_logger == 1) {
        log = true;
    }
    else {
        log = true;
        logFlag = true;
    }

    License_Handler licenser = License_Handler(license_id,logFlag);

    licenseResponse output;
    output.action = false;
    output.response = "License validation failed!";
    output.data = nlohmann::json::parse("{}");
    if(log)
        std::cout << "Checking License..." << std::endl;

    if (licenser.check_license()) {
        if (log)
            std::cout << "License is valid. Adding device..." << std::endl;
        
        nlohmann::json res = licenser.validate_device(device_id);
        if (res.contains("response") && res.contains("status")) {
            if ((res["response"]) && (res["status"] == "ACTIVE")) {
                if (log)
                    std::cout << "Device Already Registered!" << std::endl;

                output.action = true;
                output.response = "Device Already Registered!";
                output.data = res;
            }
            else if (res["status"] == "ACTIVE") {
                if (log)
                    std::cout << "Device can be Registered..." << std::endl;

                nlohmann::json res2 = licenser.add_device( device_id, device_name);
                if (res2.contains("response") && res2.contains("message")) {
                    if (res2["response"] == true) {
                        if (log)
                            std::cout << xstr(res2["message"]) << std::endl;

                        output.action = true;
                        output.response = xstr(res2["message"]);
                        output.data = res2;
                    }
                    else {
                        if (log)
                            std::cout << xstr(res2["message"]) << std::endl;
                        output.response = xstr(res2["message"]);
                    }
                }
                else {
                    if (log)
                        std::cout << "License - add device : invalid process!" << res2.dump(4) << std::endl;
                    output.response = "License - add device : invalid process!";
                }
            }
            else {
                if (log)
                    std::cout << "License Expired!" << std::endl;
                output.response = "License Expired!";
            }
        }
        else {
            if (log)
                std::cout << "License validation error!" << std::endl;
            output.response = "License validation error!";
        }
    }
    else {
        if (log)
            std::cout << "License expired!" << std::endl;
        output.response = "License expired!";
    }
    return output;
}