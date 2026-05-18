#include "addon.h"
#include "api/gw2_api.h"
#include "data/item_db.h"
#include "data/player_items.h"
#include "ui/main_window.h"
#include "ui/icon_cache.h"
#include "icon_data.h"
#include "headers/mumbleheader.h"
#include <imgui.h>
#include <string>
#include <thread>
#include <fstream>

AddonAPI_t* APIDefs = nullptr;
bool IsMapOpen = false;

static HMODULE s_thisModule = nullptr;
static bool s_imguiInitialized = false;
static std::string s_addonDir;
static std::string s_cacheDir;
static std::string s_apiKey;

void Log(ELogLevel level, const char* msg)
{
    if (APIDefs && APIDefs->Log)
        APIDefs->Log(level, "ItemSearch", msg);
}

void LogInfo(const char* msg) { Log(LOGL_INFO, msg); }
void LogWarn(const char* msg) { Log(LOGL_WARNING, msg); }
void LogDebug(const char* msg) { Log(LOGL_DEBUG, msg); }

static void OnMumbleIdentity(void* eventData)
{
    Mumble::Identity* id = static_cast<Mumble::Identity*>(eventData);
    if (!id) return;
    if (strchr(id->Name, '\\') || strchr(id->Name, '/') || strchr(id->Name, ':'))
        return;
}

static void OnWindowResized(void*)
{
    auto* nexusLink = static_cast<NexusLinkData_t*>(APIDefs->DataLink_Get(DL_NEXUS_LINK));
    if (nexusLink)
        IsMapOpen = nexusLink->IsGameplay;
}

static std::string GetSettingsPath()
{
    return s_addonDir + "\\settings.json";
}

void SaveSettingsApiKey(const std::string& key)
{
    std::string path = GetSettingsPath();
    json j;
    std::ifstream in(path);
    if (in.is_open())
    {
        try { j = json::parse(in); }
        catch (...) { j = json::object(); }
    }
    j["api_key"] = key;
    std::ofstream file(path);
    if (file.is_open())
        file << j.dump(2);
    s_apiKey = key;
}

std::string LoadSettingsApiKey()
{
    std::string path = GetSettingsPath();
    std::ifstream file(path);
    if (!file.is_open()) return {};
    try
    {
        json j = json::parse(file);
        return j.value("api_key", "");
    }
    catch (...) { return {}; }
}

void SaveSettingsOptions(bool hideLegendaryArmory, bool hideEquippedBags, bool autoFocus)
{
    std::string path = GetSettingsPath();
    json j;
    std::ifstream in(path);
    if (in.is_open())
    {
        try { j = json::parse(in); }
        catch (...) { j = json::object(); }
    }
    j["hide_legendary_armory"] = hideLegendaryArmory;
    j["hide_equipped_bags"] = hideEquippedBags;
    j["auto_focus_search"] = autoFocus;
    std::ofstream file(path);
    if (file.is_open())
        file << j.dump(2);
}

void LoadSettingsOptions(bool& hideLegendaryArmory, bool& hideEquippedBags, bool& autoFocus)
{
    std::string path = GetSettingsPath();
    std::ifstream file(path);
    if (!file.is_open()) return;
    try
    {
        json j = json::parse(file);
        hideLegendaryArmory = j.value("hide_legendary_armory", hideLegendaryArmory);
        hideEquippedBags = j.value("hide_equipped_bags", hideEquippedBags);
        autoFocus = j.value("auto_focus_search", autoFocus);
    }
    catch (...) {}
}

static void OnOptionsRender()
{
    if (ImGui::CollapsingHeader("Item Search", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextUnformatted("API Key");

        ImGui::PushItemWidth(300);
        if (ImGui::InputText("##api_key", MainWindow::Instance().GetApiKeyBuffer(), 256))
        {
            s_apiKey = MainWindow::Instance().GetApiKeyBuffer();
        }
        ImGui::PopItemWidth();

        if (ImGui::Button("Save and Reload"))
        {
            s_apiKey = MainWindow::Instance().GetApiKeyBuffer();
            if (!s_apiKey.empty())
            {
                SaveSettingsApiKey(s_apiKey);
                Gw2Api::Instance().SetApiKey(s_apiKey);
                std::thread([]()
                {
                    try
                    {
                        ItemDb::Instance().UpdateFromApi();
                        PlayerItems::Instance().Initialize(Gw2Api::Instance().GetApiKey(), s_cacheDir);
                    }
                    catch (const std::exception& e)
                    {
                        LogWarn(("Data loading failed: " + std::string(e.what())).c_str());
                    }
                    catch (...)
                    {
                        LogWarn("Data loading failed with unknown exception");
                    }
                }).detach();
            }
        }
        ImGui::SameLine();
        bool ready = PlayerItems::Instance().IsReady();
        bool hasDb = ItemDb::Instance().Count() > 0;
        ImGui::TextUnformatted(ready ? "Data loaded" : (hasDb ? "Loading player data..." : "Loading..."));
    }
}

static void OnRender()
{
    if (!s_imguiInitialized)
    {
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(APIDefs->ImguiContext));
        ImGui::SetAllocatorFunctions(
            reinterpret_cast<void* (*)(size_t, void*)>(APIDefs->ImguiMalloc),
            reinterpret_cast<void (*)(void*, void*)>(APIDefs->ImguiFree));
        s_imguiInitialized = true;

        LogInfo("ImGui context initialized");
    }

    IconCache::Instance().ProcessPendingDownloads(2);

    MainWindow::Instance().Render();
}

