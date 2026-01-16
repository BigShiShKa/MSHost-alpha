#pragma once

#include "MinecraftServerManager.h"
#include "httplib.h"
#include "json.hpp"
#include <iostream>
#include <atomic>

class HttpServer {
public:
    HttpServer(MinecraftServerManager& manager, int port, std::atomic<bool>& running, const std::string& tokens_file = "tokens");
    void run();
    void stop();
    void load_tokens();
private:
    std::atomic<bool>& running_;
    MinecraftServerManager& manager_;
    int port_;
    httplib::Server svr;
    nlohmann::json get_status_json();

    std::unordered_set<std::string> tokens_;
    std::string        tokens_file_;
    std::mutex         tokens_mx_;

    bool check_token(const std::string&);
};
