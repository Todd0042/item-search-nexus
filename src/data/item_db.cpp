#include "item_db.h"
#include "api/gw2_api.h"
#include "api/http_client.h"
#include "addon.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

ItemDb& ItemDb::Instance()
{
    static ItemDb instance;
    return instance;
}

void ItemDb::Initialize(const std::string& cacheDir)
{
    m_cacheDir = cacheDir;
    m_assetDir = cacheDir + "\\assets";

    std::string itemsPath = cacheDir + "\\all_items.json";
    std::ifstream file(itemsPath);
    if (file.is_open())
    {
        try
        {
            json data = json::parse(file);
            for (auto& [key, val] : data.items())
            {
                int id = std::stoi(key);
                StaticItemInfo info;
                info.Name = val["Name"].get<std::string>();
                info.IconUrl = val.value("IconUrl", "");
                info.ChatCode = val.value("ChatCode", "");
                info.Rarity = static_cast<ItemRarity>(val.value("Rarity", 0));
                info.Type = static_cast<ItemType>(val.value("Type", 0));
                info.SubType = static_cast<ItemSubType>(val.value("SubType", 0));
                info.StaticStatChoiceId = val.value("StatChoiceId", 0);
                if (val.contains("StatAttributes"))
                {
                    for (auto& [k, v] : val["StatAttributes"].items())
                        info.StaticStatAttributes[k] = v.get<int>();
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                m_items[id] = info;
            }
            LogInfo(("Loaded " + std::to_string(m_items.size()) + " items from cache").c_str());
        }
        catch (const std::exception& e)
        {
            LogWarn(("Failed to parse items cache: " + std::string(e.what())).c_str());
        }
    }
    else
    {
        LogInfo("No items cache found, will fetch from API");
    }

    // Load stat choices from disk cache
    std::string statPath = cacheDir + "\\statchoices.json";
    std::ifstream statFile(statPath);
    if (statFile.is_open())
    {
        try
        {
            json data = json::parse(statFile);
            std::unordered_set<std::string> seen;
            for (auto& choice : data)
            {
                int id = choice["id"].get<int>();
                std::string name = choice["name"].get<std::string>();
                m_statChoices[id] = name;
                m_statNameToIds[name].push_back(id);
                if (seen.insert(name).second)
                    m_uniqueStatChoices.push_back({id, name});
            }
            std::sort(m_uniqueStatChoices.begin(), m_uniqueStatChoices.end(),
                [](auto& a, auto& b) { return a.second < b.second; });
        }
        catch (...) {}
    }
}

StaticItemInfo ItemDb::ParseItemJson(const json& item)
{
    StaticItemInfo info;
    info.Name = item.value("name", "");
    info.ChatCode = item.value("chat_link", "");

    if (item.contains("icon"))
    {
        info.IconUrl = item["icon"].get<std::string>();
    }

    std::string rarity = item.value("rarity", "");
    if (rarity == "Junk") info.Rarity = ItemRarity::Junk;
    else if (rarity == "Basic") info.Rarity = ItemRarity::Basic;
    else if (rarity == "Fine") info.Rarity = ItemRarity::Fine;
    else if (rarity == "Masterwork") info.Rarity = ItemRarity::Masterwork;
    else if (rarity == "Rare") info.Rarity = ItemRarity::Rare;
    else if (rarity == "Exotic") info.Rarity = ItemRarity::Exotic;
    else if (rarity == "Ascended") info.Rarity = ItemRarity::Ascended;
    else if (rarity == "Legendary") info.Rarity = ItemRarity::Legendary;

    if (item.contains("type"))
    {
        std::string type = item["type"].get<std::string>();
        if (type == "Armor") info.Type = ItemType::Armor;
        else if (type == "Back") info.Type = ItemType::Back;
        else if (type == "Bag") info.Type = ItemType::Bag;
        else if (type == "Consumable") info.Type = ItemType::Consumable;
        else if (type == "Container") info.Type = ItemType::Container;
        else if (type == "CraftingMaterial") info.Type = ItemType::CraftingMaterial;
        else if (type == "Gathering") info.Type = ItemType::Gathering;
        else if (type == "Gizmo") info.Type = ItemType::Gizmo;
        else if (type == "Key") info.Type = ItemType::Key;
        else if (type == "MiniPet") info.Type = ItemType::MiniPet;
        else if (type == "Tool") info.Type = ItemType::Tool;
        else if (type == "Trait") info.Type = ItemType::Trait;
        else if (type == "Trinket") info.Type = ItemType::Trinket;
        else if (type == "Trophy") info.Type = ItemType::Trophy;
        else if (type == "UpgradeComponent") info.Type = ItemType::UpgradeComponent;
        else if (type == "Weapon") info.Type = ItemType::Weapon;
        else if (type == "PowerCore") info.Type = ItemType::PowerCore;
        else if (type == "JadeTechModule") info.Type = ItemType::JadeTechModule;
        else if (type == "Relic") info.Type = ItemType::Relic;
    }

    // Parse subtype from details.type
    if (item.contains("details") && item["details"].contains("type"))
    {
        std::string detailType = item["details"]["type"].get<std::string>();
        info.SubType = ParseItemSubType(info.Type, detailType);
    }

    // Parse built-in stats for fixed-stat items (details.infix_upgrade)
    if (item.contains("details") && item["details"].contains("infix_upgrade"))
    {
        auto& infix = item["details"]["infix_upgrade"];
        info.StaticStatChoiceId = infix.value("id", 0);
        if (infix.contains("attributes") && infix["attributes"].is_array())
        {
            for (auto& attr : infix["attributes"])
            {
                if (attr.contains("attribute") && attr.contains("modifier"))
                    info.StaticStatAttributes[attr["attribute"].get<std::string>()] = attr["modifier"].get<int>();
            }
        }
    }

    return info;
}

void ItemDb::SaveToDisk()
{
    std::string itemsPath = m_cacheDir + "\\all_items.json";
    json output = json::object();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, info] : m_items)
        {
            json j;
            j["Name"] = info.Name;
            j["IconUrl"] = info.IconUrl;
            j["ChatCode"] = info.ChatCode;
            j["Rarity"] = static_cast<int>(info.Rarity);
            j["Type"] = static_cast<int>(info.Type);
            j["SubType"] = static_cast<int>(info.SubType);
            j["StatChoiceId"] = info.StaticStatChoiceId;
            if (!info.StaticStatAttributes.empty())
            {
                json attrs = json::object();
                for (auto& [k, v] : info.StaticStatAttributes)
                    attrs[k] = v;
                j["StatAttributes"] = attrs;
            }
            output[std::to_string(id)] = j;
        }
    }

    std::ofstream file(itemsPath);
    if (file.is_open())
        file << output.dump(2);
}

