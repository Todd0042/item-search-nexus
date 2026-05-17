#include "player_items.h"
#include "item_db.h"
#include "api/gw2_api.h"
#include "addon.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

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
    }
    else
    {
        it->second.push_back(item);
    }
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
    auto& api = Gw2Api::Instance();

    // Character inventory
    auto bags = api.GetCharacterBags(name);
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
    m_ready = false;
    m_progress = 0.0f;
    auto& api = Gw2Api::Instance();
    auto& db = ItemDb::Instance();

    std::unordered_map<int, std::vector<InventoryItem>> allItems;

    // Always refetch: bank, shared inv, materials, TP (no version check)
    // Bank
    m_progress = 0.05f;
    auto bank = api.GetBank();
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
            }
        }
    }

    // Shared Inventory
    m_progress = 0.15f;
    auto sharedInv = api.GetSharedInventory();
    if (!sharedInv.is_null() && sharedInv.is_array())
    {
        for (auto& slot : sharedInv)
        {
            if (!slot.is_null() && slot.contains("id"))
            {
                InventoryItem item = MakeItem(slot["id"].get<int>(), InventoryItemSource::SharedInventory);
                if (slot.contains("count")) item.Count = slot["count"].get<int>();
                AddItem(allItems, item.Id, item);
            }
        }
    }

    // Materials
    m_progress = 0.25f;
    auto materials = api.GetMaterials();
    if (!materials.is_null() && materials.is_array())
    {
        for (auto& mat : materials)
        {
            if (!mat.is_null() && mat.contains("id") && mat.contains("count"))
            {
                int count = mat["count"].get<int>();
                if (count > 0)
                {
                    InventoryItem item = MakeItem(mat["id"].get<int>(), InventoryItemSource::MaterialStorage);
                    item.Count = count;
                    AddItem(allItems, item.Id, item);
                }
            }
        }
    }

    // Characters with age tracking
    m_progress = 0.35f;
    auto characters = api.GetCharacters();
    if (!characters.is_null() && characters.is_array())
    {
        float charStep = 0.45f / std::max((float)characters.size(), 1.0f);
        int charIdx = 0;

        for (auto& charName : characters)
        {
            std::string name = charName.get<std::string>();
            m_progress = 0.35f + charIdx * charStep;

            int64_t currentAge = 0;
            bool needsFetch = true;

            // Check if character age changed via core endpoint
            auto coreData = api.GetCharacterCore(name);
            if (!coreData.is_null() && coreData.contains("age"))
            {
                currentAge = coreData["age"].get<int64_t>();
                auto cacheIt = m_cachedAges.find(name);
                if (cacheIt != m_cachedAges.end() && cacheIt->second == currentAge)
                {
                    needsFetch = false;
                    LogDebug(("Character " + name + " unchanged (age=" + std::to_string(currentAge) + "), using cache").c_str());
                }
            }

            m_charAges[name] = currentAge;

            if (needsFetch)
            {
                LogInfo(("Fetching character data for " + name).c_str());
                ParseCharacterFromApi(name, allItems);
            }
            else
            {
                // Load cached character data from disk
                std::string charPath = m_cacheDir + "\\chars\\" + name + ".json";
                std::ifstream charFile(charPath);
                if (charFile.is_open())
                {
                    try
                    {
                        json cachedJson = json::parse(charFile);
                        int charVersion = cachedJson.value("version", 0);
                        if (charVersion < CHARACTER_CACHE_VERSION)
                        {
                            LogDebug(("Cached data for " + name + " is stale, re-fetching").c_str());
                            ParseCharacterFromApi(name, allItems);
                        }
                        else
                        {
                            CachedCharacter cached;
                            cached.Age = cachedJson.value("age", (int64_t)0);
                            cached.Items = cachedJson.value("items", std::vector<CachedCharItem>());
                            ParseCharacterFromCache(cached, name, allItems);
                        }
                    }
                    catch (...)
                    {
                        LogWarn(("Failed to parse cached data for " + name + ", re-fetching").c_str());
                        ParseCharacterFromApi(name, allItems);
                    }
                }
                else
                {
                    ParseCharacterFromApi(name, allItems);
                }
            }

            charIdx++;
        }
    }

    // Trading post delivery
    m_progress = 0.85f;
    auto tpDelivery = api.GetTradingPostDelivery();
    if (!tpDelivery.is_null() && tpDelivery.contains("items") && tpDelivery["items"].is_array())
    {
        for (auto& tpItem : tpDelivery["items"])
        {
            if (!tpItem.is_null() && tpItem.contains("id"))
            {
                InventoryItem item = MakeItem(tpItem["id"].get<int>(), InventoryItemSource::TradingPostDeliveryBox);
                if (tpItem.contains("count")) item.Count = tpItem["count"].get<int>();
                AddItem(allItems, item.Id, item);
            }
        }
    }

    // TP sell orders
    m_progress = 0.95f;
    auto tpSells = api.GetTradingPostSells();
    if (!tpSells.is_null() && tpSells.is_array())
    {
        for (auto& sell : tpSells)
        {
            if (!sell.is_null() && sell.contains("item_id"))
            {
                InventoryItem item = MakeItem(sell["item_id"].get<int>(), InventoryItemSource::TradingPostSellOrder);
                if (sell.contains("quantity")) item.Count = sell["quantity"].get<int>();
                AddItem(allItems, item.Id, item);
            }
        }
    }

    int totalCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items = std::move(allItems);
        for (auto& [id, items] : m_items)
            totalCount += (int)items.size();
    }
    m_totalItemCount = totalCount;

    m_progress = 1.0f;
    m_ready = true;

    StartBackgroundPolling();

    // Save full snapshot for offline mode
    SaveFullCache();

    // Save updated cache
    SaveCache();

    // Save per-character cache files for unchanged characters too
    CreateDirectoryA((m_cacheDir + "\\chars").c_str(), NULL);
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
    }

    LogInfo(("Player data loaded: " + std::to_string(m_items.size()) + " unique item types, " + std::to_string(totalCount) + " total item slots").c_str());
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
