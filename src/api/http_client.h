#pragma once
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>

struct HttpResult
{
    long StatusCode = 0;
    std::string Body;
    bool Success = false;
};

class HttpClient
{
public:
    static HttpClient& Instance();

    HttpResult Get(const std::string& url, const std::string& authorization = "");
    HttpResult GetJson(const std::string& endpoint, const std::string& apiKey = "");
    bool DownloadFile(const std::string& url, const std::string& filePath);

    void SetApiKey(const std::string& key) { m_apiKey = key; }
    const std::string& GetApiKey() const { return m_apiKey; }

private:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    std::string m_apiKey;
    std::string m_baseUrl = "https://api.guildwars2.com";
    std::string m_renderBaseUrl = "https://render.guildwars2.com";
};
