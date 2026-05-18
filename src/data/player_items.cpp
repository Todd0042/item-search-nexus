#include "player_items.h"
#include "item_db.h"
#include "api/gw2_api.h"
#include "addon.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <chrono>

PlayerItems& PlayerItems::Instance()
{
    static PlayerItems instance;
    return instance;
}

void to_json(json& j, const CachedCharItem& c)
{
    j = json{
        {"id", c.Id},
        {"source", static_cast<int>(c.Source)},
        {"count", c.Count},
        {"upgrades", c.Upgrades},
        {"infusions", c.Infusions},
        {"tab", c.EquipmentTabId},
        {"stat_id", c.StatChoiceId},
        {"stats", c.StatAttributes}
    };
}

void from_json(const json& j, CachedCharItem& c)
{
    c.Id = j.value("id", 0);
    c.Source = static_cast<InventoryItemSource>(j.value("source", 0));
    c.Count = j.value("count", 1);
    c.Upgrades = j.value("upgrades", std::vector<int>());
    c.Infusions = j.value("infusions", std::vector<int>());
    c.EquipmentTabId = j.value("tab", -1);
    c.StatChoiceId = j.value("stat_id", 0);
    c.StatAttributes = j.value("stats", std::unordered_map<std::string, int>());
}

InventoryItem PlayerItems::MakeItem(int id, InventoryItemSource source, const std::string& character)
{
    InventoryItem item;
    item.Id = id;
    item.Source = source;
    item.CharacterName = character;
    item.ItemInfo = ItemDb::Instance().GetItemInfo(id);
    if (item.ItemInfo)
        item.Name = item.ItemInfo->Name;
    return item;
}

void PlayerItems::AddItem(std::unordered_map<int, std::vector<InventoryItem>>& items, int id, InventoryItem item)
{
    auto it = items.find(id);
    if (it == items.end())
    {
        items[id] = { item };
        return;
    }

    // Merge with an existing entry that shares the same identity (source, character,
    // upgrades, infusions, stats, equipment tab). Items with different upgrades/stats
    // are kept separate since they are meaningfully different (e.g. two of the same
    // weapon with different sigils).
    for (auto& existing : it->second)
    {
        if (existing.Source == item.Source &&
            existing.CharacterName == item.CharacterName &&
            existing.Upgrades == item.Upgrades &&
            existing.Infusions == item.Infusions &&
            existing.StatChoiceId == item.StatChoiceId &&
            existing.EquipmentTabId == item.EquipmentTabId)
        {
            existing.Count += item.Count;
            return;
        }
    }

    it->second.push_back(item);
}

void PlayerItems::Initialize(const std::string& apiKey, const std::string& cacheDir)
{
    Stop();
    m_cacheDir = cacheDir;
    Gw2Api::Instance().SetApiKey(apiKey);
    m_accountName = Gw2Api::Instance().GetAccountName();

    if (m_accountName.empty())
    {
        LogWarn("Failed to get account name");
        return;
    }

    LogInfo(("Account: " + m_accountName).c_str());

    // Fetch character list early to know total count for progress bar
    try
    {
        auto charsData = Gw2Api::Instance().GetCharactersWithAges();
        if (!charsData.is_null() && charsData.is_array())
            m_totalCharacters = (int)charsData.size();
    }
    catch (...)
    {
        m_totalCharacters = 0;
    }

    LoadCache();
    m_offlineReady = LoadFullCache();

    m_refreshRunning = true;
    m_refreshThread = std::thread([this]()
    {
        try
        {
            RefreshFromApi();
        }
        catch (const std::exception& e)
        {
            LogWarn(("RefreshFromApi failed: " + std::string(e.what())).c_str());
        }
        catch (...)
        {
            LogWarn("RefreshFromApi failed with unknown exception");
        }
        m_refreshRunning = false;
    });
}

void PlayerItems::Stop()
{
    if (m_refreshRunning)
    {
        m_refreshRunning = false;
        if (m_refreshThread.joinable())
            m_refreshThread.join();
    }
    StopBackgroundPolling();
}

static const int CHARACTER_CACHE_VERSION = 2;

void PlayerItems::LoadCache()
{
    std::string path = m_cacheDir + "\\character_cache.json";
    std::ifstream file(path);
    if (!file.is_open())
    {
        LogInfo("No character cache found");
        return;
    }

    try
    {
        json data = json::parse(file);
        int version = data.value("version", 0);
        if (version < CHARACTER_CACHE_VERSION)
        {
            LogInfo("Character cache version mismatch, will re-fetch");
            return;
        }
        for (auto& [name, entry] : data.items())
        {
            if (name == "version") continue;
            m_cachedAges[name] = entry.value("age", (int64_t)0);
        }
        LogInfo(("Character cache loaded: " + std::to_string(m_cachedAges.size()) + " characters").c_str());
    }
    catch (const std::exception& e)
    {
        LogWarn(("Failed to parse character cache: " + std::string(e.what())).c_str());
    }
}