void ItemDb::SaveStatChoicesToDisk()
{
    if (m_cacheDir.empty()) return;
    std::string path = m_cacheDir + "\\statchoices.json";
    json arr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, name] : m_statChoices)
        {
            json j;
            j["id"] = id;
            j["name"] = name;
            arr.push_back(j);
        }
    }
    std::ofstream file(path);
    if (file.is_open())
        file << arr.dump(2);
}

void ItemDb::UpdateFromApi()
{
    LogInfo("Updating item database from API");

    FetchStatChoices();

    auto& api = Gw2Api::Instance();
    auto allIds = api.GetAllItemIds();

    if (allIds.empty())
    {
        LogWarn("Failed to get item IDs from API");
        return;
    }

    int newItems = 0;
    std::vector<int> idsToFetch;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int id : allIds)
        {
            if (m_items.find(id) == m_items.end())
                idsToFetch.push_back(id);
        }
    }

    LogInfo(("Need to fetch " + std::to_string(idsToFetch.size()) + " new items").c_str());

    const int batchSize = 200;
    size_t totalBatches = (idsToFetch.size() + batchSize - 1) / batchSize;
    for (size_t i = 0; i < idsToFetch.size(); i += batchSize)
    {
        size_t end = std::min(i + batchSize, idsToFetch.size());
        std::vector<int> batch(idsToFetch.begin() + i, idsToFetch.begin() + end);

        json items = api.GetItems(batch);
        if (items.is_null() || !items.is_array())
        {
            LogWarn("Failed to fetch item batch");
            Sleep(50);
            continue;
        }

        for (auto& item : items)
        {
            int id = item["id"].get<int>();
            StaticItemInfo info = ParseItemJson(item);

            std::lock_guard<std::mutex> lock(m_mutex);
            m_items[id] = info;
            newItems++;
        }

        size_t batchNum = i / batchSize + 1;
        if (batchNum % 50 == 0 || batchNum == totalBatches)
        {
            LogInfo(("Item DB progress: " + std::to_string(batchNum) + "/" + std::to_string(totalBatches) + " batches (" + std::to_string(newItems) + " items)").c_str());
        }

        Sleep(50);
    }

    LogInfo(("Added " + std::to_string(newItems) + " new items to database").c_str());
    SaveToDisk();
}

