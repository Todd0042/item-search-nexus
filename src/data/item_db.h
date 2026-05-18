#pragma once
#include "inventory_item.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

using json = nlohmann::json;

class ItemDb
{
public:
    static ItemDb& Instance();

    void Initialize(const std::string& cacheDir);
    void UpdateFromApiParallel();

    StaticItemInfo* GetItemInfo(int id);
    bool HasItem(int id) const;
    int Count() const;

    std::vector<int> SearchByName(const std::string& query);
    std::vector<int> Browse(const SearchOptions& options) const;

    void BatchEnsureItems(const std::vector<int>& ids);

    const std::vector<ExternalLink>& GetExternalLinks() const { return m_externalLinks; }
    void LoadExternalLinks(const std::string& path);
    void FetchStatChoices();

    const char* GetStatChoiceName(int id) const;
    std::vector<int> GetStatIdsByName(const std::string& name) const;
    const std::unordered_map<int, std::string>& GetAllStatChoices() const { return m_statChoices; }
    const std::vector<std::pair<int, std::string>>& GetUniqueStatChoices() const { return m_uniqueStatChoices; }

    static const char* ItemTypeToDisplayName(ItemType type);
    static const std::vector<ItemSubType>& GetSubTypesForType(ItemType type);
    static const char* ItemSubTypeToDisplayName(ItemSubType subType);

    const std::string& GetAssetDirectory() const { return m_assetDir; }

    float DbProgress() const { return m_dbProgress; }
    bool IsLoadingDB() const { return m_dbProgress > 0.0f && m_dbProgress < 1.0f; }

private:
    ItemDb() = default;

    StaticItemInfo ParseItemJson(const json& item);
    static ItemSubType ParseItemSubType(ItemType type, const std::string& detailType);
    void SaveToDisk();
    void SaveStatChoicesToDisk();

    std::unordered_map<int, StaticItemInfo> m_items;
    std::vector<ExternalLink> m_externalLinks;
    std::unordered_map<int, std::string> m_statChoices;
    std::unordered_map<std::string, std::vector<int>> m_statNameToIds;
    std::vector<std::pair<int, std::string>> m_uniqueStatChoices;
    std::string m_cacheDir;
    std::string m_assetDir;
    mutable std::mutex m_mutex;
    std::atomic<float> m_dbProgress{ 0.0f };
};