void PlayerItems::SaveCache()
{
    std::string path = m_cacheDir + "\\character_cache.json";
    json output = json::object();
    output["version"] = CHARACTER_CACHE_VERSION;

    for (auto& [name, age] : m_charAges)
    {
        json entry;
        entry["age"] = age;

        // Re-serialize cached items for each character from the current data
        json items = json::array();
        if (m_items.empty())
        {
            output[name] = entry;
            continue;
        }

        // Collect all items belonging to this character
        for (auto& [id, itemList] : m_items)
        {
            for (auto& invItem : itemList)
            {
                if (invItem.CharacterName == name &&
                    (invItem.Source == InventoryItemSource::CharacterInventory ||
                     invItem.Source == InventoryItemSource::CharacterEquipment))
                {
                    CachedCharItem ci;
                    ci.Id = invItem.Id;
                    ci.Source = invItem.Source;
                    ci.Count = invItem.Count;
                    ci.Upgrades = invItem.Upgrades;
                    ci.Infusions = invItem.Infusions;
                    ci.EquipmentTabId = invItem.EquipmentTabId;
                    ci.StatChoiceId = invItem.StatChoiceId;
                    ci.StatAttributes = invItem.StatAttributes;
                    items.push_back(ci);
                }
            }
        }
        entry["items"] = items;
        output[name] = entry;
    }

    std::string tempPath = path + ".temp";
    std::ofstream file(tempPath);
    if (file.is_open())
    {
        file << output.dump(2);
        file.close();
        MoveFileExA(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

void PlayerItems::SaveFullCache()
{
    std::string path = m_cacheDir + "\\full_cache.json";
    std::string tempPath = path + ".temp";
    json output = json::object();
    output["version"] = 1;
    json itemsArr = json::array();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, itemList] : m_items)
        {
            for (auto& invItem : itemList)
            {
                json j;
                j["id"] = invItem.Id;
                j["source"] = static_cast<int>(invItem.Source);
                j["char"] = invItem.CharacterName;
                j["count"] = invItem.Count;
                j["stat_id"] = invItem.StatChoiceId;
                if (!invItem.StatAttributes.empty())
                {
                    json attrs = json::object();
                    for (auto& [k, v] : invItem.StatAttributes)
                        attrs[k] = v;
                    j["stats"] = attrs;
                }
                if (!invItem.Upgrades.empty())
                    j["upgrades"] = invItem.Upgrades;
                if (!invItem.Infusions.empty())
                    j["infusions"] = invItem.Infusions;
                j["tab"] = invItem.EquipmentTabId;
                j["parent"] = invItem.ParentItemId;
                itemsArr.push_back(j);
            }
        }
    }
    output["items"] = itemsArr;

    std::ofstream file(tempPath);
    if (file.is_open())
    {
        file << output.dump(2);
        file.close();
        MoveFileExA(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    LogInfo("Full cache saved");
}

bool PlayerItems::LoadFullCache()
{
    std::string path = m_cacheDir + "\\full_cache.json";
    std::ifstream file(path);
    if (!file.is_open())
    {
        LogInfo("No full cache found, trying character cache as fallback");
        // Try to reconstruct from character_cache.json
        std::string charCachePath = m_cacheDir + "\\character_cache.json";
        std::ifstream charFile(charCachePath);
        if (!charFile.is_open())
            return false;
        try
        {
            json data = json::parse(charFile);
            int ver = data.value("version", 0);
            if (ver < CHARACTER_CACHE_VERSION) return false;

            std::unordered_map<int, std::vector<InventoryItem>> items;
            int totalItems = 0;
            for (auto& [name, entry] : data.items())
            {
                if (name == "version") continue;
                if (!entry.contains("items") || !entry["items"].is_array()) continue;
                for (auto& ci : entry["items"])
                {
                    InventoryItem item;
                    item.Id = ci.value("id", 0);
                    item.Source = static_cast<InventoryItemSource>(ci.value("source", 0));
                    item.CharacterName = name;
                    item.Count = ci.value("count", 1);
                    item.StatChoiceId = ci.value("stat_id", 0);
                    item.EquipmentTabId = ci.value("tab", -1);
                    if (ci.contains("stats"))
                    {
                        for (auto& [k, v] : ci["stats"].items())
                            item.StatAttributes[k] = v.get<int>();
                    }
                    if (ci.contains("upgrades"))
                        item.Upgrades = ci["upgrades"].get<std::vector<int>>();
                    if (ci.contains("infusions"))
                        item.Infusions = ci["infusions"].get<std::vector<int>>();

                    auto* info = ItemDb::Instance().GetItemInfo(item.Id);
                    if (info) item.ItemInfo = info;
                    AddItem(items, item.Id, item);
                    totalItems++;
                }
            }

            if (items.empty()) return false;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_items = std::move(items);
            }
            m_totalItemCount = totalItems;
            LogInfo(("Reconstructed offline cache from character data: " + std::to_string(totalItems) + " items").c_str());
            return true;
        }
        catch (const std::exception& e)
        {
            LogWarn(("Failed to reconstruct offline cache: " + std::string(e.what())).c_str());
            return false;
        }
    }

    try
    {
        json data = json::parse(file);
        int version = data.value("version", 0);
        if (version < 1) return false;

        std::unordered_map<int, std::vector<InventoryItem>> items;
        for (auto& j : data["items"])
        {
            InventoryItem item;
            item.Id = j["id"].get<int>();
            item.Source = static_cast<InventoryItemSource>(j.value("source", 0));
            item.CharacterName = j.value("char", "");
            item.Count = j.value("count", 1);
            item.StatChoiceId = j.value("stat_id", 0);
            item.EquipmentTabId = j.value("tab", -1);
            item.ParentItemId = j.value("parent", 0);
            if (j.contains("stats"))
            {
                for (auto& [k, v] : j["stats"].items())
                    item.StatAttributes[k] = v.get<int>();
            }
            if (j.contains("upgrades"))
                item.Upgrades = j["upgrades"].get<std::vector<int>>();
            if (j.contains("infusions"))
                item.Infusions = j["infusions"].get<std::vector<int>>();

            auto* info = ItemDb::Instance().GetItemInfo(item.Id);
            if (info) item.ItemInfo = info;

            AddItem(items, item.Id, item);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_items = std::move(items);
        }

        int totalCount = 0;
        for (auto& [id, itemList] : m_items)
            totalCount += (int)itemList.size();
        m_totalItemCount = totalCount;

        LogInfo(("Full cache loaded: " + std::to_string(m_items.size()) + " unique types, " + std::to_string(totalCount) + " total").c_str());
        return true;
    }
    catch (const std::exception& e)
    {
        LogWarn(("Failed to load full cache: " + std::string(e.what())).c_str());
        return false;
    }
}

static void ApplyItemStats(InventoryItem& item, const json& slot)
{
    if (slot.contains("stats") && slot["stats"].is_object())
    {
        auto& stats = slot["stats"];
        item.StatChoiceId = stats.value("id", 0);
        if (stats.contains("attributes") && stats["attributes"].is_object())
        {
            for (auto& [key, val] : stats["attributes"].items())
            {
                item.StatAttributes[key] = val.get<int>();
            }
        }
    }
    else
    {
        // Fall back to item's built-in stats (fixed-stat items like named ascended)
        auto* info = ItemDb::Instance().GetItemInfo(item.Id);
        if (info && info->StaticStatChoiceId != 0)
        {
            item.StatChoiceId = info->StaticStatChoiceId;
            item.StatAttributes = info->StaticStatAttributes;
        }
    }
}

void PlayerItems::ParseCharacterFromApi(const std::string& name, std::unordered_map<int, std::vector<InventoryItem>>& allItems)
{
    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&]() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };
    auto& api = Gw2Api::Instance();

    // Character inventory
    auto bags = api.GetCharacterBags(name);
    LogInfo((std::to_string(ms()) + "ms [CHAR] " + name + " bags fetched").c_str());
    if (!bags.is_null() && bags.contains("bags") && bags["bags"].is_array())
    {
        for (auto& bag : bags["bags"])
        {
            if (bag.is_null()) continue;
            int bagId = bag["id"].get<int>();
            InventoryItem bagItem = MakeItem(bagId, InventoryItemSource::CharacterEquipment, name);
            AddItem(allItems, bagId, bagItem);

            if (bag.contains("inventory") && bag["inventory"].is_array())
            {
                for (auto& invSlot : bag["inventory"])
                {
                    if (!invSlot.is_null() && invSlot.contains("id"))
                    {
                        InventoryItem item = MakeItem(invSlot["id"].get<int>(), InventoryItemSource::CharacterInventory, name);
                        if (invSlot.contains("count")) item.Count = invSlot["count"].get<int>();
                        if (invSlot.contains("upgrades") && invSlot["upgrades"].is_array())
                            item.Upgrades = invSlot["upgrades"].get<std::vector<int>>();
                        if (invSlot.contains("infusions") && invSlot["infusions"].is_array())
                            item.Infusions = invSlot["infusions"].get<std::vector<int>>();
                        ApplyItemStats(item, invSlot);
                        AddItem(allItems, item.Id, item);
                    }
                }
            }
        }
    }

    // Equipment tabs
    auto equipTabs = api.GetCharacterEquipmentTabs(name);
    LogInfo((std::to_string(ms()) + "ms [CHAR] " + name + " equipTabs fetched").c_str());
    if (!equipTabs.is_null() && equipTabs.is_array())
    {
        for (auto& tab : equipTabs)
        {
            if (tab.is_null() || !tab.contains("equipment")) continue;
            int tabId = tab.value("tab", 0);

            for (auto& equip : tab["equipment"])
            {
                if (!equip.is_null() && equip.contains("id"))
                {
                    InventoryItem item = MakeItem(equip["id"].get<int>(), InventoryItemSource::CharacterEquipment, name);
                    item.EquipmentTabId = tabId;
                    if (equip.contains("upgrades") && equip["upgrades"].is_array())
                        item.Upgrades = equip["upgrades"].get<std::vector<int>>();
                    if (equip.contains("infusions") && equip["infusions"].is_array())
                        item.Infusions = equip["infusions"].get<std::vector<int>>();
                    ApplyItemStats(item, equip);
                    AddItem(allItems, item.Id, item);
                }
            }
        }
    }

    // Equipment (relic, tools)
    auto equip = api.GetCharacterEquipment(name);
    LogInfo((std::to_string(ms()) + "ms [CHAR] " + name + " equipment fetched").c_str());
    if (!equip.is_null() && equip.contains("equipment") && equip["equipment"].is_array())
    {
        for (auto& eq : equip["equipment"])
        {
            if (!eq.is_null() && eq.contains("id"))
            {
                InventoryItem item = MakeItem(eq["id"].get<int>(), InventoryItemSource::CharacterEquipment, name);
                if (eq.contains("upgrades") && eq["upgrades"].is_array())
                    item.Upgrades = eq["upgrades"].get<std::vector<int>>();
                ApplyItemStats(item, eq);
                AddItem(allItems, item.Id, item);
            }
        }
    }
    LogInfo((std::to_string(ms()) + "ms [CHAR] " + name + " parse complete").c_str());
}