void ItemDb::UpdateFromApiParallel()
{
    LogInfo("Updating item database from API (parallel)");

    FetchStatChoices();

    auto& api = Gw2Api::Instance();
    auto allIds = api.GetAllItemIds();

    if (allIds.empty())
    {
        LogWarn("Failed to get item IDs from API");
        return;
    }

    std::vector<int> idsToFetch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int id : allIds)
        {
            if (m_items.find(id) == m_items.end())
                idsToFetch.push_back(id);
        }
    }

    LogInfo(("Need to fetch " + std::to_string(idsToFetch.size()) + " new items (parallel)").c_str());

    const int batchSize = 200;
    size_t totalBatches = (idsToFetch.size() + batchSize - 1) / batchSize;
    const int numWorkers = 6;
    std::atomic<int> nextBatch{ 0 };
    std::atomic<int> totalFetched{ 0 };

    auto workerFn = [&]()
    {
        // Each worker gets its own WinHTTP session for true concurrent requests
        HINTERNET hSession = WinHttpOpen(L"Gw2ItemSearch/1.0.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);

        while (true)
        {
            int batchIdx = nextBatch.fetch_add(1);
            if (batchIdx >= (int)totalBatches) break;

            size_t start = batchIdx * batchSize;
            size_t end = std::min(start + batchSize, idsToFetch.size());
            std::vector<int> batch(idsToFetch.begin() + start, idsToFetch.begin() + end);

            // Build URL
            std::string idsStr;
            for (size_t i = 0; i < batch.size(); i++)
            {
                if (i > 0) idsStr += ",";
                idsStr += std::to_string(batch[i]);
            }
            std::string url = "/v2/items?ids=" + idsStr;

            // Direct WinHTTP call (no HttpClient singleton)
            std::wstring wurl(128 + url.size(), L'\0');
            int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], (int)wurl.size());
            wurl.resize(wlen - 1);

            URL_COMPONENTS urlComp = { 0 };
            urlComp.dwStructSize = sizeof(urlComp);
            urlComp.lpszHostName = (LPWSTR)L"api.guildwars2.com";
            urlComp.dwHostNameLength = (DWORD)wcslen(L"api.guildwars2.com");
            urlComp.lpszUrlPath = &wurl[0];
            urlComp.dwUrlPathLength = (DWORD)wurl.size();

            wchar_t hostBuf[128];
            wchar_t pathBuf[4096];
            urlComp.lpszHostName = hostBuf;
            urlComp.dwHostNameLength = 128;
            urlComp.lpszUrlPath = pathBuf;
            urlComp.dwUrlPathLength = 4096;

            if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.length(), 0, &urlComp))
                continue;

            HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
            if (!hConnect) continue;

            DWORD flags = WINHTTP_FLAG_REFRESH;
            if (urlComp.nScheme == INTERNET_SCHEME_HTTPS)
                flags |= WINHTTP_FLAG_SECURE;

            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath, NULL, NULL, NULL, flags);
            if (!hRequest)
            {
                WinHttpCloseHandle(hConnect);
                continue;
            }

            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            {
                if (WinHttpReceiveResponse(hRequest, NULL))
                {
                    std::string body;
                    DWORD bytesAvailable = 0;
                    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
                    {
                        std::vector<char> buffer(bytesAvailable);
                        DWORD bytesRead = 0;
                        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead))
                            body.append(buffer.data(), bytesRead);
                    }

                    try
                    {
                        json items = json::parse(body);
                        if (items.is_array())
                        {
                            for (auto& item : items)
                            {
                                int id = item["id"].get<int>();
                                StaticItemInfo info = ParseItemJson(item);
                                std::lock_guard<std::mutex> lock(m_mutex);
                                m_items[id] = info;
                                totalFetched++;
                            }
                        }
                    }
                    catch (...) {}
                }
            }

            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
        }

        WinHttpCloseHandle(hSession);
    };

    std::vector<std::thread> workers;
    for (int w = 0; w < numWorkers; w++)
        workers.emplace_back(workerFn);

    for (auto& w : workers)
        w.join();

    LogInfo(("Parallel fetch complete: " + std::to_string(totalFetched.load()) + " new items").c_str());
    SaveToDisk();
}

