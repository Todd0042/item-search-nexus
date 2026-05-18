#pragma once
#include "data/inventory_item.h"
#include "addon.h"
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

class MainWindow
{
public:
    static MainWindow& Instance();

    void Initialize();
    void Render();
    void Toggle() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool v) { m_visible = v; }
    char* GetApiKeyBuffer() { return m_apiKeyBuffer; }
    void SetCacheDir(const std::string& dir) { m_cacheDir = dir; }
    const std::string& GetCacheDir() const { return m_cacheDir; }
    bool& GetHideLegendaryArmoryRef() { return m_hideLegendaryArmory; }
    bool& GetHideEquippedBagsRef() { return m_hideEquippedBags; }
    bool& GetAutoFocusSearchRef() { return m_autoFocusSearch; }
    bool IsShowingWizard() const { return m_showSetupWizard; }
    void ShowWizard() { m_showSetupWizard = true; }

private:
    MainWindow() = default;

    void RenderSearchBar();
    void RenderFilterPanel();
    void RenderResults();
    void RenderItemIcon(const InventoryItem& item);
    void RenderItemTooltip(const InventoryItem& item);
    void PerformSearch(const std::string& query);
    void PerformBrowse();

    void RenderSetupWizard();
    void RenderSettingsWindow();
    void RenderOptionsTab();
    void RenderAboutTab();

    bool m_visible = false;
    bool m_firstFrame = true;

    // Search state
    std::string m_searchQuery;
    std::string m_prevSearchQuery;
    std::vector<InventoryItem> m_results;
    std::vector<int> m_matchedItemIds;
    std::mutex m_resultMutex;
    std::atomic<bool> m_searching{ false };

    // Filters
    SearchOptions m_options;
    SearchOptions m_lastSearchOptions;
    bool m_showFilters = false;
    int m_selectedTypeIdx = 0;
    int m_selectedRarityIdx = 0;
    int m_selectedSubTypeIdx = 0;
    std::vector<ItemSubType> m_subTypes;
    int m_selectedStatChoiceIdx = 0;
    int m_selectedStatChoiceRepId = 0;

    // Settings
    bool m_showSettings = false;
    bool m_hideLegendaryArmory = true;
    bool m_hideEquippedBags = false;
    bool m_autoFocusSearch = false;
    int m_selectedSettingsTab = 0;

    // Setup wizard
    bool m_showSetupWizard = false;
    int m_wizardFetchMode = 0;

    // External links context menu
    int m_contextMenuItemId = 0;

    bool m_wasReady = false;

    char m_apiKeyBuffer[256] = { 0 };
    std::string m_cacheDir;
};