void PlayerItems::ParseCharacterFromCache(const CachedCharacter& cached, const std::string& name, std::unordered_map<int, std::vector<InventoryItem>>& allItems)
{
    for (auto& ci : cached.Items)
    {
        InventoryItem item = MakeItem(ci.Id, ci.Source, name);
        item.Count = ci.Count;
        item.Upgrades = ci.Upgrades;
        item.Infusions = ci.Infusions;
        item.EquipmentTabId = ci.EquipmentTabId;
        item.StatChoiceId = ci.StatChoiceId;
        item.StatAttributes = ci.StatAttributes;
        AddItem(allItems, item.Id, item);
    }
}

void PlayerItems::RefreshFromApi()
{
    auto t0 = std::chrono::steady_clock::now();
    auto ms = [&]() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };
    LogInfo("0ms [PLAYER] RefreshFromApi start");

    m_ready = false;
    m_progress = 0.0f;
    auto& api = Gw2Api::Instance();
    auto& db = ItemDb::Instance();

    std::unordered_map<int, std::vector<InventoryItem>> allItems;

    // Bank (progress 0.0 → 0.10)
    m_progress = 0.05f;
    {
        auto bank = api.GetBank();
        int slots = 0;
        if (!bank.is_null() && bank.is_array())
        {
            for (auto& slot : bank)
            {
                if (!slot.is_null() && slot.contains("id"))
                {
                    InventoryItem item = MakeItem(slot["id"].get<int>(), InventoryItemSource::Bank);
                    if (slot.contains("count")) item.Count = slot["count"].get<int>();
                    if (slot.contains("upgrades") && slot["upgrades"].is_array())
                        item.Upgrades = slot["upgrades"].get<std::vector<int>>();
                    if (slot.contains("infusions") && slot["infusions"].is_array())
                        item.Infusions = slot["infusions"].get<std::vector<int>>();
                    ApplyItemStats(item, slot);
                    AddItem(allItems, item.Id, item);
                    slots++;
                }
            }
        }
        LogInfo((std::to_string(ms()) + "ms [PLAYER] Bank: " + std::to_string(slots) + " slots").c_str());
    }
    m_progress = 0.10f;

    // Shared Inventory (progress 0.10 → 0.15)
    {
        auto sharedInv = api.GetSharedInventory();
        int slots = 0;
        if (!sharedInv.is_null() && sharedInv.is_array())
        {
            for (auto& slot : sharedInv)
            {
                if (!slot.is_null() && slot.contains("id"))
                {
                    InventoryItem item = MakeItem(slot["id"].get<int>(), InventoryItemSource::SharedInventory);
                    if (slot.contains("count")) item.Count = slot["count"].get<int>();
                    AddItem(allItems, item.Id, item);
                    slots++;
                }
            }
        }
        LogInfo((std::to_string(ms()) + "ms [PLAYER] SharedInv: " + std::to_string(slots) + " slots").c_str());
    }
    m_progress = 0.15f;

    // Materials (progress 0.15 → 0.20)
    {
        auto materials = api.GetMaterials();
        int count = 0;
        if (!materials.is_null() && materials.is_array())
        {
            for (auto& mat : materials)
            {
                if (!mat.is_null() && mat.contains("id") && mat.contains("count"))
                {
                    int c = mat["count"].get<int>();
                    if (c > 0)
                    {
                        InventoryItem item = MakeItem(mat["id"].get<int>(), InventoryItemSource::MaterialStorage);
                        item.Count = c;
                        AddItem(allItems, item.Id, item);
                        count++;
                    }
                }
            }
        }
        LogInfo((std::to_string(ms()) + "ms [PLAYER] Materials: " + std::to_string(count) + " types").c_str());
    }
    m_progress = 0.20f;

    // Characters with age tracking — parallel (progress 0.20 → 0.85)
    int totalChars = m_totalCharacters.load();
    {
        auto characters = api.GetCharactersWithAges();
        int realCharCount = characters.is_array() ? (int)characters.size() : 0;
        if (totalChars == 0 && realCharCount > 0)
            totalChars = realCharCount;
        LogInfo((std::to_string(ms()) + "ms [PLAYER] Characters list: " + std::to_string(realCharCount) + " chars").c_str());

        if (!characters.is_null() && characters.is_array())
        {
            float charProgressStart = 0.20f;
            float charProgressRange = 0.65f;
            float charStep = charProgressRange / std::max((float)totalChars, 1.0f);
            std::atomic<int> nextCharIdx{ 0 };
            std::atomic<int> completedChars{ 0 };
            std::atomic<int> fetchedChars{ 0 };
            std::atomic<int> cachedChars{ 0 };
            std::mutex mergeMutex;
            std::vector<std::unordered_map<int, std::vector<InventoryItem>>> charResults(realCharCount);
            std::vector<std::string> charNames(realCharCount);
            std::vector<int64_t> charAges(realCharCount);

            const int numCharWorkers = 6;
            auto charWorker = [&]()
            {
                while (true)
                {
                    int idx = nextCharIdx.fetch_add(1);
                    if (idx >= realCharCount) break;

                    auto& entry = characters[idx];
                    std::string name = entry["name"].get<std::string>();
                    int64_t currentAge = entry.value("age", (int64_t)0);
                    charNames[idx] = name;
                    charAges[idx] = currentAge;

                    auto cacheIt = m_cachedAges.find(name);
                    bool useCache = (cacheIt != m_cachedAges.end() && cacheIt->second == currentAge);

                    auto& outItems = charResults[idx];

                    if (useCache)
                    {
                        std::string charPath = m_cacheDir + "\\chars\\" + name + ".json";
                        std::ifstream charFile(charPath);
                        if (charFile.is_open())
                        {
                            try
                            {
                                json cachedJson = json::parse(charFile);
                                int charVersion = cachedJson.value("version", 0);
                                if (charVersion >= CHARACTER_CACHE_VERSION)
                                {
                                    CachedCharacter cached;
                                    cached.Age = cachedJson.value("age", (int64_t)0);
                                    cached.Items = cachedJson.value("items", std::vector<CachedCharItem>());
                                    ParseCharacterFromCache(cached, name, outItems);
                                    cachedChars++;
                                    useCache = true;
                                }
                                else { useCache = false; }
                            }
                            catch (...) { useCache = false; }
                        }
                        else { useCache = false; }
                    }

                    if (!useCache)
                    {
                        ParseCharacterFromApi(name, outItems);
                        fetchedChars++;
                    }

                    completedChars++;
                    m_progress = charProgressStart + (float)completedChars * charStep;
                }
            };

            std::vector<std::thread> charWorkers;
            for (int w = 0; w < numCharWorkers; w++)
                charWorkers.emplace_back(charWorker);
            for (auto& w : charWorkers)
                w.join();

            // Merge results and update ages (single-threaded)
            for (int i = 0; i < realCharCount; i++)
            {
                if (!charResults[i].empty())
                {
                    for (auto& [id, items] : charResults[i])
                    {
                        auto& existing = allItems[id];
                        existing.insert(existing.end(), items.begin(), items.end());
                    }
                }
                m_charAges[charNames[i]] = charAges[i];
            }

            LogInfo((std::to_string(ms()) + "ms [PLAYER] Characters done: " + std::to_string(fetchedChars.load()) + " fetched, " + std::to_string(cachedChars.load()) + " cached").c_str());
        }
    }

    // Trading post delivery (progress 0.85 → 0.90)
    m_progress = 0.87f;
    {
        auto tpDelivery = api.GetTradingPostDelivery();
        int slots = 0;
        if (!tpDelivery.is_null() && tpDelivery.contains("items") && tpDelivery["items"].is_array())
        {
            for (auto& tpItem : tpDelivery["items"])
            {
                if (!tpItem.is_null() && tpItem.contains("id"))
                {
                    InventoryItem item = MakeItem(tpItem["id"].get<int>(), InventoryItemSource::TradingPostDeliveryBox);
                    if (tpItem.contains("count")) item.Count = tpItem["count"].get<int>();
                    AddItem(allItems, item.Id, item);
                    slots++;
                }
            }
        }
        LogInfo((std::to_string(ms()) + "ms [PLAYER] TPDelivery: " + std::to_string(slots) + " slots").c_str());
    }

    // TP sell orders (progress 0.90 → 0.93)
    m_progress = 0.91f;
    {
        auto tpSells = api.GetTradingPostSells();
        int slots = 0;
        if (!tpSells.is_null() && tpSells.is_array())
        {
            for (auto& sell : tpSells)
            {
                if (!sell.is_null() && sell.contains("item_id"))
                {
                    InventoryItem item = MakeItem(sell["item_id"].get<int>(), InventoryItemSource::TradingPostSellOrder);
                    if (sell.contains("quantity")) item.Count = sell["quantity"].get<int>();
                    AddItem(allItems, item.Id, item);
                    slots++;
                }
            }
        }
        LogInfo((std::to_string(ms()) + "ms [PLAYER] TPSells: " + std::to_string(slots) + " slots").c_str());
    }

    int totalCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items = std::move(allItems);
        for (auto& [id, items] : m_items)
            totalCount += (int)items.size();
    }
    m_totalItemCount = totalCount;
    LogInfo((std::to_string(ms()) + "ms [PLAYER] All items moved to m_items: " + std::to_string(m_items.size()) + " types, " + std::to_string(totalCount) + " slots").c_str());

    m_progress = 1.0f;
    m_ready = true;

    StartBackgroundPolling();

    // Save full snapshot for offline mode
    LogInfo((std::to_string(ms()) + "ms [PLAYER] Saving caches...").c_str());
    SaveFullCache();
    LogInfo((std::to_string(ms()) + "ms [PLAYER] SaveFullCache done").c_str());
    SaveCache();
    LogInfo((std::to_string(ms()) + "ms [PLAYER] SaveCache done").c_str());

    // Save per-character cache files for changed characters
    CreateDirectoryA((m_cacheDir + "\\chars").c_str(), NULL);
    int savedChars = 0;
    for (auto& [name, age] : m_charAges)
    {
        auto cacheIt = m_cachedAges.find(name);
        bool changed = (cacheIt == m_cachedAges.end() || cacheIt->second != age);
        if (!changed) continue;

        std::string charPath = m_cacheDir + "\\chars\\" + name + ".json";
        json charOut;
        charOut["age"] = age;
        charOut["version"] = CHARACTER_CACHE_VERSION;
        json items = json::array();

        for (auto& [id, itemList] : m_items)
        {
            for (auto& invItem : itemList)
            {
                if (invItem.CharacterName == name &&
                    (invItem.Source == InventoryItemSource::CharacterInventory ||
                     invItem.Source == InventoryItemSource::CharacterEquipment))
                {
                    CachedCharItem ci;
                    ci.Id = invItem.Id;
                    ci.Source = invItem.Source;
                    ci.Count = invItem.Count;
                    ci.Upgrades = invItem.Upgrades;
                    ci.Infusions = invItem.Infusions;
                    ci.EquipmentTabId = invItem.EquipmentTabId;
                    ci.StatChoiceId = invItem.StatChoiceId;
                    ci.StatAttributes = invItem.StatAttributes;
                    items.push_back(ci);
                }
            }
        }
        charOut["items"] = items;

        std::string charTempPath = charPath + ".temp";
        std::ofstream charFile(charTempPath);
        if (charFile.is_open())
        {
            charFile << charOut.dump(2);
            charFile.close();
            MoveFileExA(charTempPath.c_str(), charPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        savedChars++;
    }
    LogInfo((std::to_string(ms()) + "ms [PLAYER] Saved " + std::to_string(savedChars) + " per-character caches").c_str());

    // Sync cached ages so background polling doesn't re-fetch everything
    m_cachedAges = m_charAges;

    LogInfo((std::to_string(ms()) + "ms [PLAYER] RefreshFromApi complete: " + std::to_string(m_items.size()) + " types, " + std::to_string(totalCount) + " slots").c_str());
}

std::vector<InventoryItem> PlayerItems::FindMatches(const std::vector<int>& itemIds, bool excludeLegendaryArmory, bool excludeEquippedBags)
{
    std::vector<InventoryItem> results;

    for (int id : itemIds)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_items.find(id);
        if (it == m_items.end())
            continue;

        for (auto& item : it->second)
        {
            if (excludeLegendaryArmory && item.ItemInfo)
            {
                if (item.ItemInfo->Rarity == ItemRarity::Legendary &&
                    (item.ItemInfo->Type == ItemType::Armor ||
                     item.ItemInfo->Type == ItemType::Weapon ||
                     item.ItemInfo->Type == ItemType::Trinket ||
                     item.ItemInfo->Type == ItemType::UpgradeComponent))
                {
                    continue;
                }
            }

            if (excludeEquippedBags && item.Source == InventoryItemSource::CharacterEquipment && item.ItemInfo)
            {
                if (item.ItemInfo->Type == ItemType::Bag)
                    continue;
            }

            results.push_back(item);
        }
    }

    LogDebug(("FindMatches: " + std::to_string(itemIds.size()) + " ids in -> " + std::to_string(results.size()) + " items out").c_str());
    return results;
}