StaticItemInfo* ItemDb::GetItemInfo(int id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_items.find(id);
    if (it != m_items.end())
        return &it->second;
    return nullptr;
}

bool ItemDb::HasItem(int id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_items.find(id) != m_items.end();
}

int ItemDb::Count() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return (int)m_items.size();
}

void ItemDb::BatchEnsureItems(const std::vector<int>& ids)
{
    if (ids.empty()) return;

    std::vector<int> toFetch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int id : ids)
        {
            if (m_items.find(id) == m_items.end())
                toFetch.push_back(id);
        }
    }

    if (toFetch.empty()) return;

    LogInfo(("BatchEnsureItems: fetching " + std::to_string(toFetch.size()) + " uncached items").c_str());

    const int batchSize = 200;
    for (size_t i = 0; i < toFetch.size(); i += batchSize)
    {
        size_t end = std::min(i + batchSize, toFetch.size());
        std::vector<int> batch(toFetch.begin() + i, toFetch.begin() + end);
        json items = Gw2Api::Instance().GetItems(batch);
        if (items.is_null() || !items.is_array()) continue;

        for (auto& item : items)
        {
            int id = item["id"].get<int>();
            StaticItemInfo info = ParseItemJson(item);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_items[id] = info;
        }
    }

    LogInfo("BatchEnsureItems complete");
}

std::vector<int> ItemDb::SearchByName(const std::string& query)
{
    // On-demand mode: no local database, search via API
    if (m_fetchMode == FetchMode::OnDemand && m_items.empty())
    {
        LogInfo(("SearchByName (on-demand): '" + query + "'").c_str());

        json searchResult = Gw2Api::Instance().SearchItems(query);
        if (searchResult.is_null() || !searchResult.is_array())
        {
            LogWarn("SearchByName on-demand failed");
            return {};
        }

        std::vector<int> ids = searchResult.get<std::vector<int>>();
        LogInfo(("SearchByName on-demand found " + std::to_string(ids.size()) + " matching IDs").c_str());

        if (!ids.empty())
        {
            BatchEnsureItems(ids);
        }

        return ids;
    }

    // Normal mode: search local database
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    std::vector<int> results;
    std::lock_guard<std::mutex> lock(m_mutex);
    LogDebug(("SearchByName: '" + query + "' across " + std::to_string(m_items.size()) + " items").c_str());
    for (auto& [id, info] : m_items)
    {
        std::string lowerName = info.Name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName.find(lowerQuery) != std::string::npos)
        {
            results.push_back(id);
        }
    }
    LogDebug(("SearchByName: '" + query + "' found " + std::to_string(results.size()) + " matches").c_str());
    if (!results.empty())
    {
        auto mapIt = m_items.find(results[0]);
        if (mapIt != m_items.end())
            LogDebug(("  first match: [" + std::to_string(results[0]) + "] " + mapIt->second.Name).c_str());
    }
    return results;
}

std::vector<int> ItemDb::Browse(const SearchOptions& options) const
{
    std::vector<int> results;
    if (!options.HasActiveFilter())
        return results;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, info] : m_items)
    {
        if (options.FilterItem(&info))
        {
            results.push_back(id);
        }
    }
    return results;
}

void ItemDb::FetchStatChoices()
{
    auto& api = Gw2Api::Instance();
    json choices = api.GetJsonPublic("/v2/itemstats?ids=all");
    if (choices.is_null() || !choices.is_array())
    {
        LogWarn("Failed to fetch stat choices from API");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_statChoices.clear();
        m_statNameToIds.clear();
        m_uniqueStatChoices.clear();
        std::unordered_set<std::string> seen;
        for (auto& choice : choices)
        {
            int id = choice["id"].get<int>();
            std::string name = choice["name"].get<std::string>();
            m_statChoices[id] = name;
            m_statNameToIds[name].push_back(id);
            if (seen.insert(name).second)
                m_uniqueStatChoices.push_back({id, name});
        }
        std::sort(m_uniqueStatChoices.begin(), m_uniqueStatChoices.end(),
            [](auto& a, auto& b) { return a.second < b.second; });

        LogInfo(("Fetched " + std::to_string(m_statChoices.size()) + " stat choices (" + std::to_string(m_uniqueStatChoices.size()) + " unique) from API").c_str());
    }

    SaveStatChoicesToDisk();
}

