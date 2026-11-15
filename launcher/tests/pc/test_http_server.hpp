#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

struct TestRequest {
    std::string method;
    std::string path;
};

struct TestResponse {
    TestResponse() = default;
    TestResponse(int status_, std::string body_,
                 std::string content_type_ = "application/octet-stream", std::string location_ = {},
                 bool truncate_ = false, bool chunked_ = false, int delay_ms_ = 0,
                 size_t chunk_size_ = 4096)
        : status{status_}
        , body{std::move(body_)}
        , content_type{std::move(content_type_)}
        , location{std::move(location_)}
        , truncate{truncate_}
        , chunked{chunked_}
        , delay_ms{delay_ms_}
        , chunk_size{chunk_size_} {}

    int status{200};
    std::string body;
    std::string content_type{"application/octet-stream"};
    std::string location;
    bool truncate{};
    bool chunked{};
    int delay_ms{};
    size_t chunk_size{4096};
};

class TestServer {
public:
    using Handler = std::function<TestResponse(const TestRequest&)>;

    TestServer() = default;
    ~TestServer();

    bool Start();
    void Stop();

    auto Url(const std::string& path) const -> std::string;
    void Route(const std::string& path, TestResponse response);
    void Route(const std::string& path, Handler handler);

private:
    void Loop();
    void Handle(int client);
    auto Resolve(const TestRequest& request) -> TestResponse;

    int m_socket{-1};
    std::uint16_t m_port{};
    std::atomic<bool> m_running{};
    std::thread m_thread;
    std::mutex m_mutex;
    std::map<std::string, Handler> m_routes;
};