void PlayerItems::ReplaceCharacterItems(const std::string& name, std::unordered_map<int, std::vector<InventoryItem>>& charItems)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Remove old items for this character
    for (auto it = m_items.begin(); it != m_items.end(); )
    {
        auto& items = it->second;
        items.erase(std::remove_if(items.begin(), items.end(),
            [&](const InventoryItem& item) {
                return item.CharacterName == name &&
                    (item.Source == InventoryItemSource::CharacterInventory ||
                     item.Source == InventoryItemSource::CharacterEquipment);
            }), items.end());
        if (items.empty())
            it = m_items.erase(it);
        else
            ++it;
    }
    // Add new items
    for (auto& [id, items] : charItems)
    {
        auto& existing = m_items[id];
        existing.insert(existing.end(), items.begin(), items.end());
    }
}

void PlayerItems::SaveCharacterCache(const std::string& name)
{
    CreateDirectoryA((m_cacheDir + "\\chars").c_str(), NULL);
    std::string charPath = m_cacheDir + "\\chars\\" + name + ".json";
    std::string tempPath = charPath + ".temp";
    json charOut;
    charOut["age"] = m_cachedAges[name];
    charOut["version"] = CHARACTER_CACHE_VERSION;
    json items = json::array();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, itemList] : m_items)
    {
        for (auto& invItem : itemList)
        {
            if (invItem.CharacterName == name &&
                (invItem.Source == InventoryItemSource::CharacterInventory ||
                 invItem.Source == InventoryItemSource::CharacterEquipment))
            {
                CachedCharItem ci;
                ci.Id = invItem.Id;
                ci.Source = invItem.Source;
                ci.Count = invItem.Count;
                ci.Upgrades = invItem.Upgrades;
                ci.Infusions = invItem.Infusions;
                ci.EquipmentTabId = invItem.EquipmentTabId;
                ci.StatChoiceId = invItem.StatChoiceId;
                ci.StatAttributes = invItem.StatAttributes;
                items.push_back(ci);
            }
        }
    }
    charOut["items"] = items;

    // Write to temp file first, then atomically replace
    std::ofstream tempFile(tempPath);
    if (tempFile.is_open())
    {
        tempFile << charOut.dump(2);
        tempFile.close();
        MoveFileExA(tempPath.c_str(), charPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

void PlayerItems::RefreshSingleCharacter(const std::string& name)
{
    LogInfo(("Background refreshing character: " + name).c_str());

    // HTTP calls with no lock held
    std::unordered_map<int, std::vector<InventoryItem>> charItems;
    ParseCharacterFromApi(name, charItems);

    auto coreData = Gw2Api::Instance().GetCharacterCore(name);
    int64_t age = 0;
    if (!coreData.is_null() && coreData.contains("age"))
        age = coreData["age"].get<int64_t>();

    // Atomic replace under single lock
    ReplaceCharacterItems(name, charItems);

    // Update age tracking and remove from dirty list
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_charAges[name] = age;
        m_cachedAges[name] = age;
        m_dirtyCharacters.erase(name);
    }

    // Write to disk cache so it survives logout
    SaveCharacterCache(name);

    LogInfo(("Background refresh complete for: " + name).c_str());
}

void PlayerItems::StartBackgroundPolling()
{
    if (m_backgroundRunning)
    {
        LogDebug("Background polling already running, restarting");
        StopBackgroundPolling();
    }
    m_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!m_stopEvent)
    {
        LogWarn("Failed to create stop event for background polling");
        m_backgroundRunning = false;
        return;
    }
    m_backgroundRunning = true;
    m_dirtyCharacters.clear();
    m_lastMaterialsFetch = std::chrono::steady_clock::now();

    m_backgroundThread = std::thread([this]() { BackgroundLoop(); });
    LogInfo("Background polling started");
}