const char* ItemDb::GetStatChoiceName(int id) const
{
    auto it = m_statChoices.find(id);
    if (it != m_statChoices.end())
        return it->second.c_str();
    return nullptr;
}

std::vector<int> ItemDb::GetStatIdsByName(const std::string& name) const
{
    auto it = m_statNameToIds.find(name);
    if (it != m_statNameToIds.end())
        return it->second;
    return {};
}

void ItemDb::LoadExternalLinks(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return;

    try
    {
        json data = json::parse(file);
        m_externalLinks.clear();
        for (auto& link : data)
        {
            ExternalLink el;
            el.Name = link["Name"].get<std::string>();
            el.Url = link["Url"].get<std::string>();
            m_externalLinks.push_back(el);
        }
    }
    catch (...) {}
}

ItemSubType ItemDb::ParseItemSubType(ItemType type, const std::string& detailType)
{
    switch (type)
    {
    case ItemType::Armor:
        if (detailType == "Helm") return ItemSubType::Armor_Helm;
        if (detailType == "HelmAquatic") return ItemSubType::Armor_HelmAquatic;
        if (detailType == "Shoulders") return ItemSubType::Armor_Shoulders;
        if (detailType == "Coat") return ItemSubType::Armor_Coat;
        if (detailType == "Gloves") return ItemSubType::Armor_Gloves;
        if (detailType == "Leggings") return ItemSubType::Armor_Leggings;
        if (detailType == "Boots") return ItemSubType::Armor_Boots;
        break;
    case ItemType::Weapon:
        if (detailType == "Axe") return ItemSubType::Weapon_Axe;
        if (detailType == "Dagger") return ItemSubType::Weapon_Dagger;
        if (detailType == "Mace") return ItemSubType::Weapon_Mace;
        if (detailType == "Pistol") return ItemSubType::Weapon_Pistol;
        if (detailType == "Scepter") return ItemSubType::Weapon_Scepter;
        if (detailType == "Sword") return ItemSubType::Weapon_Sword;
        if (detailType == "Focus") return ItemSubType::Weapon_Focus;
        if (detailType == "Shield") return ItemSubType::Weapon_Shield;
        if (detailType == "Torch") return ItemSubType::Weapon_Torch;
        if (detailType == "Warhorn") return ItemSubType::Weapon_Warhorn;
        if (detailType == "Greatsword") return ItemSubType::Weapon_Greatsword;
        if (detailType == "Hammer") return ItemSubType::Weapon_Hammer;
        if (detailType == "LongBow") return ItemSubType::Weapon_LongBow;
        if (detailType == "Rifle") return ItemSubType::Weapon_Rifle;
        if (detailType == "ShortBow") return ItemSubType::Weapon_ShortBow;
        if (detailType == "Staff") return ItemSubType::Weapon_Staff;
        if (detailType == "Trident") return ItemSubType::Weapon_Trident;
        if (detailType == "Harpoon") return ItemSubType::Weapon_Harpoon;
        if (detailType == "Speargun") return ItemSubType::Weapon_Speargun;
        if (detailType == "LargeBundle") return ItemSubType::Weapon_LargeBundle;
        if (detailType == "SmallBundle") return ItemSubType::Weapon_SmallBundle;
        if (detailType == "Toy") return ItemSubType::Weapon_Toy;
        if (detailType == "ToyTwoHanded") return ItemSubType::Weapon_ToyTwoHanded;
        break;
    case ItemType::Trinket:
        if (detailType == "Accessory") return ItemSubType::Trinket_Accessory;
        if (detailType == "Amulet") return ItemSubType::Trinket_Amulet;
        if (detailType == "Ring") return ItemSubType::Trinket_Ring;
        break;
    case ItemType::UpgradeComponent:
        if (detailType == "Rune") return ItemSubType::UpgradeComponent_Rune;
        if (detailType == "Sigil") return ItemSubType::UpgradeComponent_Sigil;
        if (detailType == "Gem") return ItemSubType::UpgradeComponent_Gem;
        if (detailType == "Default") return ItemSubType::UpgradeComponent_Default;
        break;
    case ItemType::Container:
        if (detailType == "Default") return ItemSubType::Container_Default;
        if (detailType == "GiftBox") return ItemSubType::Container_GiftBox;
        if (detailType == "Immediate") return ItemSubType::Container_Immediate;
        if (detailType == "OpenUI") return ItemSubType::Container_OpenUI;
        break;
    case ItemType::Gathering:
        if (detailType == "Foraging") return ItemSubType::Gathering_Foraging;
        if (detailType == "Logging") return ItemSubType::Gathering_Logging;
        if (detailType == "Mining") return ItemSubType::Gathering_Mining;
        if (detailType == "Fishing") return ItemSubType::Gathering_Fishing;
        if (detailType == "Bait") return ItemSubType::Gathering_Bait;
        if (detailType == "Lure") return ItemSubType::Gathering_Lure;
        break;
    case ItemType::Gizmo:
        if (detailType == "Default") return ItemSubType::Gizmo_Default;
        if (detailType == "ContainerKey") return ItemSubType::Gizmo_ContainerKey;
        if (detailType == "RentableContractNpc") return ItemSubType::Gizmo_RentableContractNpc;
        if (detailType == "UnlimitedConsumable") return ItemSubType::Gizmo_UnlimitedConsumable;
        break;
    case ItemType::Consumable:
        if (detailType == "AppearanceChange") return ItemSubType::Consumable_AppearanceChange;
        if (detailType == "Booze") return ItemSubType::Consumable_Booze;
        if (detailType == "ContractNpc") return ItemSubType::Consumable_ContractNpc;
        if (detailType == "Currency") return ItemSubType::Consumable_Currency;
        if (detailType == "Food") return ItemSubType::Consumable_Food;
        if (detailType == "Generic") return ItemSubType::Consumable_Generic;
        if (detailType == "Halloween") return ItemSubType::Consumable_Halloween;
        if (detailType == "Immediate") return ItemSubType::Consumable_Immediate;
        if (detailType == "MountRandomUnlock") return ItemSubType::Consumable_MountRandomUnlock;
        if (detailType == "RandomUnlock") return ItemSubType::Consumable_RandomUnlock;
        if (detailType == "Transmutation") return ItemSubType::Consumable_Transmutation;
        if (detailType == "Unlock") return ItemSubType::Consumable_Unlock;
        if (detailType == "UpgradeRemoval") return ItemSubType::Consumable_UpgradeRemoval;
        if (detailType == "Utility") return ItemSubType::Consumable_Utility;
        if (detailType == "TeleportToFriend") return ItemSubType::Consumable_TeleportToFriend;
        break;
    default:
        break;
    }
    return ItemSubType::Unknown;
}

