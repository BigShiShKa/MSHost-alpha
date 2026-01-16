#include "httpServer.h"
#include <io.h>
#include <algorithm>
#include <fstream>
#include <windows.h>
#include <deque>
#include <codecvt>

#include "Logger.h"

using json = nlohmann::json;

std::string status_to_string(ServerStatus status) {
    switch (status) {
        case ServerStatus::Stopped: return "Остановлен";
        case ServerStatus::Starting: return "Запускаю...";
        case ServerStatus::Running: return "Запущен";
        case ServerStatus::Stopping: return "Останавливаю...";
        default: return "хз, ЫсчЭз. наелся и спит";
    }
}

HttpServer::HttpServer(MinecraftServerManager& manager, int port, std::atomic<bool>& running, const std::string& tokens_file)
    : manager_(manager), port_(port), running_(running), tokens_file_(tokens_file) { load_tokens(); }

void HttpServer::load_tokens() {
    std::lock_guard lg(tokens_mx_);
    std::unordered_set<std::string> fresh;
    std::ifstream f(tokens_file_);
    if (!f) {
        LOG_ERR("Не смог открыть " + tokens_file_, "WEB");
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (!line.empty() && line[0] != '#') fresh.insert(line);
    }
    tokens_.swap(fresh);
    LOG_INFO("Токены перечитаны, всего: " + std::to_string(tokens_.size()), "WEB");
}

bool HttpServer::check_token(const std::string& t) {
    std::lock_guard lg(tokens_mx_);
    return tokens_.count(t) > 0;
}

json HttpServer::get_status_json() {
    ServerStatus status = manager_.get_status();

    return json{
        {"status", status_to_string(status)},
        {"ip", "91.223.70.49"},
        {"port", 25566}
    };
}
// ────────────────────────────────────────────────────────────────
//  sanitize_utf8  —   пропускает только корректные UTF‑8 байты
// ────────────────────────────────────────────────────────────────
static std::string sanitize_utf8(const std::string& in)
{
    std::string out;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(in.data());
    size_t i = 0, n = in.size();

    while (i < n)
    {
        unsigned char c = s[i];

        // ASCII
        if (c < 0x80) { out.push_back(c); ++i; continue; }

        // 2‑байт U+0080..07FF
        if ((c >> 5) == 0x6 && i+1 < n && (s[i+1] & 0xC0) == 0x80)
        { out.append(reinterpret_cast<const char*>(s+i), 2); i += 2; continue; }

        // 3‑байт U+0800..FFFF
        if ((c >> 4) == 0xE && i+2 < n &&
            (s[i+1] & 0xC0) == 0x80 && (s[i+2] & 0xC0) == 0x80)
        { out.append(reinterpret_cast<const char*>(s+i), 3); i += 3; continue; }

        // 4‑байт U+10000..10FFFF
        if ((c >> 3) == 0x1E && i+3 < n &&
            (s[i+1] & 0xC0) == 0x80 && (s[i+2] & 0xC0) == 0x80 &&
            (s[i+3] & 0xC0) == 0x80)
        { out.append(reinterpret_cast<const char*>(s+i), 4); i += 4; continue; }

        // иначе — битый байт, пропускаем
        ++i;
    }
    return out;
}