void PlayerItems::StopBackgroundPolling()
{
    m_backgroundRunning = false;
    if (m_stopEvent)
    {
        SetEvent(m_stopEvent);
        m_stopEvent = NULL;
    }
    if (m_backgroundThread.joinable())
        m_backgroundThread.join();
    LogInfo("Background polling stopped");
}

void PlayerItems::BackgroundLoop()
{
    auto& api = Gw2Api::Instance();
    const int pollIntervalMs = 120000; // 2 minutes

    while (m_backgroundRunning)
    {
        DWORD result = WaitForSingleObject(m_stopEvent, pollIntervalMs);
        if (result == WAIT_OBJECT_0 || !m_backgroundRunning)
            break;

        if (!api.HasApiKey() || !m_ready)
            continue;

        try
        {
            auto now = std::chrono::steady_clock::now();

            // Check all character ages in one API call
            auto charsData = api.GetCharactersWithAges();
            if (!charsData.is_null() && charsData.is_array())
            {
                for (auto& entry : charsData)
                {
                    if (!m_backgroundRunning) break;
                    if (!entry.contains("name") || !entry.contains("age"))
                        continue;

                    std::string name = entry["name"].get<std::string>();
                    int64_t age = entry["age"].get<int64_t>();

                    bool alreadyDirty = false;
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        alreadyDirty = m_dirtyCharacters.find(name) != m_dirtyCharacters.end();
                    }

                    if (!alreadyDirty)
                    {
                        auto cachedIt = m_cachedAges.find(name);
                        if (cachedIt == m_cachedAges.end() || cachedIt->second != age)
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_dirtyCharacters[name] = now;
                            LogDebug(("Character " + name + " marked dirty for background refresh").c_str());
                        }
                    }
                }
            }

            // Process dirty characters that have been dirty for >= 5.5 minutes
            std::vector<std::string> toRefresh;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto it = m_dirtyCharacters.begin(); it != m_dirtyCharacters.end(); )
                {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                    if (elapsed >= 330)
                    {
                        toRefresh.push_back(it->first);
                        it = m_dirtyCharacters.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            for (auto& name : toRefresh)
            {
                if (!m_backgroundRunning) break;
                RefreshSingleCharacter(name);
            }

            // Refresh materials every 20 minutes
            auto matElapsed = std::chrono::duration_cast<std::chrono::minutes>(now - m_lastMaterialsFetch).count();
            if (matElapsed >= 20)
            {
                LogInfo("Background refreshing materials");
                auto materials = api.GetMaterials();
                if (!materials.is_null() && materials.is_array())
                {
                    std::unordered_map<int, std::vector<InventoryItem>> matItems;
                    for (auto& mat : materials)
                    {
                        if (!mat.is_null() && mat.contains("id") && mat.contains("count"))
                        {
                            int count = mat["count"].get<int>();
                            if (count > 0)
                            {
                                InventoryItem item = MakeItem(mat["id"].get<int>(), InventoryItemSource::MaterialStorage);
                                item.Count = count;
                                AddItem(matItems, item.Id, item);
                            }
                        }
                    }

                    // Replace materials in m_items under a single lock
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        for (auto it = m_items.begin(); it != m_items.end(); )
                        {
                            auto& items = it->second;
                            items.erase(std::remove_if(items.begin(), items.end(),
                                [](const InventoryItem& item) {
                                    return item.Source == InventoryItemSource::MaterialStorage;
                                }), items.end());
                            if (items.empty())
                                it = m_items.erase(it);
                            else
                                ++it;
                        }
                        for (auto& [id, items] : matItems)
                        {
                            auto& existing = m_items[id];
                            existing.insert(existing.end(), items.begin(), items.end());
                        }
                    }

                    m_lastMaterialsFetch = now;
                }
            }
        }
        catch (const std::exception& e)
        {
            LogWarn(("Background polling error: " + std::string(e.what())).c_str());
        }
        catch (...) {}
    }

    if (m_stopEvent)
    {
        CloseHandle(m_stopEvent);
        m_stopEvent = NULL;
    }
}
