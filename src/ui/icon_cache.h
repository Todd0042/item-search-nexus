#pragma once
#include <string>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>

struct Texture_t;

class IconCache
{
public:
    static IconCache& Instance();

    void Initialize(const std::string& cacheDir);
    Texture_t* GetIcon(int itemId, const std::string& iconUrl);
    void PreloadIcon(int itemId, const std::string& iconUrl);
    void ProcessPendingDownloads(int maxPerFrame = 0);

    void Shutdown();

private:
    IconCache() = default;
    ~IconCache() { Shutdown(); }

    std::string BuildIconPath(int itemId) const;
    std::string BuildIconKey(int itemId) const;
    void DownloadLoop();

    std::string m_iconDir;
    std::unordered_map<int, std::string> m_pendingDownloads;
    std::queue<int> m_readyForTexture;
    std::mutex m_mutex;

    std::thread m_downloadThread;
    std::atomic<bool> m_downloadRunning{ false };
};
