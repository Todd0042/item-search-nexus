#include "gw2_api.h"
#include "http_client.h"
#include "addon.h"
#include <sstream>
#include <algorithm>

Gw2Api& Gw2Api::Instance()
{
    static Gw2Api instance;
    return instance;
}

json Gw2Api::GetJson(const std::string& endpoint)
{
    HttpResult result = HttpClient::Instance().GetJson(endpoint);
    if (!result.Success)
    {
        LogWarn(("GW2 API request failed: " + endpoint + " Status: " + std::to_string(result.StatusCode)).c_str());
        return json();
    }
    try
    {
        return json::parse(result.Body);
    }
    catch (const std::exception& e)
    {
        LogWarn(("Failed to parse JSON from " + endpoint + ": " + std::string(e.what())).c_str());
        return json();
    }
}

json Gw2Api::GetJsonWithAuth(const std::string& endpoint)
{
    std::string url = m_baseUrl + endpoint;
    HttpResult result = HttpClient::Instance().Get(url, m_apiKey);
    if (!result.Success)
    {
        LogWarn(("GW2 API auth request failed: " + endpoint + " Status: " + std::to_string(result.StatusCode)).c_str());
        return json();
    }
    try
    {
        return json::parse(result.Body);
    }
    catch (const std::exception& e)
    {
        LogWarn(("Failed to parse JSON from " + endpoint + ": " + std::string(e.what())).c_str());
        return json();
    }
}

std::string Gw2Api::GetAccountName()
{
    auto data = GetJsonWithAuth("/v2/account");
    if (data.is_null() || !data.contains("name"))
        return "";
    return data["name"].get<std::string>();
}

std::vector<int> Gw2Api::GetAllItemIds()
{
    auto data = GetJson("/v2/items");
    if (data.is_null()) return {};
    return data.get<std::vector<int>>();
}

json Gw2Api::GetItems(const std::vector<int>& ids)
{
    if (ids.empty()) return json::array();

    std::string idsStr;
    for (size_t i = 0; i < ids.size(); i++)
    {
        if (i > 0) idsStr += ",";
        idsStr += std::to_string(ids[i]);
    }
    return GetJson("/v2/items?ids=" + idsStr);
}

json Gw2Api::GetItem(int id)
{
    return GetJson("/v2/items/" + std::to_string(id));
}

json Gw2Api::GetBank()
{
    return GetJsonWithAuth("/v2/account/bank");
}

json Gw2Api::GetSharedInventory()
{
    return GetJsonWithAuth("/v2/account/inventory");
}

json Gw2Api::GetMaterials()
{
    return GetJsonWithAuth("/v2/account/materials");
}

json Gw2Api::GetCharacters()
{
    auto data = GetJsonWithAuth("/v2/characters");
    if (data.is_null()) return json::array();
    return data;
}

json Gw2Api::GetCharactersWithAges()
{
    return GetJsonWithAuth("/v2/characters?page=0&page_size=200");
}

json Gw2Api::GetCharacterCore(const std::string& name)
{
    return GetJsonWithAuth("/v2/characters/" + name + "/core");
}

json Gw2Api::GetCharacterBags(const std::string& name)
{
    return GetJsonWithAuth("/v2/characters/" + name + "/inventory");
}

json Gw2Api::GetCharacterEquipmentTabs(const std::string& name)
{
    return GetJsonWithAuth("/v2/characters/" + name + "/equipmenttabs?tabs=all");
}

json Gw2Api::GetCharacterEquipment(const std::string& name)
{
    std::string endpoint = "/v2/characters/" + name + "/equipment?v=2024-04-01T00:00:00.000Z";
    return GetJsonWithAuth(endpoint);
}

json Gw2Api::GetTradingPostDelivery()
{
    return GetJsonWithAuth("/v2/commerce/delivery");
}

json Gw2Api::GetTradingPostSells()
{
    return GetJsonWithAuth("/v2/commerce/transactions/current/sells");
}

json Gw2Api::GetLegendaryArmory()
{
    return GetJsonWithAuth("/v2/account/legendaryarmory");
}