static const char* ItemTypeToDisplayNameImpl(ItemType type)
{
    switch (type)
    {
    case ItemType::Armor: return "Armor";
    case ItemType::Back: return "Back";
    case ItemType::Bag: return "Bag";
    case ItemType::Consumable: return "Consumable";
    case ItemType::Container: return "Container";
    case ItemType::CraftingMaterial: return "Crafting Material";
    case ItemType::Gathering: return "Gathering";
    case ItemType::Gizmo: return "Gizmo";
    case ItemType::Key: return "Key";
    case ItemType::MiniPet: return "Mini Pet";
    case ItemType::Tool: return "Tool";
    case ItemType::Trait: return "Trait";
    case ItemType::Trinket: return "Trinket";
    case ItemType::Trophy: return "Trophy";
    case ItemType::UpgradeComponent: return "Upgrade Component";
    case ItemType::Weapon: return "Weapon";
    case ItemType::PowerCore: return "Power Core";
    case ItemType::JadeTechModule: return "Jade Tech Module";
    case ItemType::Relic: return "Relic";
    default: return "Unknown";
    }
}

const char* ItemDb::ItemTypeToDisplayName(ItemType type)
{
    return ItemTypeToDisplayNameImpl(type);
}

const std::vector<ItemSubType>& ItemDb::GetSubTypesForType(ItemType type)
{
    static const std::vector<ItemSubType> s_armor = {
        ItemSubType::Armor_Helm, ItemSubType::Armor_Shoulders,
        ItemSubType::Armor_Coat, ItemSubType::Armor_Gloves,
        ItemSubType::Armor_Leggings, ItemSubType::Armor_Boots,
        ItemSubType::Armor_HelmAquatic
    };
    static const std::vector<ItemSubType> s_weapon = {
        ItemSubType::Weapon_Axe, ItemSubType::Weapon_Dagger,
        ItemSubType::Weapon_Mace, ItemSubType::Weapon_Pistol,
        ItemSubType::Weapon_Scepter, ItemSubType::Weapon_Sword,
        ItemSubType::Weapon_Focus, ItemSubType::Weapon_Shield,
        ItemSubType::Weapon_Torch, ItemSubType::Weapon_Warhorn,
        ItemSubType::Weapon_Greatsword, ItemSubType::Weapon_Hammer,
        ItemSubType::Weapon_LongBow, ItemSubType::Weapon_Rifle,
        ItemSubType::Weapon_ShortBow, ItemSubType::Weapon_Staff,
        ItemSubType::Weapon_Trident, ItemSubType::Weapon_Harpoon,
        ItemSubType::Weapon_Speargun, ItemSubType::Weapon_LargeBundle,
        ItemSubType::Weapon_SmallBundle, ItemSubType::Weapon_Toy,
        ItemSubType::Weapon_ToyTwoHanded
    };
    static const std::vector<ItemSubType> s_trinket = {
        ItemSubType::Trinket_Accessory, ItemSubType::Trinket_Amulet,
        ItemSubType::Trinket_Ring
    };
    static const std::vector<ItemSubType> s_upgrade = {
        ItemSubType::UpgradeComponent_Rune, ItemSubType::UpgradeComponent_Sigil,
        ItemSubType::UpgradeComponent_Gem, ItemSubType::UpgradeComponent_Default
    };
    static const std::vector<ItemSubType> s_container = {
        ItemSubType::Container_Default, ItemSubType::Container_GiftBox,
        ItemSubType::Container_Immediate, ItemSubType::Container_OpenUI
    };
    static const std::vector<ItemSubType> s_gathering = {
        ItemSubType::Gathering_Foraging, ItemSubType::Gathering_Logging,
        ItemSubType::Gathering_Mining, ItemSubType::Gathering_Fishing,
        ItemSubType::Gathering_Bait, ItemSubType::Gathering_Lure
    };
    static const std::vector<ItemSubType> s_gizmo = {
        ItemSubType::Gizmo_Default, ItemSubType::Gizmo_ContainerKey,
        ItemSubType::Gizmo_RentableContractNpc, ItemSubType::Gizmo_UnlimitedConsumable
    };
    static const std::vector<ItemSubType> s_consumable = {
        ItemSubType::Consumable_AppearanceChange, ItemSubType::Consumable_Booze,
        ItemSubType::Consumable_ContractNpc, ItemSubType::Consumable_Currency,
        ItemSubType::Consumable_Food, ItemSubType::Consumable_Generic,
        ItemSubType::Consumable_Halloween, ItemSubType::Consumable_Immediate,
        ItemSubType::Consumable_MountRandomUnlock, ItemSubType::Consumable_RandomUnlock,
        ItemSubType::Consumable_Transmutation, ItemSubType::Consumable_Unlock,
        ItemSubType::Consumable_UpgradeRemoval, ItemSubType::Consumable_Utility,
        ItemSubType::Consumable_TeleportToFriend
    };
    static const std::vector<ItemSubType> s_empty;

    switch (type)
    {
    case ItemType::Armor: return s_armor;
    case ItemType::Weapon: return s_weapon;
    case ItemType::Trinket: return s_trinket;
    case ItemType::UpgradeComponent: return s_upgrade;
    case ItemType::Container: return s_container;
    case ItemType::Gathering: return s_gathering;
    case ItemType::Gizmo: return s_gizmo;
    case ItemType::Consumable: return s_consumable;
    default: return s_empty;
    }
}