static void OnQuickAccessToggle(const char* aIdentifier, bool aIsRelease)
{
    if (!aIsRelease)
        MainWindow::Instance().Toggle();
}

static void EnsureExternalLinksExist()
{
    std::string destPath = s_addonDir + "\\external_links.json";
    std::ifstream destCheck(destPath);
    if (destCheck.good()) return;

    json defaultLinks = json::array();
    defaultLinks.push_back({{"Name", "Wiki"}, {"Url", "https://wiki.guildwars2.com/wiki/?search=@@chat@@"}});
    defaultLinks.push_back({{"Name", "GW2BLTC"}, {"Url", "https://www.gw2bltc.com/en/item/@@itemid@@"}});
    defaultLinks.push_back({{"Name", "GW2TP"}, {"Url", "https://www.gw2tp.com/item/@@itemid@@"}});
    defaultLinks.push_back({{"Name", "GW2Efficiency"}, {"Url", "https://gw2efficiency.com/crafting/calculator/a~0!b~1!c~0!d~1-@@itemid@@"}});

    std::ofstream file(destPath);
    if (file.is_open())
        file << defaultLinks.dump(2);
}

static void LoadCallback(AddonAPI_t* aAPI)
{
    APIDefs = aAPI;

    s_addonDir = APIDefs->Paths_GetAddonDirectory("ItemSearch");
    LogInfo(("Addon directory: " + s_addonDir).c_str());

    // Ensure addon directory exists
    CreateDirectoryA(s_addonDir.c_str(), NULL);

    APIDefs->Events_Subscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentity);
    APIDefs->Events_Subscribe(EV_WINDOW_RESIZED, OnWindowResized);
    APIDefs->GUI_Register(RT_Render, OnRender);
    APIDefs->GUI_Register(RT_OptionsRender, OnOptionsRender);

    // Extract embedded icon to addon directory
    std::string iconPath = s_addonDir + "\\icon.png";
    {
        std::ofstream iconFile(iconPath, std::ios::binary);
        if (iconFile.is_open())
        {
            iconFile.write(reinterpret_cast<const char*>(ICON_PNG_DATA), ICON_PNG_SIZE);
            iconFile.close();
            APIDefs->Textures_GetOrCreateFromFile("IS_ICON_DEFAULT", iconPath.c_str());
            APIDefs->Textures_GetOrCreateFromFile("IS_ICON_HOVER", iconPath.c_str());
        }
    }

    APIDefs->QuickAccess_Add("IS_TOGGLE", "IS_ICON_DEFAULT", "IS_ICON_HOVER", "IS_TOGGLE_BIND", "Item Search");
    APIDefs->InputBinds_RegisterWithString("IS_TOGGLE_BIND", OnQuickAccessToggle, "Alt+F");

    EnsureExternalLinksExist();

    s_cacheDir = s_addonDir + "\\cache";
    CreateDirectoryA(s_cacheDir.c_str(), NULL);
    CreateDirectoryA((s_cacheDir + "\\chars").c_str(), NULL);

    MainWindow::Instance().SetCacheDir(s_cacheDir);
    IconCache::Instance().Initialize(s_cacheDir);
    ItemDb::Instance().Initialize(s_cacheDir);

    std::string extLinksPath = s_addonDir + "\\external_links.json";
    ItemDb::Instance().LoadExternalLinks(extLinksPath);

    // Load saved API key
    s_apiKey = LoadSettingsApiKey();
    strncpy(MainWindow::Instance().GetApiKeyBuffer(), s_apiKey.c_str(), 255);

    MainWindow::Instance().Initialize();

    LoadSettingsOptions(
        MainWindow::Instance().GetHideLegendaryArmoryRef(),
        MainWindow::Instance().GetHideEquippedBagsRef(),
        MainWindow::Instance().GetAutoFocusSearchRef()
    );

    // If we have an API key, start loading data
    if (!s_apiKey.empty())
    {
        Gw2Api::Instance().SetApiKey(s_apiKey);
        std::thread([]()
        {
            try
            {
                ItemDb::Instance().UpdateFromApi();
                PlayerItems::Instance().Initialize(Gw2Api::Instance().GetApiKey(), s_cacheDir);
            }
            catch (const std::exception& e)
            {
                LogWarn(("Data loading failed: " + std::string(e.what())).c_str());
            }
            catch (...)
            {
                LogWarn("Data loading failed with unknown exception");
            }
        }).detach();
    }

    LogInfo("Gw2ItemSearch loaded");
}

static void UnloadCallback()
{
    IconCache::Instance().Shutdown();
    PlayerItems::Instance().Stop();

    APIDefs->GUI_Deregister(OnRender);
    APIDefs->GUI_Deregister(OnOptionsRender);
    APIDefs->InputBinds_Deregister("IS_TOGGLE_BIND");
    APIDefs->QuickAccess_Remove("IS_TOGGLE");

    s_imguiInitialized = false;
    LogInfo("Gw2ItemSearch unloaded");
}

static AddonDefinition_t AddonDef =
{
    .Signature   = 0xFFFFFFFF,
    .APIVersion  = NEXUS_API_VERSION,
    .Name        = "Item Search",
    .Version     = { 0, 7, 0, 0 },
    .Author      = "Todd0042",
    .Description = "Searches for items across your account",
    .Load        = LoadCallback,
    .Unload      = UnloadCallback,
    .Flags       = AF_None,
    .Provider    = UP_GitHub,
    .UpdateLink  = "https://github.com/Todd0042/item-search-nexus"
};

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    return &AddonDef;
}
