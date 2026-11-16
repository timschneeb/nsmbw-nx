#include "test_http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

TestServer::~TestServer() {
    Stop();
}

bool TestServer::Start() {
    m_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0) {
        return false;
    }
    int one = 1;
    ::setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(m_socket, 16) != 0) {
        ::close(m_socket);
        m_socket = -1;
        return false;
    }

    socklen_t length = sizeof(address);
    if (::getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        ::close(m_socket);
        m_socket = -1;
        return false;
    }
    m_port = ntohs(address.sin_port);
    m_running = true;
    m_thread = std::thread([this] { Loop(); });
    return true;
}

void TestServer::Stop() {
    if (!m_running.exchange(false)) {
        return;
    }
    if (m_socket >= 0) {
        ::shutdown(m_socket, SHUT_RDWR);
        ::close(m_socket);
        m_socket = -1;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

auto TestServer::Url(const std::string& path) const -> std::string {
    return "http://127.0.0.1:" + std::to_string(m_port) + path;
}

void TestServer::Route(const std::string& path, TestResponse response) {
    Route(path, [response](const TestRequest&) { return response; });
}

void TestServer::Route(const std::string& path, Handler handler) {
    std::lock_guard lock(m_mutex);
    m_routes[path] = std::move(handler);
}

auto TestServer::Resolve(const TestRequest& request) -> TestResponse {
    Handler handler;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_routes.find(request.path);
        if (it == m_routes.end()) {
            TestResponse missing;
            missing.status = 404;
            missing.body = "not found";
            return missing;
        }
        handler = it->second;
    }
    return handler(request);
}

void TestServer::Loop() {
    while (m_running) {
        const int client = ::accept(m_socket, nullptr, nullptr);
        if (client < 0) {
            break;
        }
        Handle(client);
        ::close(client);
    }
}

void TestServer::Handle(int client) {
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 64 * 1024) {
        const ssize_t read = ::recv(client, buffer, sizeof(buffer), 0);
        if (read <= 0) {
            return;
        }
        request.append(buffer, static_cast<size_t>(read));
    }

    TestRequest parsed;
    const auto first_space = request.find(' ');
    const auto second_space = first_space == std::string::npos ? std::string::npos
                                                               : request.find(' ', first_space + 1);
    if (first_space != std::string::npos && second_space != std::string::npos) {
        parsed.method = request.substr(0, first_space);
        parsed.path = request.substr(first_space + 1, second_space - first_space - 1);
    }
    const auto range_pos = request.find("Range: bytes=");
    if (range_pos != std::string::npos) {
        const auto range_end = request.find("\r\n", range_pos);
        parsed.range = request.substr(range_pos + 13, range_end - (range_pos + 13));
    }

    TestResponse response = Resolve(parsed);
    if (!parsed.range.empty() && !response.ignore_range) {
        long long start = -1;
        long long end = -1;
        if (std::sscanf(parsed.range.c_str(), "%lld-%lld", &start, &end) == 2 &&
            start >= 0 && static_cast<size_t>(start) < response.body.size()) {
            const long long total = static_cast<long long>(response.body.size());
            if (end < start || end >= total) {
                end = total - 1;
            }
            response.body = response.body.substr(static_cast<size_t>(start),
                                                 static_cast<size_t>(end - start + 1));
            response.status = 206;
            response.content_range = "bytes " + std::to_string(start) + "-" +
                                     std::to_string(end) + "/" + std::to_string(total);
        } else {
            response.status = 416;
            response.body.clear();
        }
    }
    const char* reason = response.status == 200 ? "OK"
                       : response.status == 206 ? "Partial Content"
                       : response.status == 302 ? "Found"
                       : response.status == 404 ? "Not Found"
                       : response.status == 416 ? "Range Not Satisfiable"
                       : response.status == 500 ? "Internal Server Error"
                                                : "Status";
    std::string headers = "HTTP/1.1 " + std::to_string(response.status) + " " + reason + "\r\n";
    headers += "Content-Type: " + response.content_type + "\r\n";
    headers += "Connection: close\r\n";
    if (!response.location.empty()) {
        headers += "Location: " + response.location + "\r\n";
    }
    if (!response.content_range.empty()) {
        headers += "Content-Range: " + response.content_range + "\r\n";
    }
    if (response.chunked) {
        headers += "Transfer-Encoding: chunked\r\n\r\n";
    } else {
        headers += "Content-Length: " + std::to_string(response.body.size()) + "\r\n\r\n";
    }
    ::send(client, headers.data(), headers.size(), 0);

    if (response.body.empty()) {
        return;
    }

    const size_t declared = response.body.size();
    const size_t limit = response.truncate ? declared / 2 : declared;
    if (response.chunked) {
        for (size_t offset = 0; offset < limit; offset += response.chunk_size) {
            const size_t count = std::min(response.chunk_size, limit - offset);
            const std::string size = [&] {
                char hex[32];
                std::snprintf(hex, sizeof(hex), "%zx\r\n", count);
                return std::string(hex);
            }();
            ::send(client, size.data(), size.size(), 0);
            ::send(client, response.body.data() + offset, count, 0);
            ::send(client, "\r\n", 2, 0);
            if (response.delay_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(response.delay_ms));
            }
        }
        ::send(client, "0\r\n\r\n", 5, 0);
        return;
    }

    for (size_t offset = 0; offset < limit; offset += response.chunk_size) {
        const size_t count = std::min(response.chunk_size, limit - offset);
        if (::send(client, response.body.data() + offset, count, 0) < 0) {
            break;
        }
        if (response.delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(response.delay_ms));
        }
    }
}