void HttpServer::run() {
    LOG_INFO("Инициализация маршрутов...", "WEB");
    LOG_INFO("Адрес памяти сервера: " + std::to_string(reinterpret_cast<uintptr_t>(&svr)), "WEB");

    // Middleware токена
    svr.set_pre_routing_handler([&](const auto& req, auto& res) {
        std::string client_ip = req.get_header_value("X-Real-IP");
        if (client_ip.empty()) client_ip = req.get_header_value("X-Forwarded-For");
        if (client_ip.empty()) client_ip = req.remote_addr;

        static std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_req;
        auto ip = client_ip;
        
        if (last_req.count(ip) && 
            std::chrono::steady_clock::now() - last_req[ip] < std::chrono::milliseconds(1000)) {
            res.status = 429;
            return httplib::Server::HandlerResponse::Handled;
        }

        static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;

        std::wstring wmethod = conv.from_bytes(req.method);
        std::wstring wpath   = conv.from_bytes(req.path);

        LOG_INFO("[" + client_ip + "] " + req.method + " " + req.path, "WEB");

        if (req.path.rfind("/api/", 0) == 0) {
            auto token = req.get_header_value("X-API-Token");
            if (!check_token(token)) {
                res.status = 401;
                res.set_content("Unauthorized", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }
        }

        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Эндпоинты API
    svr.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        json response = get_status_json();
        res.set_content(response.dump(), "application/json");
    });

    svr.Get("/api/logs", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string logPath = R"(D:\Programms\MSHost\server.log)";
        
        // Попробуем открыть файл несколько раз с задержкой
        std::ifstream fs;
        int attempts = 3;
        while (attempts-- > 0) {
            fs.open(logPath);
            if (fs.is_open()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!fs.is_open()) {
            LOG_ERR("Не удалось открыть logPath: " + logPath + ", errno=" + std::to_string(errno), "WEB");
            res.status = 404;
            res.set_content(R"({"error": "Лог-файл не найден или недоступен"})", "application/json");
            return;
        }

        try {
            // Оптимизированное чтение последних строк
            const size_t maxLines = 500;
            std::deque<std::string> lastLines;
            
            // Простой способ для небольших файлов
            std::string line;
            while (std::getline(fs, line)) {
                line = sanitize_utf8(line); 
                if (lastLines.size() >= maxLines) {
                    lastLines.pop_front();
                }
                lastLines.push_back(line);
            }

            // Формируем ответ
            std::string logs;
            for (const auto& l : lastLines) {
                logs += l + "\n";
            }

            json response = { {"logs", sanitize_utf8(logs)} };   // ← ещё раз на всяк случай
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            LOG_ERR("Ошибка при чтении логов: " + std::string(e.what()), "WEB");
            res.status = 500;
            res.set_content(R"({"error": "Ошибка при чтении лог-файла"})", "application/json");
        }
    });

    svr.Get("/api/download-modpack", [this](const httplib::Request& req, httplib::Response& res) {
        std::string modpackPath = "C:\\Games\\Arclight1.20.1\\modpack.zip"; // Путь к файлу сборки
        
        // Проверяем существование файла
        std::ifstream fs(modpackPath, std::ios::binary);
        if (!fs.is_open()) {
            res.status = 404;
            res.set_content("Файл сборки не найден", "text/plain");
            return;
        }

        // Получаем размер файла
        fs.seekg(0, std::ios::end);
        auto size = fs.tellg();
        fs.seekg(0, std::ios::beg);

        // Устанавливаем заголовки
        res.set_header("Content-Type", "application/zip");
        res.set_header("Content-Disposition", "attachment; filename=modpack.zip");
        res.set_header("Content-Length", std::to_string(size));
        
        // Читаем файл и отправляем
        std::string content((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
        res.set_content(content, "application/zip");
    });

    svr.Post("/api/start", [this](const httplib::Request&, httplib::Response& res) {
        std::wcout << L"Запрошен запуск" << std::endl;
        manager_.start();
        res.set_content(get_status_json().dump(), "application/json");
    });

    svr.Post("/api/stop", [this](const httplib::Request&, httplib::Response& res) {
        manager_.stop();
        res.set_content(get_status_json().dump(), "application/json");
    });

    svr.Post("/api/exit", [this](const httplib::Request&, httplib::Response& res) {
        std::wcout << L"Получен запрос на завершение работы через API" << std::endl;
        manager_.stop();
        this->stop();
        running_ = false;
        #ifdef _WIN32
            // 1) берём дескриптор консольного stdin
            HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
            // 2) отменяем все операции чтения (ReadConsole) в другом потоке
            CancelIoEx(hIn, nullptr);
            // 3) закрываем, чтобы дальнейшие чтения сразу давали EOF
            CloseHandle(hIn);
        #endif
        res.set_content(R"({
        "status": "shutdown",
        "message": "Сервер выключается. До скорой встречи!"
        })", "application/json");

    });

    svr.Post("/api/command", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            manager_.send_command(body["command"].get<std::string>());
            res.set_content(get_status_json().dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content(json{{"error", "invalid request"}}.dump(), "application/json");
        }
    });

    // Фронтенд
    svr.set_mount_point("/", "./www");
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/index.html");
    });

    LOG_INFO("HTTP сервер запущен на порту: " + std::to_string(port_), "WEB");
    try {
        svr.new_task_queue = [] { return new httplib::ThreadPool(4); };
        if (!svr.listen("0.0.0.0", port_)) {
            LOG_ERR("Не удалось запустить сервер!", "WEB");
        }
    } catch (const std::exception& ex) {
        LOG_ERR(std::string("Ошибка: ") + ex.what(), "WEB");
        //std::wcerr << L"[WEB] Ошибка: " << ex.what() << std::endl;
    }

}

void HttpServer::stop() {
    LOG_WARNING("Остановка WEB сервера...", "WEB");
    svr.stop();

#ifdef _WIN32
    // Форсируем разрыв select() через самоподключение
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock != INVALID_SOCKET) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(port_);
        connect(sock, (sockaddr*)&addr, sizeof(addr));
        closesocket(sock);
    }
#endif
    LOG_INFO("Сервер остановлен", "WEB");
}