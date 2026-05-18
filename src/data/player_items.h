#pragma once
#include "inventory_item.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <windows.h>

using json = nlohmann::json;

struct CachedCharItem
{
    int Id = 0;
    InventoryItemSource Source = InventoryItemSource::Unknown;
    int Count = 1;
    std::vector<int> Upgrades;
    std::vector<int> Infusions;
    int EquipmentTabId = -1;
    int StatChoiceId = 0;
    std::unordered_map<std::string, int> StatAttributes;
};

struct CachedCharacter
{
    int64_t Age = 0;
    std::vector<CachedCharItem> Items;
};

void to_json(json& j, const CachedCharItem& c);
void from_json(const json& j, CachedCharItem& c);

class PlayerItems
{
public:
    static PlayerItems& Instance();

    void Initialize(const std::string& apiKey, const std::string& cacheDir);
    void RefreshFromApi();
    bool IsReady() const { return m_ready; }
    bool IsOfflineReady() const { return m_offlineReady; }
    float Progress() const { return m_progress; }
    int TotalItemCount() const { return m_totalItemCount; }
    int TotalCharacters() const { return m_totalCharacters; }

    void Stop();

    const std::unordered_map<int, std::vector<InventoryItem>>& GetAllItems() const { return m_items; }

    std::vector<InventoryItem> FindMatches(const std::vector<int>& itemIds, bool excludeLegendaryArmory, bool excludeEquippedBags);

    void StartBackgroundPolling();
    void StopBackgroundPolling();

private:
    PlayerItems() = default;

    InventoryItem MakeItem(int id, InventoryItemSource source, const std::string& character = "");
    void AddItem(std::unordered_map<int, std::vector<InventoryItem>>& items, int id, InventoryItem item);

    void LoadCache();
    void SaveCache();
    void ParseCharacterFromApi(const std::string& name, std::unordered_map<int, std::vector<InventoryItem>>& allItems);
    void ParseCharacterFromCache(const CachedCharacter& cached, const std::string& name, std::unordered_map<int, std::vector<InventoryItem>>& allItems);
    void RefreshSingleCharacter(const std::string& name);
    void ReplaceCharacterItems(const std::string& name, std::unordered_map<int, std::vector<InventoryItem>>& charItems);
    void SaveCharacterCache(const std::string& name);

    void BackgroundLoop();
    void SaveFullCache();
    bool LoadFullCache();

    std::unordered_map<int, std::vector<InventoryItem>> m_items;
    std::unordered_map<std::string, int64_t> m_charAges;
    std::unordered_map<std::string, int64_t> m_cachedAges;
    std::string m_accountName;
    std::string m_cacheDir;
    std::mutex m_mutex;
    std::atomic<bool> m_ready{ false };
    std::atomic<bool> m_offlineReady{ false };
    std::atomic<float> m_progress{ 0.0f };
    std::atomic<int> m_totalItemCount{ 0 };
    std::atomic<int> m_totalCharacters{ 0 };

    std::thread m_backgroundThread;
    std::atomic<bool> m_backgroundRunning{ false };
    HANDLE m_stopEvent = NULL;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_dirtyCharacters;
    std::chrono::steady_clock::time_point m_lastMaterialsFetch;

    std::thread m_refreshThread;
    std::atomic<bool> m_refreshRunning{ false };
};