const char* ItemDb::ItemSubTypeToDisplayName(ItemSubType subType)
{
    switch (subType)
    {
    case ItemSubType::Armor_Boots: return "Boots";
    case ItemSubType::Armor_Coat: return "Coat";
    case ItemSubType::Armor_Gloves: return "Gloves";
    case ItemSubType::Armor_Helm: return "Helm";
    case ItemSubType::Armor_HelmAquatic: return "Helm (Aquatic)";
    case ItemSubType::Armor_Leggings: return "Leggings";
    case ItemSubType::Armor_Shoulders: return "Shoulders";
    case ItemSubType::Weapon_Axe: return "Axe";
    case ItemSubType::Weapon_Dagger: return "Dagger";
    case ItemSubType::Weapon_Mace: return "Mace";
    case ItemSubType::Weapon_Pistol: return "Pistol";
    case ItemSubType::Weapon_Scepter: return "Scepter";
    case ItemSubType::Weapon_Sword: return "Sword";
    case ItemSubType::Weapon_Focus: return "Focus";
    case ItemSubType::Weapon_Shield: return "Shield";
    case ItemSubType::Weapon_Torch: return "Torch";
    case ItemSubType::Weapon_Warhorn: return "Warhorn";
    case ItemSubType::Weapon_Greatsword: return "Greatsword";
    case ItemSubType::Weapon_Hammer: return "Hammer";
    case ItemSubType::Weapon_LongBow: return "Longbow";
    case ItemSubType::Weapon_Rifle: return "Rifle";
    case ItemSubType::Weapon_ShortBow: return "Shortbow";
    case ItemSubType::Weapon_Staff: return "Staff";
    case ItemSubType::Weapon_Trident: return "Trident";
    case ItemSubType::Weapon_Harpoon: return "Harpoon";
    case ItemSubType::Weapon_Speargun: return "Speargun";
    case ItemSubType::Weapon_LargeBundle: return "Large Bundle";
    case ItemSubType::Weapon_SmallBundle: return "Small Bundle";
    case ItemSubType::Weapon_Toy: return "Toy";
    case ItemSubType::Weapon_ToyTwoHanded: return "Toy (Two-Handed)";
    case ItemSubType::Trinket_Accessory: return "Accessory";
    case ItemSubType::Trinket_Amulet: return "Amulet";
    case ItemSubType::Trinket_Ring: return "Ring";
    case ItemSubType::UpgradeComponent_Rune: return "Rune";
    case ItemSubType::UpgradeComponent_Sigil: return "Sigil";
    case ItemSubType::UpgradeComponent_Gem: return "Gem";
    case ItemSubType::UpgradeComponent_Default: return "Upgrade";
    case ItemSubType::Container_Default: return "Container";
    case ItemSubType::Container_GiftBox: return "Gift Box";
    case ItemSubType::Container_Immediate: return "Immediate";
    case ItemSubType::Container_OpenUI: return "Open UI";
    case ItemSubType::Gathering_Foraging: return "Foraging";
    case ItemSubType::Gathering_Logging: return "Logging";
    case ItemSubType::Gathering_Mining: return "Mining";
    case ItemSubType::Gathering_Fishing: return "Fishing";
    case ItemSubType::Gathering_Bait: return "Bait";
    case ItemSubType::Gathering_Lure: return "Lure";
    case ItemSubType::Gizmo_Default: return "Gizmo";
    case ItemSubType::Gizmo_ContainerKey: return "Container Key";
    case ItemSubType::Gizmo_RentableContractNpc: return "Contract NPC";
    case ItemSubType::Gizmo_UnlimitedConsumable: return "Unlimited Consumable";
    case ItemSubType::Consumable_AppearanceChange: return "Appearance Change";
    case ItemSubType::Consumable_Booze: return "Booze";
    case ItemSubType::Consumable_ContractNpc: return "Contract NPC";
    case ItemSubType::Consumable_Currency: return "Currency";
    case ItemSubType::Consumable_Food: return "Food";
    case ItemSubType::Consumable_Generic: return "Generic";
    case ItemSubType::Consumable_Halloween: return "Halloween";
    case ItemSubType::Consumable_Immediate: return "Immediate";
    case ItemSubType::Consumable_MountRandomUnlock: return "Mount Unlock";
    case ItemSubType::Consumable_RandomUnlock: return "Random Unlock";
    case ItemSubType::Consumable_Transmutation: return "Transmutation";
    case ItemSubType::Consumable_Unlock: return "Unlock";
    case ItemSubType::Consumable_UpgradeRemoval: return "Upgrade Removal";
    case ItemSubType::Consumable_Utility: return "Utility";
    case ItemSubType::Consumable_TeleportToFriend: return "Teleport to Friend";
    default: return "";
    }
}
