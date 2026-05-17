#include "icon_cache.h"
#include "api/http_client.h"
#include "addon.h"
#include <fstream>

IconCache& IconCache::Instance()
{
    static IconCache instance;
    return instance;
}

void IconCache::Initialize(const std::string& cacheDir)
{
    m_iconDir = cacheDir + "\\icons";
    CreateDirectoryA(m_iconDir.c_str(), NULL);

    m_downloadRunning = true;
    m_downloadThread = std::thread([this]() { DownloadLoop(); });
}

void IconCache::Shutdown()
{
    m_downloadRunning = false;
    if (m_downloadThread.joinable())
        m_downloadThread.join();
}

std::string IconCache::BuildIconPath(int itemId) const
{
    return m_iconDir + "\\item_" + std::to_string(itemId) + ".png";
}

std::string IconCache::BuildIconKey(int itemId) const
{
    return "IS_ITEM_" + std::to_string(itemId);
}

Texture_t* IconCache::GetIcon(int itemId, const std::string& iconUrl)
{
    std::string key = BuildIconKey(itemId);

    Texture_t* tex = APIDefs->Textures_Get(key.c_str());
    if (tex) return tex;

    std::string filePath = BuildIconPath(itemId);
    std::ifstream file(filePath);
    if (file.good())
    {
        file.close();
        return APIDefs->Textures_GetOrCreateFromFile(key.c_str(), filePath.c_str());
    }

    PreloadIcon(itemId, iconUrl);
    return nullptr;
}

void IconCache::PreloadIcon(int itemId, const std::string& iconUrl)
{
    if (iconUrl.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pendingDownloads.find(itemId) == m_pendingDownloads.end())
    {
        m_pendingDownloads[itemId] = iconUrl;
    }
}

void IconCache::ProcessPendingDownloads(int maxPerFrame)
{
    int processed = 0;
    while (processed < maxPerFrame)
    {
        int itemId;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_readyForTexture.empty()) break;
            itemId = m_readyForTexture.front();
            m_readyForTexture.pop();
        }

        std::string filePath = BuildIconPath(itemId);
        std::string key = BuildIconKey(itemId);
        APIDefs->Textures_GetOrCreateFromFile(key.c_str(), filePath.c_str());

        processed++;
    }
}

void IconCache::DownloadLoop()
{
    while (m_downloadRunning)
    {
        int itemId;
        std::string url;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_pendingDownloads.empty())
            {
                auto it = m_pendingDownloads.begin();
                itemId = it->first;
                url = it->second;
                m_pendingDownloads.erase(it);
            }
        }

        if (url.empty())
        {
            Sleep(100);
            continue;
        }

        std::string filePath = BuildIconPath(itemId);
        HttpClient::Instance().DownloadFile(url, filePath);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_readyForTexture.push(itemId);
        }
    }
}
