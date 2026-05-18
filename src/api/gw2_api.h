#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Gw2Api
{
public:
    static Gw2Api& Instance();

    void SetApiKey(const std::string& key) { m_apiKey = key; }
    bool HasApiKey() const { return !m_apiKey.empty(); }
    const std::string& GetApiKey() const { return m_apiKey; }

    std::string GetAccountName();
    std::vector<int> GetAllItemIds();
    json GetItems(const std::vector<int>& ids);
    json GetItem(int id);
    json GetBank();
    json GetSharedInventory();
    json GetMaterials();
    json GetCharacters();
    json GetCharactersWithAges();
    json GetCharacterCore(const std::string& name);
    json GetCharacterBags(const std::string& name);
    json GetCharacterEquipmentTabs(const std::string& name);
    json GetCharacterEquipment(const std::string& name);
    json GetTradingPostDelivery();
    json GetTradingPostSells();
    json GetLegendaryArmory();
    json SearchItems(const std::string& text);
    json GetJsonPublic(const std::string& endpoint) { return GetJson(endpoint); }

    template<typename T>
    T FetchWithRetry(std::function<T()> fn, int maxRetries = 3)
    {
        for (int i = 0; i < maxRetries; i++)
        {
            try
            {
                return fn();
            }
            catch (...)
            {
                if (i < maxRetries - 1)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        return T();
    }

private:
    Gw2Api() = default;
    std::string m_apiKey;
    std::string m_baseUrl = "https://api.guildwars2.com";

    json GetJson(const std::string& endpoint);
    json GetJsonWithAuth(const std::string& endpoint);
};
